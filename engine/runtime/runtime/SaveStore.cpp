#include "astro/runtime/SaveStore.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <locale>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>
#include <tuple>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace astro::runtime
{
namespace
{
constexpr std::size_t maxProfileIdBytes = 64;
constexpr std::size_t maxGameVersionBytes = 64;
constexpr std::size_t maxSavedProperties = 100000;
constexpr std::size_t maxCustomKeyBytes = 128;
constexpr std::size_t maxCustomStringBytes = 65536;

core::Error saveError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool isSafeToken(const std::string_view value)
{
    if (value.empty() || value.size() > maxProfileIdBytes) return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9') || character == '.' || character == '_' || character == '-';
    });
}

bool isSafeCustomKey(const std::string_view value)
{
    if (value.empty() || value.size() > maxCustomKeyBytes) return false;
    return std::all_of(value.begin(), value.end(), [](const unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
    });
}

bool isKnownProperty(const std::string_view property)
{
    using namespace SaveProperty;
    return property == entityName || property == positionX || property == positionY || property == positionZ
        || property == rotationX || property == rotationY || property == rotationZ || property == scaleX
        || property == scaleY || property == scaleZ || property == spriteVisible;
}

bool isNumberProperty(const std::string_view property)
{
    using namespace SaveProperty;
    return property == positionX || property == positionY || property == positionZ || property == rotationX
        || property == rotationY || property == rotationZ || property == scaleX || property == scaleY || property == scaleZ;
}

using SaveValue = std::variant<std::string, double, bool>;
struct SaveRecord
{
    scene::EntityId entity = 0;
    std::string property;
    SaveValue value;
};
struct SaveDocument
{
    std::uint32_t formatVersion = 0;
    SaveSlotInfo info;
    std::uint32_t profileFingerprint = 0;
    std::vector<SaveRecord> records;
    SaveCustomState customRecords;
};

std::uint32_t crc32(const std::string_view data)
{
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : data)
    {
        crc ^= static_cast<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

std::string hex32(const std::uint32_t value)
{
    std::ostringstream output;
    output << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << value;
    return output.str();
}

std::uint32_t profileFingerprint(const SaveProfile& profile)
{
    std::ostringstream source;
    source.imbue(std::locale::classic());
    source << std::quoted(profile.id()) << ' ' << profile.schemaVersion() << '\n';
    for (const auto& [entity, properties] : profile.fields())
    {
        for (const auto& property : properties)
            source << entity << ' ' << std::quoted(property) << '\n';
    }
    for (const auto& [key, value] : profile.customFields())
        source << "CUSTOM " << std::quoted(key) << ' ' << value.index() << '\n';
    return crc32(source.str());
}

core::Result<std::string> encodeDocument(const SaveDocument& document)
{
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "ASTRO_SAVE " << document.formatVersion << '\n';
    output << "META " << document.info.slot << ' ' << document.info.scene << ' '
           << std::quoted(document.info.profileId) << ' ' << document.info.profileVersion << ' '
           << hex32(document.profileFingerprint) << ' ' << std::quoted(document.info.gameVersion) << ' '
           << document.info.savedAtUnixSeconds << '\n';
    output << "RECORDS " << document.records.size() << '\n';
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& record : document.records)
    {
        output << "FIELD " << record.entity << ' ' << std::quoted(record.property) << ' ';
        if (const auto* text = std::get_if<std::string>(&record.value))
            output << "S " << std::quoted(*text);
        else if (const auto* number = std::get_if<double>(&record.value))
            output << "D " << *number;
        else output << "B " << (std::get<bool>(record.value) ? 1 : 0);
        output << '\n';
    }
    if (document.formatVersion >= 2)
    {
        output << "CUSTOM_RECORDS " << document.customRecords.size() << '\n';
        for (const auto& [key, value] : document.customRecords)
        {
            output << "CUSTOM " << std::quoted(key) << ' ';
            if (const auto* text = std::get_if<std::string>(&value)) output << "S " << std::quoted(*text);
            else if (const auto* number = std::get_if<double>(&value)) output << "D " << *number;
            else output << "B " << (std::get<bool>(value) ? 1 : 0);
            output << '\n';
        }
    }
    output << "END\n";
    if (!output) return core::Result<std::string>::failure(saveError(core::ErrorCode::InternalError, "Could not serialize save document"));
    auto content = output.str();
    content += "CHECKSUM " + hex32(crc32(content)) + "\n";
    return core::Result<std::string>::success(std::move(content));
}

