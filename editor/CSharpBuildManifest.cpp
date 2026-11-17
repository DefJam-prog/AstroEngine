#include "CSharpBuildManifest.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string_view>
#include <variant>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace astro::editor
{
namespace
{
constexpr std::size_t maxManifestBytes = 1024 * 1024;
using ErrorCode = astro::core::ErrorCode;

astro::core::Result<void> failure(ErrorCode code, std::string message)
{
    return astro::core::Result<void>::failure({code, std::move(message)});
}

bool validUtf8(const std::string& value)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t i = 0;
    while (i < value.size())
    {
        const auto first = bytes[i++];
        if (first < 0x80) continue;
        unsigned int count{};
        std::uint32_t point{};
        if ((first & 0xe0) == 0xc0) { count = 1; point = first & 0x1f; if (point < 2) return false; }
        else if ((first & 0xf0) == 0xe0) { count = 2; point = first & 0x0f; }
        else if ((first & 0xf8) == 0xf0) { count = 3; point = first & 0x07; }
        else return false;
        if (i + count > value.size()) return false;
        for (unsigned int n = 0; n < count; ++n)
        {
            const auto next = bytes[i++];
            if ((next & 0xc0) != 0x80) return false;
            point = (point << 6) | (next & 0x3f);
        }
        if ((count == 2 && point < 0x800) || (count == 3 && point < 0x10000)
            || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    return true;
}

std::string pathUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}

bool withinRoot(const std::filesystem::path& root, const std::filesystem::path& path)
{
    auto r = root.begin();
    auto p = path.begin();
    for (; r != root.end() && p != path.end(); ++r, ++p)
    {
#ifdef _WIN32
        if (_wcsicmp(r->c_str(), p->c_str()) != 0) return false;
#else
        if (*r != *p) return false;
#endif
    }
    return r == root.end();
}

bool isReparsePoint(const std::filesystem::path& path)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code ec;
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path, ec));
#endif
}

astro::core::Result<std::filesystem::path> safeManifestPath(const std::filesystem::path& root, bool forWrite)
{
    if (root.empty()) return astro::core::Result<std::filesystem::path>::failure(
        {ErrorCode::InvalidArgument, "Project root is empty"});
    std::error_code ec;
    const auto project = std::filesystem::weakly_canonical(root, ec);
    if (ec || !std::filesystem::is_directory(project, ec))
        return astro::core::Result<std::filesystem::path>::failure(
            {ErrorCode::InvalidArgument, "Project root is not an existing directory"});
    const auto metadata = project / L".astro";
    if (std::filesystem::exists(metadata, ec))
    {
        if (ec || isReparsePoint(metadata)) return astro::core::Result<std::filesystem::path>::failure(
            {ErrorCode::InvalidArgument, "Project .astro directory cannot be a reparse point"});
    }
    if (ec) return astro::core::Result<std::filesystem::path>::failure(
        {ErrorCode::IoError, "Cannot inspect project .astro directory: " + ec.message()});
    const auto path = metadata / L"csharp-build.json";
    if (std::filesystem::exists(path, ec) && (ec || isReparsePoint(path)))
        return astro::core::Result<std::filesystem::path>::failure(
            {ErrorCode::InvalidArgument, "C# build manifest cannot be a reparse point"});
    if (ec) return astro::core::Result<std::filesystem::path>::failure(
        {ErrorCode::IoError, "Cannot inspect C# build manifest: " + ec.message()});
    if (!forWrite && !std::filesystem::exists(path, ec))
        return astro::core::Result<std::filesystem::path>::failure(
            {ErrorCode::NotFound, "Project C# build manifest does not exist"});
    if (ec) return astro::core::Result<std::filesystem::path>::failure(
        {ErrorCode::IoError, "Cannot inspect C# build manifest: " + ec.message()});
    const auto resolved = std::filesystem::weakly_canonical(path, ec);
    if (ec || !withinRoot(project, resolved)) return astro::core::Result<std::filesystem::path>::failure(
        {ErrorCode::InvalidArgument, "C# build manifest path resolves outside the project"});
    return astro::core::Result<std::filesystem::path>::success(path);
}

