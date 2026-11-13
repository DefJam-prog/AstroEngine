#include "astro/runtime/SaveStore.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <string_view>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace
{
int failures = 0;
template <typename T>
void check(const T& condition, const char* message)
{
    if (!static_cast<bool>(condition)) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::uint32_t checksum(const std::string_view bytes)
{
    std::uint32_t crc = 0xffffffffU;
    for (const auto byte : bytes)
    {
        crc ^= static_cast<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

std::string updateChecksum(std::string bytes)
{
    const auto footer = bytes.rfind("\nCHECKSUM ");
    if (footer == std::string::npos) return {};
    const auto prefix = bytes.substr(0, footer + 1);
    std::ostringstream value;
    value << std::hex << std::nouppercase << std::setw(8) << std::setfill('0') << checksum(prefix);
    return prefix + "CHECKSUM " + value.str() + "\n";
}

astro::runtime::SaveProfile makeProfile(const astro::scene::EntityId entity)
{
    using namespace astro::runtime;
    auto profile = SaveProfile::create("puzzle-progress", 1);
    if (!profile) throw std::runtime_error(profile.error().message);
    if (!profile.value().addField(entity, std::string(SaveProperty::entityName))
        || !profile.value().addField(entity, std::string(SaveProperty::positionX))
        || !profile.value().addField(entity, std::string(SaveProperty::spriteVisible)))
        throw std::runtime_error("Could not create test SaveProfile");
    return std::move(profile).value();
}
}

int main()
{
    using namespace astro;
    using namespace astro::runtime;
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() / ("AstroEngine save integration " + std::to_string(unique));
    std::error_code fsError;
    check(std::filesystem::create_directory(directory, fsError) && !fsError, "create isolated save test directory");
    if (failures) return EXIT_FAILURE;

    auto opened = SaveStore::open(directory, 5);
    check(opened && opened.value().slotCount() == 5, "open bounded manual slot store");
    check(!SaveStore::open(directory, 0), "reject empty slot count");
    check(!SaveStore::open(directory, 101), "reject excessive slot count");
    check(!SaveStore::open(directory, 2, 17 * 1024 * 1024), "cap maximum save file size");
    if (!opened) return EXIT_FAILURE;
    auto store = std::move(opened).value();

    scene::Scene scene{903, "Puzzle Ω"};
    const auto playerResult = scene.createEntity("Игрок");
    const auto secondaryResult = scene.createEntity("Door");
    check(playerResult && secondaryResult, "create selected and non-selected scene entities");
    if (!playerResult || !secondaryResult) return EXIT_FAILURE;
    const auto player = playerResult.value();
    const auto secondary = secondaryResult.value();
    auto transform = scene.transform(player)->value;
    transform.position = {12.5, 4.0, 2.0};
    transform.rotationDegrees = {0.0, 0.0, 17.0};
    check(scene.setTransform(player, transform), "set source transform");
    check(scene.setSpriteRenderer(player, {73}, true), "add selected SpriteRenderer state");
    auto profile = makeProfile(player);
    check(!profile.addField(player, "unknown.property"), "reject unsupported fields in profile");
    check(!profile.addField(player, std::string(SaveProperty::positionX)), "reject duplicate selected fields");
    check(!SaveProfile::create("unsafe/profile", 1), "reject path-like profile IDs");

    check(!store.save(5, scene, profile, "1.2.0"), "reject slot outside configured range");
    auto saved = store.save(0, scene, profile, "1.2.0");
    check(saved && saved.value().slotInfo.propertyCount == 3, "save only three explicitly selected fields");
    check(saved && saved.value().missingEntities == 0 && saved.value().missingComponents == 0,
        "report no missing selected objects or components");
    const auto path = directory / "slot-0.astro-save";
    check(std::filesystem::exists(path), "write slot file beneath configured user-data root");
    auto inspected = store.inspect(0);
    check(inspected && inspected.value().profileId == "puzzle-progress"
        && inspected.value().gameVersion == "1.2.0" && inspected.value().propertyCount == 3
        && inspected.value().savedAtUnixSeconds > 0,
        "inspect slot metadata and preview count");
    check(!store.inspect(1), "empty slot has no metadata");

    auto reopenedResult = SaveStore::open(directory, 5);
    check(reopenedResult, "reopen SaveStore from persisted user-data directory");
    if (!reopenedResult) return EXIT_FAILURE;
    auto reopenedStore = std::move(reopenedResult).value();

    check(scene.setProperty(player, "name", scene::PropertyValue{std::string("Changed")}), "change selected entity name");
    transform = scene.transform(player)->value;
    transform.position.x = 88.0;
    transform.position.y = 99.0;
    check(scene.setTransform(player, transform), "change selected and unselected transform values");
    check(scene.setSpriteRenderer(player, {73}, false), "change selected SpriteRenderer state");
    auto loaded = reopenedStore.load(0, scene, profile, "1.2.0");
    check(loaded && loaded.value().appliedProperties == 3, "load selected fields into runtime Scene");
    check(scene.find(player)->name == "Игрок" && scene.transform(player)->value.position.x == 12.5
        && scene.spriteRenderer(player)->visible, "restore selected values exactly");
    check(scene.transform(player)->value.position.y == 99.0 && scene.find(secondary)->name == "Door",
        "preserve unselected fields and entities");
    const auto entityCount = scene.entities().size();
    const auto again = store.load(0, scene, profile, "1.2.0");
    check(again && again.value().appliedProperties == 3 && scene.entities().size() == entityCount,
        "repeated load is idempotent and does not duplicate entities");

    auto mismatchedProfile = SaveProfile::create("puzzle-progress", 1);
    check(mismatchedProfile && mismatchedProfile.value().addField(player, std::string(SaveProperty::positionZ)),
        "make profile with a different selected-field set");
    const auto beforeMismatch = scene.transform(player)->value;
    const auto profileMismatch = store.load(0, scene, mismatchedProfile.value(), "1.2.0");
    check(!profileMismatch && profileMismatch.error().code == core::ErrorCode::VersionMismatch,
        "reject changed selection set even when profile version was not incremented");
    check(!store.load(0, scene, profile, "2.0.0"), "reject incompatible game version");
    auto versionedProfile = SaveProfile::create("puzzle-progress", 2);
    check(versionedProfile && versionedProfile.value().addField(player, std::string(SaveProperty::entityName))
        && versionedProfile.value().addField(player, std::string(SaveProperty::positionX))
        && versionedProfile.value().addField(player, std::string(SaveProperty::spriteVisible)),
        "make profile with incremented schema version");
    check(!store.load(0, scene, versionedProfile.value(), "1.2.0"), "reject incompatible profile schema version");
    check(scene.transform(player)->value == beforeMismatch, "incompatible load leaves Scene untouched");

    auto secondSaveState = scene.transform(player)->value;
    secondSaveState.position.x = 55.0;
    check(scene.setTransform(player, secondSaveState), "prepare second slot revision");
    check(store.save(0, scene, profile, "1.2.0"), "replace slot and create a backup of the previous valid version");
    auto orphan = path;
    orphan += ".tmp.crashed-process";
    writeText(orphan, "ASTRO_SAVE 1\nPARTIAL");
    auto bytes = readText(path);
    if (!bytes.empty()) bytes[bytes.find("55") == std::string::npos ? 0 : bytes.find("55")] ^= 1;
    writeText(path, bytes);
    const auto backupLoad = store.load(0, scene, profile, "1.2.0");
    check(backupLoad && backupLoad.value().slotInfo.fromBackup, "recover previous valid slot after main-file corruption");
    check(scene.transform(player)->value.position.x == 12.5, "backup restores previous saved property value");
    check(store.inspect(0) && store.inspect(0).value().fromBackup, "slot inspection indicates backup recovery");

    auto missingProfile = SaveProfile::create("missing-object", 1);
    check(missingProfile && missingProfile.value().addField(secondary, std::string(SaveProperty::entityName)),
        "select a second entity for missing-object test");
    check(store.save(1, scene, missingProfile.value(), "1.2.0"), "save selected secondary object");
    scene::Scene sceneWithoutSecondary{903, "Puzzle Ω"};
    check(sceneWithoutSecondary.createEntity("Other"), "create replacement scene without saved entity");
    auto missingLoad = store.load(1, sceneWithoutSecondary, missingProfile.value(), "1.2.0");
    check(missingLoad && missingLoad.value().missingEntities == 1 && missingLoad.value().appliedProperties == 0,
        "report and skip an entity absent from the current scene");

    auto spriteProfile = SaveProfile::create("sprite-state", 1);
    check(spriteProfile && spriteProfile.value().addField(player, std::string(SaveProperty::spriteVisible)),
        "select SpriteRenderer visibility");
    check(store.save(2, scene, spriteProfile.value(), "1.2.0"), "write SpriteRenderer-only profile");
    check(scene.removeSpriteRenderer(player), "remove component after saving its selected state");
    auto missingComponentLoad = store.load(2, scene, spriteProfile.value(), "1.2.0");
    check(missingComponentLoad && missingComponentLoad.value().missingComponents == 1,
        "report selected component missing during load");

    check(store.save(3, scene, profile, "1.2.0"), "write standalone corruption test slot");
    const auto corruptPath = directory / "slot-3.astro-save";
    auto corrupt = readText(corruptPath);
    if (corrupt.size() > 20) corrupt[10] ^= 1;
    writeText(corruptPath, corrupt);
    const auto beforeCorruptLoad = scene.find(player)->name;
    const auto corruptLoad = store.load(3, scene, profile, "1.2.0");
    check(!corruptLoad, "reject damaged save with no valid backup");
    check(scene.find(player)->name == beforeCorruptLoad, "corrupt load makes no partial changes");

    check(store.save(4, scene, profile, "1.2.0"), "write version-validation slot");
    transform = scene.transform(player)->value;
    transform.position.x = 44.0;
    check(scene.setTransform(player, transform), "mutate state between version-validation saves");
    check(store.save(4, scene, profile, "1.2.0"), "create valid backup before future-version check");
    auto versionBytes = readText(directory / "slot-4.astro-save");
    const auto header = versionBytes.find("ASTRO_SAVE 1\n");
    if (header != std::string::npos) versionBytes.replace(header, std::string("ASTRO_SAVE 1\n").size(), "ASTRO_SAVE 99\n");
    versionBytes = updateChecksum(std::move(versionBytes));
    writeText(directory / "slot-4.astro-save", versionBytes);
    const auto beforeUnsupportedVersion = scene.transform(player)->value;
    const auto wrongVersion = store.load(4, scene, profile, "1.2.0");
    check(!wrongVersion && wrongVersion.error().code == core::ErrorCode::VersionMismatch,
        "diagnose an unsupported format version instead of falling back to an older backup");
    const auto unsupportedInspection = store.inspect(4);
    check(!unsupportedInspection && unsupportedInspection.error().code == core::ErrorCode::VersionMismatch,
        "slot inspection also reports the unsupported primary version instead of hiding it with backup metadata");
    check(scene.transform(player)->value == beforeUnsupportedVersion,
        "unsupported version with available backup leaves Scene unchanged");

#ifdef _WIN32
    const auto lockedPath = directory / "slot-1.astro-save";
    const HANDLE lock = CreateFileW(lockedPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(lock != INVALID_HANDLE_VALUE, "open existing slot without delete sharing to simulate interrupted publish");
    check(scene.setProperty(secondary, "name", scene::PropertyValue{std::string("Changed door")}),
        "change state before blocked replacement");
    const auto failedReplace = store.save(1, scene, missingProfile.value(), "1.2.0");
    check(!failedReplace, "report failure when replacement is blocked by a reader");
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    auto afterFailedWrite = store.load(1, scene, missingProfile.value(), "1.2.0");
    check(afterFailedWrite && scene.find(secondary)->name == "Door",
        "failed replacement leaves last committed save loadable");
#endif

    std::filesystem::remove_all(directory, fsError);
    check(!fsError, "remove isolated temporary test files");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