core::Result<SaveDocument> parseDocument(const std::string_view bytes)
{
    const auto footerStart = bytes.rfind("\nCHECKSUM ");
    if (footerStart == std::string_view::npos)
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save file has no checksum footer"));
    const auto dataLength = footerStart + 1;
    const auto footer = bytes.substr(dataLength);
    std::istringstream footerInput{std::string(footer)};
    std::string footerToken, checksumText, trailing;
    if (!(footerInput >> footerToken >> checksumText) || footerToken != "CHECKSUM"
        || (footerInput >> trailing) || checksumText.size() != 8)
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save checksum footer is malformed"));
    std::uint32_t storedChecksum = 0;
    const auto checksumParse = std::from_chars(checksumText.data(), checksumText.data() + checksumText.size(), storedChecksum, 16);
    if (checksumParse.ec != std::errc{} || checksumParse.ptr != checksumText.data() + checksumText.size()
        || crc32(bytes.substr(0, dataLength)) != storedChecksum)
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ValidationError, "Save checksum does not match content"));

    std::istringstream input{std::string(bytes.substr(0, dataLength))};
    input.imbue(std::locale::classic());
    SaveDocument document;
    std::string token, fingerprintText;
    if (!(input >> token >> document.formatVersion) || token != "ASTRO_SAVE")
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save header is missing or malformed"));
    if (document.formatVersion != 1 && document.formatVersion != 2)
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::VersionMismatch, "Unsupported save format version"));
    if (!(input >> token >> document.info.slot >> document.info.scene >> std::quoted(document.info.profileId)
            >> document.info.profileVersion >> fingerprintText >> std::quoted(document.info.gameVersion)
            >> document.info.savedAtUnixSeconds) || token != "META")
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save metadata is malformed"));
    const auto fingerprintParse = std::from_chars(fingerprintText.data(), fingerprintText.data() + fingerprintText.size(),
        document.profileFingerprint, 16);
    if (fingerprintText.size() != 8 || fingerprintParse.ec != std::errc{}
        || fingerprintParse.ptr != fingerprintText.data() + fingerprintText.size())
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save profile fingerprint is malformed"));
    if (document.info.scene == 0 || document.info.profileVersion == 0 || document.info.savedAtUnixSeconds == 0
        || !isSafeToken(document.info.profileId) || !isSafeToken(document.info.gameVersion)
        || document.info.gameVersion.size() > maxGameVersionBytes)
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ValidationError, "Save metadata values are invalid"));
    std::size_t count = 0;
    if (!(input >> token >> count) || token != "RECORDS" || count > maxSavedProperties)
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save record count is invalid"));
    document.records.reserve(count);
    std::set<std::pair<scene::EntityId, std::string>> seen;
    for (std::size_t index = 0; index < count; ++index)
    {
        SaveRecord record;
        std::string type;
        if (!(input >> token >> record.entity >> std::quoted(record.property) >> type) || token != "FIELD"
            || record.entity == 0 || !isKnownProperty(record.property))
            return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save field record is invalid"));
        if (!seen.emplace(record.entity, record.property).second)
            return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ValidationError, "Save contains a duplicate field"));
        if (type == "S" && record.property == SaveProperty::entityName)
        {
            std::string value;
            if (!(input >> std::quoted(value)))
                return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save string value is malformed"));
            record.value = std::move(value);
        }
        else if (type == "D" && isNumberProperty(record.property))
        {
            double value = 0.0;
            if (!(input >> value) || !std::isfinite(value))
                return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save numeric value is invalid"));
            record.value = value;
        }
        else if (type == "B" && record.property == SaveProperty::spriteVisible)
        {
            int value = -1;
            if (!(input >> value) || (value != 0 && value != 1))
                return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save boolean value is invalid"));
            record.value = value != 0;
        }
        else return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::TypeMismatch, "Save value type does not match field"));
        document.records.push_back(std::move(record));
    }
    document.info.propertyCount = count;
    if (document.formatVersion >= 2)
    {
        std::size_t customCount = 0;
        if (!(input >> token >> customCount) || token != "CUSTOM_RECORDS"
            || customCount > maxSavedProperties - count)
            return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Custom save record count is invalid"));
        for (std::size_t index = 0; index < customCount; ++index)
        {
            std::string key, type;
            if (!(input >> token >> std::quoted(key) >> type) || token != "CUSTOM"
                || !isSafeCustomKey(key) || document.customRecords.contains(key))
                return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Custom save record is invalid"));
            if (type == "S")
            {
                std::string value;
                if (!(input >> std::quoted(value)) || value.size() > maxCustomStringBytes)
                    return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Custom string is malformed or too large"));
                document.customRecords.emplace(std::move(key), std::move(value));
            }
            else if (type == "D")
            {
                double value = 0.0;
                if (!(input >> value) || !std::isfinite(value))
                    return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Custom number is invalid"));
                document.customRecords.emplace(std::move(key), value);
            }
            else if (type == "B")
            {
                int value = -1;
                if (!(input >> value) || (value != 0 && value != 1))
                    return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Custom boolean is invalid"));
                document.customRecords.emplace(std::move(key), value != 0);
            }
            else return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::TypeMismatch, "Unknown custom save value type"));
        }
        document.info.propertyCount += customCount;
    }
    if (!(input >> token) || token != "END")
        return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Save file terminator is missing"));
    input >> std::ws;
    if (!input.eof()) return core::Result<SaveDocument>::failure(saveError(core::ErrorCode::ParseError, "Unexpected data after save terminator"));
    return core::Result<SaveDocument>::success(std::move(document));
}