astro::core::Result<std::vector<std::string>> scriptInventory(const std::filesystem::path& projectRoot)
{
    std::error_code ec;
    const auto project = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec) return astro::core::Result<std::vector<std::string>>::failure({ErrorCode::IoError, ec.message()});
    const auto scripts = project / L"Scripts";
    if (!std::filesystem::exists(scripts, ec))
    {
        if (ec) return astro::core::Result<std::vector<std::string>>::failure({ErrorCode::IoError, ec.message()});
        return astro::core::Result<std::vector<std::string>>::success({});
    }
    if (isReparsePoint(scripts)) return astro::core::Result<std::vector<std::string>>::failure(
        {ErrorCode::InvalidArgument, "Project Scripts directory cannot be a reparse point"});
    const auto scriptsRoot = std::filesystem::weakly_canonical(scripts, ec);
    if (ec || !withinRoot(project, scriptsRoot)) return astro::core::Result<std::vector<std::string>>::failure(
        {ErrorCode::InvalidArgument, "Project Scripts directory resolves outside the project"});
    std::vector<std::string> result;
    std::filesystem::recursive_directory_iterator it(scriptsRoot, std::filesystem::directory_options::none, ec), end;
    for (; !ec && it != end; it.increment(ec))
    {
        if (it->is_symlink(ec) || isReparsePoint(it->path()))
            return astro::core::Result<std::vector<std::string>>::failure(
                {ErrorCode::InvalidArgument, "Scripts directory cannot contain symlink/reparse entries"});
        if (it->is_regular_file(ec) && it->path().extension() == L".cs")
            result.push_back("Scripts/" + pathUtf8(it->path().lexically_relative(scriptsRoot)));
    }
    if (ec) return astro::core::Result<std::vector<std::string>>::failure({ErrorCode::IoError, "Cannot enumerate Scripts directory: " + ec.message()});
    return astro::core::Result<std::vector<std::string>>::success(std::move(result));
}

struct JsonValue final
{
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    std::variant<std::nullptr_t, bool, std::int64_t, std::string, Array, Object> value;
};

class JsonParser final
{
public:
    explicit JsonParser(std::string_view text) : text_(text) {}
    bool parse(JsonValue& value, std::string& error)
    {
        skip();
        if (!parseValue(value, 0)) { error = error_; return false; }
        skip();
        if (position_ != text_.size()) { error = "Unexpected characters after JSON value"; return false; }
        return true;
    }
private:
    std::string_view text_;
    std::size_t position_{};
    std::string error_;