core::Result<std::string> readFile(const std::filesystem::path& path, const std::size_t maximumBytes)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return core::Result<std::string>::failure(saveError(core::ErrorCode::IoError, "Cannot inspect save file: " + error.message()));
    if (size > maximumBytes) return core::Result<std::string>::failure(saveError(core::ErrorCode::ValidationError, "Save file exceeds configured size limit"));
    std::ifstream file(path, std::ios::binary);
    if (!file) return core::Result<std::string>::failure(saveError(core::ErrorCode::IoError, "Cannot open save file for reading"));
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!bytes.empty() && !file.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        return core::Result<std::string>::failure(saveError(core::ErrorCode::IoError, "Could not read complete save file"));
    return core::Result<std::string>::success(std::move(bytes));
}

std::filesystem::path backupFilePath(const std::filesystem::path& primary)
{
    auto path = primary;
#ifdef _WIN32
    path += L".bak";
#else
    path += ".bak";
#endif
    return path;
}

core::Result<void> atomicWrite(const std::filesystem::path& destination, const std::string_view bytes)
{
    static std::atomic<std::uint64_t> sequence{0};
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        auto temporary = destination;
#ifdef _WIN32
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(sequence.fetch_add(1));
        const HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (handle == INVALID_HANDLE_VALUE) continue;
        std::size_t offset = 0;
        bool writeSucceeded = true;
        while (offset < bytes.size())
        {
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, std::numeric_limits<DWORD>::max()));
            DWORD written = 0;
            if (!WriteFile(handle, bytes.data() + offset, chunk, &written, nullptr) || written == 0)
            { writeSucceeded = false; break; }
            offset += written;
        }
        if (!writeSucceeded || !FlushFileBuffers(handle))
        {
            CloseHandle(handle); DeleteFileW(temporary.c_str());
            return core::Result<void>::failure(saveError(core::ErrorCode::IoError, "Could not flush temporary save file"));
        }
        CloseHandle(handle);
        if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary.c_str());
            return core::Result<void>::failure(saveError(core::ErrorCode::IoError, "Could not atomically replace save file (Windows error " + std::to_string(GetLastError()) + ")"));
        }
#else
        temporary += ".tmp." + std::to_string(static_cast<unsigned long long>(::getpid())) + "."
            + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0) continue;
        std::size_t offset = 0;
        bool writeSucceeded = true;
        while (offset < bytes.size())
        {
            const auto written = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { writeSucceeded = false; break; }
            offset += static_cast<std::size_t>(written);
        }
        if (!writeSucceeded || ::fsync(descriptor) != 0)
        {
            ::close(descriptor); std::error_code ignored; std::filesystem::remove(temporary, ignored);
            return core::Result<void>::failure(saveError(core::ErrorCode::IoError, "Could not flush temporary save file"));
        }
        if (::close(descriptor) != 0 || std::rename(temporary.string().c_str(), destination.string().c_str()) != 0)
        {
            std::error_code ignored; std::filesystem::remove(temporary, ignored);
            return core::Result<void>::failure(saveError(core::ErrorCode::IoError, "Could not atomically replace save file"));
        }
#endif
        return core::Result<void>::success();
    }
    return core::Result<void>::failure(saveError(core::ErrorCode::IoError, "Could not reserve a unique temporary save file"));
}

core::Result<void> validateExpected(const SaveDocument& document, const std::size_t slot,
    const scene::Scene& state, const SaveProfile& profile, const std::string_view gameVersion)
{
    if (document.info.slot != slot || document.info.scene != state.id())
        return core::Result<void>::failure(saveError(core::ErrorCode::ValidationError, "Save slot or Scene ID does not match the requested load"));
    if (document.info.profileId != profile.id() || document.info.profileVersion != profile.schemaVersion()
        || document.profileFingerprint != profileFingerprint(profile))
        return core::Result<void>::failure(saveError(core::ErrorCode::VersionMismatch, "Save profile or selected fields do not match current profile"));
    if (document.info.gameVersion != gameVersion)
        return core::Result<void>::failure(saveError(core::ErrorCode::VersionMismatch, "Save belongs to a different game version"));
    for (const auto& record : document.records)
    {
        const auto selected = profile.fields().find(record.entity);
        if (selected == profile.fields().end()
            || std::find(selected->second.begin(), selected->second.end(), record.property) == selected->second.end())
            return core::Result<void>::failure(saveError(core::ErrorCode::ValidationError, "Save contains a field outside the selected profile"));
    }
    if (document.customRecords.size() != profile.customFields().size())
        return core::Result<void>::failure(saveError(core::ErrorCode::VersionMismatch, "Save custom fields do not match selected profile"));
    for (const auto& [key, value] : document.customRecords)
    {
        const auto selected = profile.customFields().find(key);
        if (selected == profile.customFields().end() || selected->second.index() != value.index())
            return core::Result<void>::failure(saveError(core::ErrorCode::VersionMismatch, "Save custom field type or selection differs from profile"));
    }
    if (!profile.customFields().empty() && document.formatVersion < 2)
        return core::Result<void>::failure(saveError(core::ErrorCode::VersionMismatch, "Save format does not contain selected custom fields"));
    return core::Result<void>::success();
}

core::Result<SaveValue> readSceneValue(const scene::Scene& state, const scene::EntityId entity,
    const std::string_view property)
{
    if (property == SaveProperty::entityName)
    {
        const auto* value = state.find(entity);
        if (!value) return core::Result<SaveValue>::failure(saveError(core::ErrorCode::NotFound, "Selected save entity is missing"));
        return core::Result<SaveValue>::success(value->name);
    }
    if (property == SaveProperty::spriteVisible)
    {
        const auto* value = state.spriteRenderer(entity);
        if (!value) return core::Result<SaveValue>::failure(saveError(core::ErrorCode::NotFound, "Selected SpriteRenderer component is missing"));
        return core::Result<SaveValue>::success(value->visible);
    }
    const auto* component = state.transform(entity);
    if (!component) return core::Result<SaveValue>::failure(saveError(core::ErrorCode::NotFound, "Selected Transform component is missing"));
    const auto& transform = component->value;
    if (property == SaveProperty::positionX) return core::Result<SaveValue>::success(transform.position.x);
    if (property == SaveProperty::positionY) return core::Result<SaveValue>::success(transform.position.y);
    if (property == SaveProperty::positionZ) return core::Result<SaveValue>::success(transform.position.z);
    if (property == SaveProperty::rotationX) return core::Result<SaveValue>::success(transform.rotationDegrees.x);
    if (property == SaveProperty::rotationY) return core::Result<SaveValue>::success(transform.rotationDegrees.y);
    if (property == SaveProperty::rotationZ) return core::Result<SaveValue>::success(transform.rotationDegrees.z);
    if (property == SaveProperty::scaleX) return core::Result<SaveValue>::success(transform.scale.x);
    if (property == SaveProperty::scaleY) return core::Result<SaveValue>::success(transform.scale.y);
    if (property == SaveProperty::scaleZ) return core::Result<SaveValue>::success(transform.scale.z);
    return core::Result<SaveValue>::failure(saveError(core::ErrorCode::NotFound, "Unknown save property"));
}