    void skip() { while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t'
        || text_[position_] == '\r' || text_[position_] == '\n')) ++position_; }
    bool fail(std::string message) { if (error_.empty()) error_ = std::move(message); return false; }
    bool take(char ch) { if (position_ >= text_.size() || text_[position_] != ch) return false; ++position_; return true; }
    static void appendUtf8(std::string& out, std::uint32_t point)
    {
        if (point <= 0x7f) out.push_back(static_cast<char>(point));
        else if (point <= 0x7ff) { out.push_back(static_cast<char>(0xc0 | (point >> 6))); out.push_back(static_cast<char>(0x80 | (point & 0x3f))); }
        else if (point <= 0xffff) { out.push_back(static_cast<char>(0xe0 | (point >> 12))); out.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (point & 0x3f))); }
        else { out.push_back(static_cast<char>(0xf0 | (point >> 18))); out.push_back(static_cast<char>(0x80 | ((point >> 12) & 0x3f))); out.push_back(static_cast<char>(0x80 | ((point >> 6) & 0x3f))); out.push_back(static_cast<char>(0x80 | (point & 0x3f))); }
    }
    bool codeUnit(std::uint32_t& result)
    {
        if (position_ + 4 > text_.size()) return fail("Incomplete JSON Unicode escape");
        result = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char ch = text_[position_++];
            result <<= 4;
            if (ch >= '0' && ch <= '9') result |= static_cast<unsigned int>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') result |= static_cast<unsigned int>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') result |= static_cast<unsigned int>(ch - 'A' + 10);
            else return fail("Invalid JSON Unicode escape");
        }
        return true;
    }
    bool parseString(std::string& result)
    {
        if (!take('"')) return fail("Expected JSON string");
        while (position_ < text_.size())
        {
            const unsigned char ch = static_cast<unsigned char>(text_[position_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return fail("Control character in JSON string");
            if (ch != '\\') { result.push_back(static_cast<char>(ch)); continue; }
            if (position_ == text_.size()) return fail("Incomplete JSON escape");
            switch (text_[position_++])
            {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u':
            {
                std::uint32_t point{};
                if (!codeUnit(point)) return false;
                if (point >= 0xd800 && point <= 0xdbff)
                {
                    if (position_ + 2 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u')
                        return fail("JSON high surrogate lacks a low surrogate");
                    position_ += 2;
                    std::uint32_t low{};
                    if (!codeUnit(low) || low < 0xdc00 || low > 0xdfff) return fail("Invalid JSON low surrogate");
                    point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
                }
                else if (point >= 0xdc00 && point <= 0xdfff) return fail("Unexpected JSON low surrogate");
                appendUtf8(result, point);
                break;
            }
            default: return fail("Unknown JSON escape");
            }
        }
        return fail("Unterminated JSON string");
    }
    bool parseValue(JsonValue& out, unsigned int depth)
    {
        if (depth > 16) return fail("JSON nesting exceeds the manifest limit");
        skip();
        if (position_ >= text_.size()) return fail("Unexpected end of JSON");
        if (text_[position_] == '"') { std::string s; if (!parseString(s)) return false; out.value = std::move(s); return true; }
        if (text_[position_] == '{') return parseObject(out, depth + 1);
        if (text_[position_] == '[') return parseArray(out, depth + 1);
        if (text_.substr(position_, 4) == "true") { position_ += 4; out.value = true; return true; }
        if (text_.substr(position_, 5) == "false") { position_ += 5; out.value = false; return true; }
        if (text_.substr(position_, 4) == "null") { position_ += 4; out.value = nullptr; return true; }
        return parseInteger(out);
    }
    bool parseObject(JsonValue& out, unsigned int depth)
    {
        take('{'); skip(); JsonValue::Object object;
        if (take('}')) { out.value = std::move(object); return true; }
        while (true)
        {
            skip(); std::string key;
            if (!parseString(key)) return false;
            skip(); if (!take(':')) return fail("Expected colon after JSON object key");
            JsonValue value;
            if (!parseValue(value, depth)) return false;
            if (!object.emplace(std::move(key), std::move(value)).second) return fail("Duplicate JSON object key");
            skip(); if (take('}')) break;
            if (!take(',')) return fail("Expected comma in JSON object");
        }
        out.value = std::move(object); return true;
    }
    bool parseArray(JsonValue& out, unsigned int depth)
    {
        take('['); skip(); JsonValue::Array array;
        if (take(']')) { out.value = std::move(array); return true; }
        while (true)
        {
            JsonValue value;
            if (!parseValue(value, depth)) return false;
            array.push_back(std::move(value));
            skip(); if (take(']')) break;
            if (!take(',')) return fail("Expected comma in JSON array");
        }
        out.value = std::move(array); return true;
    }
    bool parseInteger(JsonValue& out)
    {
        const auto start = position_;
        if (position_ < text_.size() && text_[position_] == '-') ++position_;
        if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') return fail("Invalid JSON value");
        if (text_[position_] == '0') ++position_;
        else while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        std::int64_t number{};
        const auto parsed = std::from_chars(text_.data() + start, text_.data() + position_, number);
        if (parsed.ec != std::errc{} || parsed.ptr != text_.data() + position_) return fail("JSON integer is out of range");
        out.value = number; return true;
    }
};

void appendEscaped(std::string& out, const std::string& value)
{
    out.push_back('"');
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value)
    {
        switch (ch)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) { out += "\\u00"; out.push_back(hex[ch >> 4]); out.push_back(hex[ch & 15]); }
            else out.push_back(static_cast<char>(ch));
        }
    }
    out.push_back('"');
}

bool asciiTypeNameValid(const std::string& name)
{
    if (name.empty() || name.size() > 512) return false;
    for (const unsigned char ch : name) if (ch <= 0x20 || ch == 0x7f) return false;
    return true;
}
}