core::Result<void> applySceneValue(scene::Scene& state, const SaveRecord& record, bool& missingComponent)
{
    if (!state.find(record.entity)) return core::Result<void>::failure(saveError(core::ErrorCode::NotFound, "Selected save entity is missing"));
    if (record.property == SaveProperty::entityName)
        return state.setProperty(record.entity, "name", scene::PropertyValue{std::get<std::string>(record.value)});
    if (record.property == SaveProperty::spriteVisible)
    {
        const auto* component = state.spriteRenderer(record.entity);
        if (!component) { missingComponent = true; return core::Result<void>::success(); }
        const auto asset = component->texture;
        return state.setSpriteRenderer(record.entity, asset, std::get<bool>(record.value));
    }
    const auto* component = state.transform(record.entity);
    if (!component) { missingComponent = true; return core::Result<void>::success(); }
    auto value = component->value;
    const auto number = std::get<double>(record.value);
    if (record.property == SaveProperty::positionX) value.position.x = number;
    else if (record.property == SaveProperty::positionY) value.position.y = number;
    else if (record.property == SaveProperty::positionZ) value.position.z = number;
    else if (record.property == SaveProperty::rotationX) value.rotationDegrees.x = number;
    else if (record.property == SaveProperty::rotationY) value.rotationDegrees.y = number;
    else if (record.property == SaveProperty::rotationZ) value.rotationDegrees.z = number;
    else if (record.property == SaveProperty::scaleX) value.scale.x = number;
    else if (record.property == SaveProperty::scaleY) value.scale.y = number;
    else if (record.property == SaveProperty::scaleZ) value.scale.z = number;
    return state.setTransform(record.entity, value);
}
}

core::Result<SaveProfile> SaveProfile::create(std::string id, const std::uint32_t schemaVersion)
{
    if (!isSafeToken(id) || schemaVersion == 0)
        return core::Result<SaveProfile>::failure(saveError(core::ErrorCode::InvalidArgument, "Save profile requires a safe ID and non-zero schema version"));
    return core::Result<SaveProfile>::success(SaveProfile(std::move(id), schemaVersion));
}

core::Result<void> SaveProfile::addField(const scene::EntityId entity, std::string property)
{
    if (entity == 0 || entity == std::numeric_limits<scene::EntityId>::max() || !isKnownProperty(property))
        return core::Result<void>::failure(saveError(core::ErrorCode::InvalidArgument, "Save field requires a non-zero Entity ID and supported property"));
    auto& properties = fields_[entity];
    if (std::find(properties.begin(), properties.end(), property) != properties.end())
        return core::Result<void>::failure(saveError(core::ErrorCode::AlreadyExists, "Save field is already selected"));
    properties.push_back(std::move(property));
    std::sort(properties.begin(), properties.end());
    return core::Result<void>::success();
}

core::Result<void> SaveProfile::removeField(const scene::EntityId entity, const std::string_view property)
{
    const auto found = fields_.find(entity);
    if (found == fields_.end()) return core::Result<void>::failure(saveError(core::ErrorCode::NotFound, "Save entity selection was not found"));
    const auto item = std::find(found->second.begin(), found->second.end(), property);
    if (item == found->second.end()) return core::Result<void>::failure(saveError(core::ErrorCode::NotFound, "Save field selection was not found"));
    found->second.erase(item);
    if (found->second.empty()) fields_.erase(found);
    return core::Result<void>::success();
}

core::Result<void> SaveProfile::addCustomField(std::string key, SaveCustomValue typeExample)
{
    if (!isSafeCustomKey(key))
        return core::Result<void>::failure(saveError(core::ErrorCode::InvalidArgument, "Custom save field key is invalid"));
    if (const auto* number = std::get_if<double>(&typeExample); number && !std::isfinite(*number))
        return core::Result<void>::failure(saveError(core::ErrorCode::InvalidArgument, "Custom save numeric type example must be finite"));
    if (const auto* text = std::get_if<std::string>(&typeExample); text && text->size() > maxCustomStringBytes)
        return core::Result<void>::failure(saveError(core::ErrorCode::InvalidArgument, "Custom save string type example is too large"));
    if (!customFields_.emplace(std::move(key), std::move(typeExample)).second)
        return core::Result<void>::failure(saveError(core::ErrorCode::AlreadyExists, "Custom save field is already selected"));
    return core::Result<void>::success();
}