std::filesystem::path CSharpBuildManifestFile::manifestPath(const std::filesystem::path& projectRoot)
{
    return projectRoot / L".astro" / L"csharp-build.json";
}

astro::core::Result<void> CSharpBuildManifestFile::validate(const CSharpBuildManifest& manifest,
    const std::vector<std::string>& projectScriptPaths)
{
    if (manifest.schemaVersion != 1) return failure(ErrorCode::VersionMismatch, "Only C# build manifest schemaVersion 1 is supported");
    if (!validUtf8(manifest.sdkVersion) || manifest.sdkVersion.size() > 32
        || manifest.sdkVersion.rfind("10.0.", 0) != 0 || manifest.sdkVersion.size() <= 5
        || !std::all_of(manifest.sdkVersion.begin() + 5, manifest.sdkVersion.end(), [](unsigned char ch) { return ch >= '0' && ch <= '9'; }))
        return failure(ErrorCode::ValidationError, "sdkVersion must pin a .NET 10.0.x SDK version");
    if (manifest.targetFramework != "net10.0")
        return failure(ErrorCode::ValidationError, "Only targetFramework net10.0 is supported by this prototype");
    if (manifest.assemblyName.empty() || manifest.assemblyName.size() > 100
        || !((manifest.assemblyName[0] >= 'A' && manifest.assemblyName[0] <= 'Z')
            || (manifest.assemblyName[0] >= 'a' && manifest.assemblyName[0] <= 'z') || manifest.assemblyName[0] == '_'))
        return failure(ErrorCode::ValidationError, "assemblyName must be a simple assembly name");
    for (const unsigned char ch : manifest.assemblyName)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')
            || ch == '_' || ch == '.' || ch == '-'))
            return failure(ErrorCode::ValidationError, "assemblyName contains path or unsupported characters");
    if (manifest.scriptBindings.size() > 4096) return failure(ErrorCode::ValidationError, "Too many C# script bindings");
    std::set<std::string> uniqueSources;
    for (const auto& binding : manifest.scriptBindings)
    {
        const auto& source = binding.sourcePath;
        if (!validUtf8(source) || source.rfind("Scripts/", 0) != 0 || source.find('\\') != std::string::npos
            || source.find("..") != std::string::npos || source.find(':') != std::string::npos
            || source.size() <= 11 || source.substr(source.size() - 3) != ".cs")
            return failure(ErrorCode::ValidationError, "Binding sourcePath must name a .cs file under Scripts/");
        if (!uniqueSources.insert(source).second) return failure(ErrorCode::AlreadyExists, "A C# source has more than one selected managed type");
        if (!validUtf8(binding.managedTypeName) || !asciiTypeNameValid(binding.managedTypeName))
            return failure(ErrorCode::ValidationError, "managedTypeName must be 1–512 UTF-8 bytes without whitespace/control characters");
        if (!projectScriptPaths.empty() && std::find(projectScriptPaths.begin(), projectScriptPaths.end(), source) == projectScriptPaths.end())
            return failure(ErrorCode::NotFound, "Binding source file does not exist under Scripts: " + source);
    }
    return astro::core::Result<void>::success();
}