core::Result<void> SaveProfile::removeCustomField(const std::string_view key)
{
    if (customFields_.erase(std::string(key)) == 0)
        return core::Result<void>::failure(saveError(core::ErrorCode::NotFound, "Custom save field was not selected"));
    return core::Result<void>::success();
}

core::Result<SaveStore> SaveStore::open(std::filesystem::path directory,
    const std::size_t slotCount, const std::size_t maximumFileBytes)
{
    constexpr std::size_t maxSlots = 100;
    constexpr std::size_t maxFileBytes = 16 * 1024 * 1024;
    if (directory.empty() || slotCount == 0 || slotCount > maxSlots
        || maximumFileBytes == 0 || maximumFileBytes > maxFileBytes)
        return core::Result<SaveStore>::failure(saveError(core::ErrorCode::InvalidArgument, "Save store path, slot count, or file size limit is invalid"));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || !std::filesystem::is_directory(directory, error) || error)
        return core::Result<SaveStore>::failure(saveError(core::ErrorCode::IoError, "Could not open save directory"));
    return core::Result<SaveStore>::success(SaveStore(std::move(directory), slotCount, maximumFileBytes));
}

core::Result<SaveSlotInspection> SaveStore::inspectExistingSlot(const std::filesystem::path& directory,
    const std::size_t slot, const std::size_t maximumFileBytes)
{
    constexpr std::size_t maxFileBytes = 16 * 1024 * 1024;
    if (directory.empty() || slot >= 100 || maximumFileBytes == 0 || maximumFileBytes > maxFileBytes)
        return core::Result<SaveSlotInspection>::failure(saveError(core::ErrorCode::InvalidArgument,
            "Save inspection path, slot, or file size limit is invalid"));
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error)
        return core::Result<SaveSlotInspection>::failure(saveError(core::ErrorCode::NotFound,
            "Existing save directory was not found"));
    const SaveStore inspector(directory, 100, maximumFileBytes);
    return inspector.inspectDetailed(slot);
}

core::Result<std::filesystem::path> SaveStore::slotPath(const std::size_t slot) const
{
    if (slot >= slotCount_) return core::Result<std::filesystem::path>::failure(saveError(core::ErrorCode::InvalidArgument, "Save slot is outside configured range"));
    return core::Result<std::filesystem::path>::success(directory_ / ("slot-" + std::to_string(slot) + ".astro-save"));
}

core::Result<SaveWriteResult> SaveStore::save(const std::size_t slot, const scene::Scene& state,
    const SaveProfile& profile, const std::string_view gameVersion) const
{
    return save(slot, state, profile, gameVersion, SaveCustomState{});
}

core::Result<SaveWriteResult> SaveStore::save(const std::size_t slot, const scene::Scene& state,
    const SaveProfile& profile, const std::string_view gameVersion, const SaveCustomState& customState) const
{
    const auto targetResult = slotPath(slot);
    if (!targetResult) return core::Result<SaveWriteResult>::failure(targetResult.error());
    if (state.id() == 0 || state.id() == std::numeric_limits<scene::SceneId>::max()
        || profile.schemaVersion() == 0 || !isSafeToken(profile.id()) || gameVersion.empty()
        || gameVersion.size() > maxGameVersionBytes || !isSafeToken(gameVersion))
        return core::Result<SaveWriteResult>::failure(saveError(core::ErrorCode::InvalidArgument, "Save profile or game version is invalid"));
    if (customState.size() != profile.customFields().size())
        return core::Result<SaveWriteResult>::failure(saveError(core::ErrorCode::ValidationError, "Custom save state does not match selected profile fields"));
    for (const auto& [key, value] : customState)
    {
        const auto selected = profile.customFields().find(key);
        if (!isSafeCustomKey(key) || selected == profile.customFields().end() || selected->second.index() != value.index())
            return core::Result<SaveWriteResult>::failure(saveError(core::ErrorCode::TypeMismatch, "Custom save state contains an unselected key or wrong value type"));
        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
            return core::Result<SaveWriteResult>::failure(saveError(core::ErrorCode::ValidationError, "Custom save number must be finite"));
        if (const auto* text = std::get_if<std::string>(&value); text && text->size() > maxCustomStringBytes)
            return core::Result<SaveWriteResult>::failure(saveError(core::ErrorCode::ValidationError, "Custom save string exceeds size limit"));
    }
    std::vector<SaveRecord> records;
    std::set<scene::EntityId> missingEntities;
    std::set<scene::EntityId> missingComponents;
    for (const auto& [entity, properties] : profile.fields())
    {
        if (!state.find(entity)) { missingEntities.insert(entity); continue; }
        for (const auto& property : properties)
        {
            auto value = readSceneValue(state, entity, property);
            if (!value)
            {
                if (value.error().code == core::ErrorCode::NotFound) missingComponents.insert(entity);
                else return core::Result<SaveWriteResult>::failure(value.error());
                continue;
            }
            records.push_back({entity, property, std::move(value).value()});
        }
    }
    if (records.size() + customState.size() > maxSavedProperties)
        return core::Result<SaveWriteResult>::failure(saveError(core::ErrorCode::ValidationError, "Save profile selects too many fields"));
    std::sort(records.begin(), records.end(), [](const SaveRecord& lhs, const SaveRecord& rhs) {
        return std::tie(lhs.entity, lhs.property) < std::tie(rhs.entity, rhs.property);
    });
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    if (timestamp <= 0) return core::Result<SaveWriteResult>::failure(saveError(core::ErrorCode::InternalError, "System clock returned an invalid save timestamp"));
    SaveDocument document;
    document.formatVersion = customState.empty() ? 1U : 2U;
    document.info = {slot, state.id(), profile.id(), profile.schemaVersion(), std::string(gameVersion),
        static_cast<std::uint64_t>(timestamp), records.size() + customState.size(), false};
    document.profileFingerprint = profileFingerprint(profile);
    document.records = std::move(records);
    document.customRecords = customState;
    auto encoded = encodeDocument(document);
    if (!encoded) return core::Result<SaveWriteResult>::failure(encoded.error());
    if (encoded.value().size() > maximumFileBytes_)
        return core::Result<SaveWriteResult>::failure(saveError(core::ErrorCode::ValidationError, "Serialized save exceeds configured size limit"));

    const auto target = targetResult.value();
    const auto backup = backupFilePath(target);
    std::error_code filesystemError;
    if (std::filesystem::exists(target, filesystemError) && !filesystemError)
    {
        auto oldBytes = readFile(target, maximumFileBytes_);
        if (oldBytes && parseDocument(oldBytes.value()))
        {
            const auto backupWrite = atomicWrite(backup, oldBytes.value());
            if (!backupWrite) return core::Result<SaveWriteResult>::failure(backupWrite.error());
        }
    }
    const auto write = atomicWrite(target, encoded.value());
    if (!write) return core::Result<SaveWriteResult>::failure(write.error());

    SaveWriteResult result;
    result.slotInfo = document.info;
    result.missingEntities = missingEntities.size();
    result.missingComponents = missingComponents.size();
    return core::Result<SaveWriteResult>::success(std::move(result));
}

core::Result<SaveSlotInfo> SaveStore::inspect(const std::size_t slot) const
{
    auto detailed = inspectDetailed(slot);
    if (!detailed) return core::Result<SaveSlotInfo>::failure(detailed.error());
    return core::Result<SaveSlotInfo>::success(std::move(detailed).value().info);
}