astro::core::Result<CSharpBuildManifest> CSharpBuildManifestFile::parse(const std::string& utf8Json)
{
    if (utf8Json.size() > maxManifestBytes) return astro::core::Result<CSharpBuildManifest>::failure(
        {ErrorCode::ValidationError, "C# build manifest exceeds 1 MiB"});
    if (!validUtf8(utf8Json)) return astro::core::Result<CSharpBuildManifest>::failure(
        {ErrorCode::ParseError, "C# build manifest is not valid UTF-8"});
    JsonValue document; std::string parseError;
    JsonParser parser(utf8Json);
    if (!parser.parse(document, parseError)) return astro::core::Result<CSharpBuildManifest>::failure(
        {ErrorCode::ParseError, "Invalid C# build manifest JSON: " + parseError});
    const auto* root = std::get_if<JsonValue::Object>(&document.value);
    if (!root) return astro::core::Result<CSharpBuildManifest>::failure({ErrorCode::ParseError, "C# build manifest must be a JSON object"});
    for (const auto& [key, value] : *root)
    {
        (void)value;
        if (key != "schemaVersion" && key != "sdkVersion" && key != "targetFramework"
            && key != "assemblyName" && key != "scriptBindings")
            return astro::core::Result<CSharpBuildManifest>::failure({ErrorCode::ValidationError, "Unknown C# build manifest field: " + key});
    }
    CSharpBuildManifest manifest;
    const auto get = [&](std::string_view key) -> const JsonValue* { const auto it = root->find(key); return it == root->end() ? nullptr : &it->second; };
    const auto* schema = get("schemaVersion");
    const auto* sdk = get("sdkVersion");
    const auto* framework = get("targetFramework");
    const auto* assembly = get("assemblyName");
    if (!schema || !sdk || !framework || !assembly)
        return astro::core::Result<CSharpBuildManifest>::failure({ErrorCode::ValidationError, "C# build manifest is missing a required field"});
    const auto* schemaNumber = std::get_if<std::int64_t>(&schema->value);
    const auto* sdkText = std::get_if<std::string>(&sdk->value);
    const auto* frameworkText = std::get_if<std::string>(&framework->value);
    const auto* assemblyText = std::get_if<std::string>(&assembly->value);
    if (!schemaNumber || !sdkText || !frameworkText || !assemblyText)
        return astro::core::Result<CSharpBuildManifest>::failure({ErrorCode::ValidationError, "C# build manifest fields have incorrect JSON types"});
    manifest.schemaVersion = static_cast<int>(*schemaNumber);
    manifest.sdkVersion = *sdkText; manifest.targetFramework = *frameworkText; manifest.assemblyName = *assemblyText;
    if (const auto* bindings = get("scriptBindings"))
    {
        const auto* array = std::get_if<JsonValue::Array>(&bindings->value);
        if (!array) return astro::core::Result<CSharpBuildManifest>::failure({ErrorCode::ValidationError, "scriptBindings must be an array"});
        for (const auto& item : *array)
        {
            const auto* object = std::get_if<JsonValue::Object>(&item.value);
            if (!object || object->size() != 2) return astro::core::Result<CSharpBuildManifest>::failure(
                {ErrorCode::ValidationError, "Each scriptBindings entry must contain only sourcePath and managedTypeName"});
            const auto sourceIt = object->find("sourcePath"), typeIt = object->find("managedTypeName");
            if (sourceIt == object->end() || typeIt == object->end()) return astro::core::Result<CSharpBuildManifest>::failure(
                {ErrorCode::ValidationError, "Each script binding requires sourcePath and managedTypeName"});
            const auto* sourceText = std::get_if<std::string>(&sourceIt->second.value);
            const auto* typeText = std::get_if<std::string>(&typeIt->second.value);
            if (!sourceText || !typeText) return astro::core::Result<CSharpBuildManifest>::failure(
                {ErrorCode::ValidationError, "C# script binding fields must be strings"});
            manifest.scriptBindings.push_back({*sourceText, *typeText});
        }
    }
    auto valid = validate(manifest);
    if (!valid) return astro::core::Result<CSharpBuildManifest>::failure(valid.error());
    return astro::core::Result<CSharpBuildManifest>::success(std::move(manifest));
}

std::string CSharpBuildManifestFile::serialize(const CSharpBuildManifest& manifest)
{
    std::string output = "{\n  \"schemaVersion\": " + std::to_string(manifest.schemaVersion) + ",\n  \"sdkVersion\": ";
    appendEscaped(output, manifest.sdkVersion);
    output += ",\n  \"targetFramework\": "; appendEscaped(output, manifest.targetFramework);
    output += ",\n  \"assemblyName\": "; appendEscaped(output, manifest.assemblyName);
    output += ",\n  \"scriptBindings\": [";
    for (std::size_t i = 0; i < manifest.scriptBindings.size(); ++i)
    {
        const auto& binding = manifest.scriptBindings[i];
        output += i == 0 ? "\n    {\"sourcePath\": " : ",\n    {\"sourcePath\": ";
        appendEscaped(output, binding.sourcePath); output += ", \"managedTypeName\": "; appendEscaped(output, binding.managedTypeName);
        output += "}";
    }
    if (!manifest.scriptBindings.empty()) output += '\n';
    output += "  ]\n}\n";
    return output;
}

astro::core::Result<CSharpBuildManifest> CSharpBuildManifestFile::read(const std::filesystem::path& projectRoot)
{
    auto path = safeManifestPath(projectRoot, false);
    if (!path) return astro::core::Result<CSharpBuildManifest>::failure(path.error());
    std::error_code ec;
    const auto size = std::filesystem::file_size(path.value(), ec);
    if (ec || size > maxManifestBytes) return astro::core::Result<CSharpBuildManifest>::failure(
        {ErrorCode::IoError, ec ? "Cannot inspect C# build manifest: " + ec.message() : "C# build manifest exceeds 1 MiB"});
    std::ifstream input(path.value(), std::ios::binary);
    if (!input) return astro::core::Result<CSharpBuildManifest>::failure({ErrorCode::IoError, "Cannot read C# build manifest"});
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef
        && static_cast<unsigned char>(text[1]) == 0xbb && static_cast<unsigned char>(text[2]) == 0xbf) text.erase(0, 3);
    auto parsed = parse(text);
    if (!parsed) return parsed;
    auto sources = scriptInventory(projectRoot);
    if (!sources) return astro::core::Result<CSharpBuildManifest>::failure(sources.error());
    auto valid = validate(parsed.value(), sources.value());
    if (!valid) return astro::core::Result<CSharpBuildManifest>::failure(valid.error());
    for (const auto& binding : parsed.value().scriptBindings)
        if (std::find(sources.value().begin(), sources.value().end(), binding.sourcePath) == sources.value().end())
            return astro::core::Result<CSharpBuildManifest>::failure({ErrorCode::NotFound,
                "Binding source file does not exist under Scripts: " + binding.sourcePath});
    return parsed;
}

astro::core::Result<void> CSharpBuildManifestFile::write(const std::filesystem::path& projectRoot,
    const CSharpBuildManifest& manifest)
{
    auto sources = scriptInventory(projectRoot);
    if (!sources) return failure(sources.error().code, sources.error().message);
    auto valid = validate(manifest, sources.value());
    if (!valid) return valid;
    for (const auto& binding : manifest.scriptBindings)
        if (std::find(sources.value().begin(), sources.value().end(), binding.sourcePath) == sources.value().end())
            return failure(ErrorCode::NotFound, "Binding source file does not exist under Scripts: " + binding.sourcePath);
    std::error_code ec;
    auto path = safeManifestPath(projectRoot, true);
    if (!path) return failure(path.error().code, path.error().message);
    std::filesystem::create_directories(path.value().parent_path(), ec);
    if (ec) return failure(ErrorCode::IoError, "Cannot create project .astro directory: " + ec.message());
    const auto contents = serialize(manifest);
    static std::atomic_uint64_t sequence{};
    auto temporary = path.value();
    temporary += ".tmp-" + std::to_string(++sequence);
#ifdef _WIN32
    temporary += "-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return failure(ErrorCode::IoError, "Cannot reserve temporary C# build manifest");
    DWORD written{};
    const BOOL writeOk = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == contents.size() && FlushFileBuffers(file);
    const DWORD writeError = writeOk ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!writeOk)
    {
        DeleteFileW(temporary.c_str());
        return failure(ErrorCode::IoError, "Cannot write temporary C# build manifest (Windows error " + std::to_string(writeError) + ")");
    }
    if (!MoveFileExW(temporary.c_str(), path.value().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto error = std::error_code(static_cast<int>(GetLastError()), std::system_category()).message();
        DeleteFileW(temporary.c_str());
        return failure(ErrorCode::IoError, "Cannot publish C# build manifest: " + error);
    }
#else
    { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
      output.flush(); if (!output) { std::filesystem::remove(temporary, ec); return failure(ErrorCode::IoError, "Cannot write temporary C# build manifest"); } }
    std::filesystem::rename(temporary, path.value(), ec);
    if (ec) { std::filesystem::remove(temporary, ec); return failure(ErrorCode::IoError, "Cannot publish C# build manifest: " + ec.message()); }
#endif
    return astro::core::Result<void>::success();
}
}