core::Result<SaveSlotInspection> SaveStore::inspectDetailed(const std::size_t slot) const
{
    const auto pathResult = slotPath(slot);
    if (!pathResult) return core::Result<SaveSlotInspection>::failure(pathResult.error());
    const auto primary = pathResult.value();
    auto primaryBytes = readFile(primary, maximumFileBytes_);
    if (primaryBytes)
    {
        auto document = parseDocument(primaryBytes.value());
        if (document && document.value().info.slot == slot)
            return core::Result<SaveSlotInspection>::success({document.value().info,
                std::move(document).value().customRecords, primaryBytes.value().size()});
        if (!document && document.error().code == core::ErrorCode::VersionMismatch)
            return core::Result<SaveSlotInspection>::failure(document.error());
    }
    const auto backupPath = backupFilePath(primary);
    auto backupBytes = readFile(backupPath, maximumFileBytes_);
    if (backupBytes)
    {
        auto document = parseDocument(backupBytes.value());
        if (document)
        {
            auto info = document.value().info;
            if (info.slot != slot)
                return core::Result<SaveSlotInspection>::failure(saveError(core::ErrorCode::ValidationError,
                    "Backup metadata slot does not match its file"));
            info.fromBackup = true;
            return core::Result<SaveSlotInspection>::success({std::move(info),
                std::move(document).value().customRecords, backupBytes.value().size()});
        }
    }
    if (!primaryBytes && !backupBytes)
        return core::Result<SaveSlotInspection>::failure(saveError(core::ErrorCode::NotFound, "Save slot is empty"));
    if (!primaryBytes && primaryBytes.error().code != core::ErrorCode::NotFound)
        return core::Result<SaveSlotInspection>::failure(primaryBytes.error());
    if (!backupBytes && backupBytes.error().code != core::ErrorCode::NotFound)
        return core::Result<SaveSlotInspection>::failure(backupBytes.error());
    return core::Result<SaveSlotInspection>::failure(saveError(core::ErrorCode::ValidationError,
        "Neither save slot nor its backup is valid"));
}

core::Result<SaveLoadResult> SaveStore::load(const std::size_t slot, scene::Scene& state,
    const SaveProfile& profile, const std::string_view gameVersion) const
{
    SaveCustomState customState;
    return load(slot, state, profile, gameVersion, customState);
}

core::Result<SaveLoadResult> SaveStore::load(const std::size_t slot, scene::Scene& state,
    const SaveProfile& profile, const std::string_view gameVersion, SaveCustomState& customState) const
{
    const auto pathResult = slotPath(slot);
    if (!pathResult) return core::Result<SaveLoadResult>::failure(pathResult.error());
    if (!isSafeToken(gameVersion) || gameVersion.size() > maxGameVersionBytes)
        return core::Result<SaveLoadResult>::failure(saveError(core::ErrorCode::InvalidArgument, "Game version is invalid"));
    const auto primary = pathResult.value();
    auto primaryBytes = readFile(primary, maximumFileBytes_);
    core::Result<SaveDocument> document = primaryBytes
        ? parseDocument(primaryBytes.value())
        : core::Result<SaveDocument>::failure(primaryBytes.error());
    bool fromBackup = false;
    if (!document)
    {
        if (document.error().code == core::ErrorCode::VersionMismatch)
            return core::Result<SaveLoadResult>::failure(document.error());
        const auto backupPath = backupFilePath(primary);
        auto backupBytes = readFile(backupPath, maximumFileBytes_);
        if (backupBytes)
        {
            auto backupDocument = parseDocument(backupBytes.value());
            if (backupDocument) { document = std::move(backupDocument); fromBackup = true; }
        }
    }
    if (!document) return core::Result<SaveLoadResult>::failure(document.error());
    const auto compatible = validateExpected(document.value(), slot, state, profile, gameVersion);
    if (!compatible) return core::Result<SaveLoadResult>::failure(compatible.error());

    auto candidate = state;
    SaveLoadResult result;
    result.slotInfo = document.value().info;
    result.slotInfo.fromBackup = fromBackup;
    std::set<scene::EntityId> missingEntities, missingComponents;
    for (const auto& record : document.value().records)
    {
        if (!candidate.find(record.entity)) { missingEntities.insert(record.entity); continue; }
        bool missingComponent = false;
        const auto applied = applySceneValue(candidate, record, missingComponent);
        if (!applied) return core::Result<SaveLoadResult>::failure(applied.error());
        if (missingComponent) missingComponents.insert(record.entity);
        else ++result.appliedProperties;
    }
    result.missingEntities = missingEntities.size();
    result.missingComponents = missingComponents.size();
    auto customCandidate = document.value().customRecords;
    state = std::move(candidate);
    customState = std::move(customCandidate);
    return core::Result<SaveLoadResult>::success(std::move(result));
}
}
