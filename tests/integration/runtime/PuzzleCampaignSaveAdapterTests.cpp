#include "astro/runtime/PuzzleCampaignSaveAdapter.hpp"
#include "astro/runtime/PuzzleLevel.hpp"
#include "PuzzleCampaignSaveAdapterTestHooks.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
int failures = 0;
void check(const bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

bool sameSnapshot(const astro::runtime::PuzzleSnapshot& lhs, const astro::runtime::PuzzleSnapshot& rhs)
{
    return lhs.width == rhs.width && lhs.height == rhs.height && lhs.tileKinds == rhs.tileKinds
        && lhs.levelTarget == rhs.levelTarget && lhs.score == rhs.score && lhs.level == rhs.level
        && lhs.tiles == rhs.tiles && lhs.randomState == rhs.randomState;
}

bool playAcceptedMove(astro::runtime::PuzzleCampaignSession& campaign)
{
    const auto& current = campaign.game();
    for (std::size_t row = 0; row < current.height(); ++row)
        for (std::size_t column = 0; column < current.width(); ++column)
            for (const auto offset : {astro::runtime::GridPosition{1, 0}, astro::runtime::GridPosition{0, 1}})
            {
                const astro::runtime::GridPosition next{column + offset.column, row + offset.row};
                if (next.column >= current.width() || next.row >= current.height()) continue;
                auto candidate = current;
                const auto result = candidate.swap({column, row}, next);
                if (!result || !result.value().accepted) continue;
                const auto applied = campaign.swap({column, row}, next);
                return applied && applied.value().accepted;
            }
    return false;
}

bool reachTarget(astro::runtime::PuzzleCampaignSession& campaign)
{
    for (int move = 0; move < 100 && !campaign.completionPending(); ++move)
        if (!playAcceptedMove(campaign)) return false;
    return campaign.completionPending();
}

std::string readBytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

int interruptCopyChild(const std::filesystem::path& source, const std::filesystem::path& target,
    const std::string_view campaignId)
{
    const auto preview = astro::runtime::previewPuzzleCampaignSaveTransfer(source, target, campaignId);
    if (!preview) return 91;
    const auto copied = astro::runtime::copyPuzzleCampaignSaveTransfer(preview.value(), true,
        [](const std::size_t completed, const std::size_t) {
            if (completed == 1) std::_Exit(77);
            return true;
        });
    return copied ? 92 : 93;
}

using ArchiveBoundary = astro::runtime::testing::CampaignArchiveBoundary;

std::string archiveBoundaryName(const ArchiveBoundary boundary)
{
    switch (boundary)
    {
    case ArchiveBoundary::PartialWrite: return "partial-write";
    case ArchiveBoundary::VerifiedBeforePublish: return "verified-before-publish";
    case ArchiveBoundary::PublishedBeforeStagingCleanup: return "published-before-cleanup";
    }
    return "invalid";
}

bool parseArchiveBoundary(const std::string_view name, ArchiveBoundary& boundary)
{
    if (name == "partial-write") boundary = ArchiveBoundary::PartialWrite;
    else if (name == "verified-before-publish") boundary = ArchiveBoundary::VerifiedBeforePublish;
    else if (name == "published-before-cleanup") boundary = ArchiveBoundary::PublishedBeforeStagingCleanup;
    else return false;
    return true;
}

int interruptArchiveChild(const std::filesystem::path& source, const std::filesystem::path& target,
    const std::string_view campaignId, const std::string_view boundaryName,
    const std::filesystem::path& markerPath)
{
    ArchiveBoundary wanted{};
    if (!parseArchiveBoundary(boundaryName, wanted)) return 90;
    const auto preview = astro::runtime::previewPuzzleCampaignSaveTransfer(source, target, campaignId);
    if (!preview || !preview.value().pendingRecovery) return 91;
    const auto archived = astro::runtime::testing::archiveMalformedCampaignJournalWithHook(
        *preview.value().pendingRecovery, [wanted, markerPath](const ArchiveBoundary reached) {
            if (reached != wanted) return;
            { std::ofstream marker(markerPath, std::ios::binary | std::ios::trunc);
                marker << "paused"; marker.flush(); }
            while (true) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    return archived ? 92 : 93;
}

int interruptArchiveConflictCopyChild(const std::filesystem::path& source,
    const std::filesystem::path& target, const std::string_view campaignId,
    const std::string_view boundaryName, const std::filesystem::path& markerPath)
{
    ArchiveBoundary wanted{};
    if (!parseArchiveBoundary(boundaryName, wanted)) return 90;
    const auto transfer = astro::runtime::previewPuzzleCampaignSaveTransfer(source, target, campaignId);
    if (!transfer || !transfer.value().pendingRecovery) return 91;
    const auto inspection = astro::runtime::inspectMalformedPuzzleCampaignSaveArchive(
        *transfer.value().pendingRecovery);
    if (!inspection) return 92;
    const auto copyPreview = astro::runtime::previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
        *transfer.value().pendingRecovery, inspection.value());
    if (!copyPreview) return 93;
    const auto copied = astro::runtime::testing::preserveMalformedCampaignArchiveConflictWithHook(
        *transfer.value().pendingRecovery, copyPreview.value(), [wanted, markerPath,
            destination = copyPreview.value().destinationPath.filename().string()](const ArchiveBoundary reached) {
            if (reached != wanted) return;
            { std::ofstream marker(markerPath, std::ios::binary | std::ios::trunc);
                marker << destination; marker.flush(); }
            while (true) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        });
    return copied ? 94 : 95;
}

std::filesystem::path campaignArchivePath(const std::filesystem::path& target,
    const std::uint64_t fingerprint)
{
    std::ostringstream name;
    name << ".astro-campaign-transfer.invalid-" << std::hex << fingerprint << ".archive";
    return target / name.str();
}

std::vector<std::filesystem::path> campaignArchiveStagingPaths(const std::filesystem::path& archivePath)
{
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    const auto prefix = archivePath.filename().string() + ".tmp-";
    for (std::filesystem::directory_iterator iterator(archivePath.parent_path(), error), end;
        iterator != end && !error; iterator.increment(error))
        if (iterator->path().filename().string().starts_with(prefix)) paths.push_back(iterator->path());
    std::sort(paths.begin(), paths.end());
    return paths;
}

#ifdef _WIN32
std::wstring quoteWindowsArgument(const std::wstring& value)
{
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const auto ch : value)
    {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') { result.append(slashes * 2 + 1, L'\\'); result.push_back(ch); slashes = 0; continue; }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

int launchInterruptedCopyChild(const std::filesystem::path& source, const std::filesystem::path& target,
    const std::string_view campaignId)
{
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return -1;
    executable.resize(length);
    std::wstring command = quoteWindowsArgument(executable) + L" --interrupt-copy "
        + quoteWindowsArgument(source.wstring()) + L" " + quoteWindowsArgument(target.wstring()) + L" ";
    for (const auto ch : campaignId) command.push_back(static_cast<wchar_t>(ch));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process)) return -1;
    CloseHandle(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, 10000);
    DWORD exitCode = 0;
    const bool exited = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE;
    CloseHandle(process.hProcess);
    return exited ? static_cast<int>(exitCode) : -1;
}

bool launchInterruptedArchiveChild(const std::filesystem::path& source, const std::filesystem::path& target,
    const std::string_view campaignId, const ArchiveBoundary boundary,
    const std::filesystem::path& markerPath)
{
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return false;
    executable.resize(length);
    std::wstring id;
    for (const auto ch : campaignId) id.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    const auto boundaryAscii = archiveBoundaryName(boundary);
    const std::wstring boundaryWide(boundaryAscii.begin(), boundaryAscii.end());
    std::wstring command = quoteWindowsArgument(executable) + L" --interrupt-archive "
        + quoteWindowsArgument(source.wstring()) + L" " + quoteWindowsArgument(target.wstring()) + L" "
        + quoteWindowsArgument(id) + L" "
        + quoteWindowsArgument(boundaryWide)
        + L" " + quoteWindowsArgument(markerPath.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    bool reached = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::error_code error;
        if (std::filesystem::exists(markerPath, error) && !error) { reached = true; break; }
        if (WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0) break;
    }
    const bool terminated = TerminateProcess(process.hProcess, 86) != FALSE;
    const bool exited = WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0;
    DWORD exitCode = 0;
    const bool correctExit = exited && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE && exitCode == 86;
    CloseHandle(process.hProcess);
    return reached && terminated && correctExit;
}

bool launchInterruptedArchiveConflictCopyChild(const std::filesystem::path& source,
    const std::filesystem::path& target, const std::string_view campaignId,
    const ArchiveBoundary boundary, const std::filesystem::path& markerPath)
{
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) return false;
    executable.resize(length);
    std::wstring id;
    for (const auto ch : campaignId) id.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
    const auto boundaryAscii = archiveBoundaryName(boundary);
    const std::wstring boundaryWide(boundaryAscii.begin(), boundaryAscii.end());
    std::wstring command = quoteWindowsArgument(executable) + L" --interrupt-conflict-copy "
        + quoteWindowsArgument(source.wstring()) + L" " + quoteWindowsArgument(target.wstring()) + L" "
        + quoteWindowsArgument(id) + L" " + quoteWindowsArgument(boundaryWide) + L" "
        + quoteWindowsArgument(markerPath.wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    bool reached = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::error_code error;
        if (std::filesystem::exists(markerPath, error) && !error) { reached = true; break; }
        if (WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0) break;
    }
    const bool terminated = TerminateProcess(process.hProcess, 86) != FALSE;
    const bool exited = WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0;
    DWORD exitCode = 0;
    const bool correctExit = exited && GetExitCodeProcess(process.hProcess, &exitCode) != FALSE && exitCode == 86;
    CloseHandle(process.hProcess);
    return reached && terminated && correctExit;
}
#else
int launchInterruptedCopyChild(const std::filesystem::path& source, const std::filesystem::path& target,
    const std::string_view campaignId)
{
    const auto child = ::fork();
    if (child < 0) return -1;
    if (child == 0)
    {
        const auto executable = std::filesystem::canonical("/proc/self/exe").string();
        const auto sourceText = source.string(), targetText = target.string(), idText = std::string(campaignId);
        ::execl(executable.c_str(), executable.c_str(), "--interrupt-copy", sourceText.c_str(),
            targetText.c_str(), idText.c_str(), static_cast<char*>(nullptr));
        std::_Exit(94);
    }
    int status = 0;
    if (::waitpid(child, &status, 0) != child || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

bool launchInterruptedArchiveChild(const std::filesystem::path& source, const std::filesystem::path& target,
    const std::string_view campaignId, const ArchiveBoundary boundary,
    const std::filesystem::path& markerPath)
{
    const auto child = ::fork();
    if (child < 0) return false;
    if (child == 0)
    {
        const auto executable = std::filesystem::canonical("/proc/self/exe").string();
        const auto sourceText = source.string(), targetText = target.string();
        const auto idText = std::string(campaignId), boundaryText = archiveBoundaryName(boundary);
        const auto markerText = markerPath.string();
        ::execl(executable.c_str(), executable.c_str(), "--interrupt-archive", sourceText.c_str(),
            targetText.c_str(), idText.c_str(), boundaryText.c_str(), markerText.c_str(),
            static_cast<char*>(nullptr));
        std::_Exit(94);
    }
    bool reached = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::error_code error;
        if (std::filesystem::exists(markerPath, error) && !error) { reached = true; break; }
        int status = 0;
        const auto waited = ::waitpid(child, &status, WNOHANG);
        if (waited == child) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool terminated = ::kill(child, SIGKILL) == 0;
    int status = 0;
    const bool exited = ::waitpid(child, &status, 0) == child && WIFSIGNALED(status)
        && WTERMSIG(status) == SIGKILL;
    return reached && terminated && exited;
}

bool launchInterruptedArchiveConflictCopyChild(const std::filesystem::path& source,
    const std::filesystem::path& target, const std::string_view campaignId,
    const ArchiveBoundary boundary, const std::filesystem::path& markerPath)
{
    const auto child = ::fork();
    if (child < 0) return false;
    if (child == 0)
    {
        const auto executable = std::filesystem::canonical("/proc/self/exe").string();
        const auto sourceText = source.string(), targetText = target.string();
        const auto idText = std::string(campaignId), boundaryText = archiveBoundaryName(boundary);
        const auto markerText = markerPath.string();
        ::execl(executable.c_str(), executable.c_str(), "--interrupt-conflict-copy", sourceText.c_str(),
            targetText.c_str(), idText.c_str(), boundaryText.c_str(), markerText.c_str(),
            static_cast<char*>(nullptr));
        std::_Exit(94);
    }
    bool reached = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::error_code error;
        if (std::filesystem::exists(markerPath, error) && !error) { reached = true; break; }
        int status = 0;
        if (::waitpid(child, &status, WNOHANG) == child) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const bool terminated = ::kill(child, SIGKILL) == 0;
    int status = 0;
    const bool exited = ::waitpid(child, &status, 0) == child && WIFSIGNALED(status)
        && WTERMSIG(status) == SIGKILL;
    return reached && terminated && exited;
}

struct JournalLockChild final
{
    pid_t process = -1;
    int releaseDescriptor = -1;
    bool ready = false;
};

JournalLockChild startJournalLockChild(const std::filesystem::path& journalPath)
{
    int readyPipe[2]{-1, -1}, releasePipe[2]{-1, -1};
    if (::pipe(readyPipe) != 0 || ::pipe(releasePipe) != 0) return {};
    const auto child = ::fork();
    if (child < 0) return {};
    if (child == 0)
    {
        ::close(readyPipe[0]);
        ::close(releasePipe[1]);
        const auto path = journalPath.string();
        const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        const char acquired = descriptor >= 0 && ::flock(descriptor, LOCK_EX | LOCK_NB) == 0 ? '1' : '0';
        if (::write(readyPipe[1], &acquired, 1) != 1) std::_Exit(97);
        char release = 0;
        if (acquired == '1' && ::read(releasePipe[0], &release, 1) != 1) std::_Exit(98);
        if (descriptor >= 0) ::close(descriptor);
        ::close(readyPipe[1]);
        ::close(releasePipe[0]);
        std::_Exit(acquired == '1' && release == 'x' ? 0 : 96);
    }
    ::close(readyPipe[1]);
    ::close(releasePipe[0]);
    pollfd ready{readyPipe[0], POLLIN, 0};
    const bool signalled = ::poll(&ready, 1, 3000) == 1 && (ready.revents & POLLIN) != 0;
    char acquired = 0;
    const bool locked = signalled && ::read(readyPipe[0], &acquired, 1) == 1 && acquired == '1';
    ::close(readyPipe[0]);
    return {child, releasePipe[1], locked};
}

bool releaseJournalLockChild(JournalLockChild& child)
{
    if (child.process < 0 || child.releaseDescriptor < 0) return false;
    const char release = 'x';
    const bool wrote = ::write(child.releaseDescriptor, &release, 1) == 1;
    ::close(child.releaseDescriptor);
    child.releaseDescriptor = -1;
    int status = 0;
    const bool exited = ::waitpid(child.process, &status, 0) == child.process && WIFEXITED(status)
        && WEXITSTATUS(status) == 0;
    child.process = -1;
    return wrote && exited;
}
#endif
}

#ifdef _WIN32
int wmain(const int argc, wchar_t** argv)
#else
int main(const int argc, char** argv)
#endif
{
    using namespace astro::runtime;
#ifdef _WIN32
    if (argc == 5 && std::wstring_view(argv[1]) == L"--interrupt-copy")
    {
        const std::filesystem::path source(argv[2]), target(argv[3]);
        const std::wstring id(argv[4]);
        std::string campaignId;
        campaignId.reserve(id.size());
        for (const auto ch : id)
        {
            if (ch > 0x7f) return 95;
            campaignId.push_back(static_cast<char>(ch));
        }
        return interruptCopyChild(source, target, campaignId);
    }
    if (argc == 7 && std::wstring_view(argv[1]) == L"--interrupt-archive")
    {
        std::string id, boundary;
        for (const auto ch : std::wstring_view(argv[4]))
        {
            if (ch > 0x7f) return 95;
            id.push_back(static_cast<char>(ch));
        }
        for (const auto ch : std::wstring_view(argv[5]))
        {
            if (ch > 0x7f) return 95;
            boundary.push_back(static_cast<char>(ch));
        }
        return interruptArchiveChild(argv[2], argv[3], id, boundary, argv[6]);
    }
    if (argc == 7 && std::wstring_view(argv[1]) == L"--interrupt-conflict-copy")
    {
        std::string id, boundary;
        for (const auto ch : std::wstring_view(argv[4]))
        {
            if (ch > 0x7f) return 95;
            id.push_back(static_cast<char>(ch));
        }
        for (const auto ch : std::wstring_view(argv[5]))
        {
            if (ch > 0x7f) return 95;
            boundary.push_back(static_cast<char>(ch));
        }
        return interruptArchiveConflictCopyChild(argv[2], argv[3], id, boundary, argv[6]);
    }
#else
    if (argc == 5 && std::string_view(argv[1]) == "--interrupt-copy")
        return interruptCopyChild(argv[2], argv[3], argv[4]);
    if (argc == 7 && std::string_view(argv[1]) == "--interrupt-archive")
        return interruptArchiveChild(argv[2], argv[3], argv[4], argv[5], argv[6]);
    if (argc == 7 && std::string_view(argv[1]) == "--interrupt-conflict-copy")
        return interruptArchiveConflictCopyChild(argv[2], argv[3], argv[4], argv[5], argv[6]);
#endif
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporaryRoot = std::filesystem::temp_directory_path()
        / (std::filesystem::path(L"astro-campaign-save-test-Сохранения-")
            / std::filesystem::path(std::to_wstring(unique)));
    std::error_code filesystemError;
    std::filesystem::create_directories(temporaryRoot, filesystemError);
    check(!filesystemError, "create isolated Unicode project root");
    if (filesystemError) return EXIT_FAILURE;

    const std::vector<std::uint8_t> playableBoard{0,1,0, 2,0,1, 2,1,2};
    PuzzleLevelDefinition intro{"intro", "Introduction", 3, 3, 3, 42, 300, playableBoard};
    PuzzleLevelDefinition finale{"finale", "Finale", 3, 3, 3, 42, 10, playableBoard};
    check(static_cast<bool>(PuzzleLevelFile::write(temporaryRoot, intro)), "write authored intro level");
    check(static_cast<bool>(PuzzleLevelFile::write(temporaryRoot, finale)), "write authored finale level");
    const PuzzleCampaignDefinition campaignDefinition{
        "save-story", "Save Story", "intro", {"intro", "finale"}, {
            {"intro", PuzzleCampaignRuleKind::ScoreAtLeast, 30, "finale"},
            {"intro", PuzzleCampaignRuleKind::Default, 0, "finale"},
            {"finale", PuzzleCampaignRuleKind::Default, 0, std::nullopt},
        }};
    check(static_cast<bool>(PuzzleCampaignFile::write(temporaryRoot, campaignDefinition)),
        "write authored campaign with DEFAULT and END transitions");

    astro::scene::Scene scene(7001, "Campaign save scene");
    auto playerResult = scene.createEntity("Saved player");
    check(static_cast<bool>(playerResult), "create test entity");
    if (!playerResult) { std::filesystem::remove_all(temporaryRoot, filesystemError); return EXIT_FAILURE; }
    const auto playerId = playerResult.value();

    auto storeResult = SaveStore::open(temporaryRoot / "User Data" / "Campaign Slots", 10);
    auto profileResult = SaveProfile::create("puzzle-campaign-progress", 1);
    check(storeResult && profileResult, "create SaveStore and versioned progress profile");
    if (!storeResult || !profileResult)
    {
        std::filesystem::remove_all(temporaryRoot, filesystemError);
        return EXIT_FAILURE;
    }
    auto store = std::move(storeResult).value();
    const auto missingReadOnlyDirectory = temporaryRoot / "Must Not Be Created";
    const auto missingInspection = SaveStore::inspectExistingSlot(missingReadOnlyDirectory, 0);
    check(!missingInspection && !std::filesystem::exists(missingReadOnlyDirectory),
        "read-only slot inspection does not create a missing directory");
    auto profile = std::move(profileResult).value();
    check(static_cast<bool>(selectPuzzleCampaignProgressFields(profile)),
        "explicitly select campaign ID, level ID, completion state and Puzzle snapshot");
    check(static_cast<bool>(profile.addField(playerId, std::string(SaveProperty::entityName))),
        "select only the requested Scene property in addition to campaign fields");

    auto sessionResult = PuzzleCampaignSession::start(temporaryRoot, campaignDefinition.id);
    check(static_cast<bool>(sessionResult), "start authored campaign from project files");
    if (!sessionResult)
    {
        std::filesystem::remove_all(temporaryRoot, filesystemError);
        return EXIT_FAILURE;
    }
    auto campaignSession = std::move(sessionResult).value();
    const auto firstMove = campaignSession.swap({1, 0}, {1, 1});
    check(firstMove && firstMove.value().accepted && !campaignSession.completionPending(),
        "make progress that has not yet reached the authored level target");
    const auto savedSnapshot = campaignSession.game().snapshot();
    const auto savedMidLevel = savePuzzleCampaignProgress(store, 0, scene, profile,
        "1.0.0", campaignSession);
    check(savedMidLevel && savedMidLevel.value().slotInfo.propertyCount == 5,
        "save the four explicitly selected campaign fields and one explicit Scene property");

    check(static_cast<bool>(scene.setProperty(playerId, "name",
        astro::scene::PropertyValue{std::string("Unsaved name")})), "mutate Scene before restore");
    const auto loadedMidLevel = loadPuzzleCampaignProgress(store, 0, scene, profile,
        "1.0.0", temporaryRoot, campaignDefinition.id);
    check(loadedMidLevel && loadedMidLevel.value().campaign.currentLevel().id == "intro"
        && !loadedMidLevel.value().campaign.completionPending()
        && sameSnapshot(loadedMidLevel.value().campaign.game().snapshot(), savedSnapshot),
        "round-trip active campaign level, score, board and deterministic RNG state");
    check(loadedMidLevel && scene.find(playerId)->name == "Saved player"
        && loadedMidLevel.value().additionalCustomState.empty(),
        "publish the selected Scene field only after campaign and snapshot validation");
    if (!loadedMidLevel)
    {
        std::filesystem::remove_all(temporaryRoot, filesystemError);
        return EXIT_FAILURE;
    }
    campaignSession = std::move(loadedMidLevel.value().campaign);

    check(static_cast<bool>(scene.setProperty(playerId, "name",
        astro::scene::PropertyValue{std::string("Must survive wrong campaign")})),
        "set Scene marker before campaign mismatch");
    const auto mismatchedCampaign = loadPuzzleCampaignProgress(store, 0, scene, profile,
        "1.0.0", temporaryRoot, "other-story");
    check(!mismatchedCampaign && mismatchedCampaign.error().code == astro::core::ErrorCode::VersionMismatch
        && scene.find(playerId)->name == "Must survive wrong campaign",
        "reject a slot for another campaign without mutating Scene state");

    auto versionTwoResult = SaveProfile::create("puzzle-campaign-progress", 2);
    check(static_cast<bool>(versionTwoResult), "create incompatible profile schema");
    if (versionTwoResult)
    {
        auto versionTwo = std::move(versionTwoResult).value();
        check(static_cast<bool>(selectPuzzleCampaignProgressFields(versionTwo)),
            "select same fields under a newer profile schema");
        check(static_cast<bool>(versionTwo.addField(playerId, std::string(SaveProperty::entityName))),
            "select compatible Scene field under a newer profile schema");
        const auto rejectedVersion = loadPuzzleCampaignProgress(store, 0, scene, versionTwo,
            "1.0.0", temporaryRoot, campaignDefinition.id);
        check(!rejectedVersion && rejectedVersion.error().code == astro::core::ErrorCode::VersionMismatch
            && scene.find(playerId)->name == "Must survive wrong campaign",
            "reject profile schema mismatch before publishing loaded Scene data");
    }

    check(reachTarget(campaignSession), "advance restored intro session to its authored score target");
    const auto pendingSave = savePuzzleCampaignProgress(store, 1, scene, profile,
        "1.0.0", campaignSession);
    check(static_cast<bool>(pendingSave), "save a level-complete state before the user chooses Continue");
    auto pendingRestore = loadPuzzleCampaignProgress(store, 1, scene, profile,
        "1.0.0", temporaryRoot, campaignDefinition.id);
    check(pendingRestore && pendingRestore.value().campaign.completionPending()
        && pendingRestore.value().campaign.currentLevel().id == "intro",
        "restore the explicit pending Continue state from the saved target score");
    if (pendingRestore) campaignSession = std::move(pendingRestore.value().campaign);

    const auto nextLevel = campaignSession.completeCurrentLevel();
    check(nextLevel && nextLevel.value() == std::optional<std::string>("finale")
        && campaignSession.currentLevel().id == "finale", "continue after restoring target-complete level");
    check(reachTarget(campaignSession), "reach the authored final level target");
    const auto finalScore = campaignSession.game().score();
    const auto ended = campaignSession.completeCurrentLevel();
    check(ended && !ended.value() && campaignSession.state() == PuzzleCampaignSessionState::Ended,
        "follow terminal END in the authored campaign");
    const auto endedSave = savePuzzleCampaignProgress(store, 2, scene, profile,
        "1.0.0", campaignSession);
    check(static_cast<bool>(endedSave), "save completed END result");
    auto endedRestore = loadPuzzleCampaignProgress(store, 2, scene, profile,
        "1.0.0", temporaryRoot, campaignDefinition.id);
    check(endedRestore && endedRestore.value().campaign.state() == PuzzleCampaignSessionState::Ended
        && endedRestore.value().campaign.currentLevel().id == "finale"
        && endedRestore.value().campaign.game().score() == finalScore,
        "round-trip completed campaign END and retain final board score");

    const auto slotZeroRefresh = savePuzzleCampaignProgress(store, 0, scene, profile,
        "1.0.0", campaignSession);
    check(static_cast<bool>(slotZeroRefresh), "create a valid backup for read-only transfer preview");
    const auto primarySlotZero = store.directory() / "slot-0.astro-save";
    const auto backupSlotZero = store.directory() / "slot-0.astro-save.bak";
    {
        std::ofstream corrupt(primarySlotZero, std::ios::binary | std::ios::trunc);
        corrupt << "damaged primary slot";
    }
    {
        std::ofstream corrupt(store.directory() / "slot-8.astro-save", std::ios::binary | std::ios::trunc);
        corrupt << "not an Astro save";
        std::ofstream unsupported(store.directory() / "slot-7.astro-save", std::ios::binary | std::ios::trunc);
        unsupported << "ASTRO_SAVE 999\n";
        std::ofstream temporary(store.directory() / "slot-4.astro-save.tmp.fixture", std::ios::binary);
        temporary << "partial";
        std::ofstream unexpected(store.directory() / "notes.txt", std::ios::binary);
        unexpected << "not a slot";
    }
    const auto previewTarget = temporaryRoot / "Другая копия" / "Campaign Slots";
    const auto sourcePrimaryBeforePreview = readBytes(primarySlotZero);
    const auto sourceBackupBeforePreview = readBytes(backupSlotZero);
    const auto preview = previewPuzzleCampaignSaveTransfer(store.directory(), previewTarget, campaignDefinition.id);
    if (!preview) std::cerr << "Preview diagnostic: " << preview.error().message << '\n';
    check(preview && preview.value().slots.size() == 10 && !preview.value().targetDirectoryExists
        && !std::filesystem::exists(previewTarget),
        "read-only preview covers ten slots and does not create its target directory");
    if (preview)
    {
        const auto& slots = preview.value().slots;
        check(slots[0].state == PuzzleCampaignSaveSlotPreviewState::RecoveredFromBackup
            && slots[0].savedLevelId == "intro" && slots[0].sourceBytes > 0,
            "preview distinguishes a backup-recovered campaign slot");
        check(slots[1].state == PuzzleCampaignSaveSlotPreviewState::Valid
            && slots[2].state == PuzzleCampaignSaveSlotPreviewState::Valid,
            "preview validates ordinary campaign slots including END");
        check(slots[3].state == PuzzleCampaignSaveSlotPreviewState::Empty,
            "preview identifies an empty slot");
        check(slots[7].state == PuzzleCampaignSaveSlotPreviewState::Invalid
            && slots[8].state == PuzzleCampaignSaveSlotPreviewState::Invalid,
            "preview reports unsupported and corrupt slots");
        check(preview.value().recoveredSlots == 1 && preview.value().validSlots == 2
            && preview.value().invalidSlots == 2 && preview.value().sourceBytes > 0,
            "preview summarizes valid, recovered, invalid slots and source bytes");
        check(preview.value().unexpectedSourceArtifacts.size() == 2,
            "preview reports temporary and unrelated source artifacts");
        const auto blockedSource = revalidatePuzzleCampaignSaveTransferPreview(preview.value());
        check(blockedSource && blockedSource.value().state
            == PuzzleCampaignSaveTransferPreflightState::SourceNotTransferable,
            "invalid slots and unexpected artifacts block transfer preflight");
    }
    check(readBytes(primarySlotZero) == sourcePrimaryBeforePreview
        && readBytes(backupSlotZero) == sourceBackupBeforePreview,
        "preview leaves primary and backup source bytes unchanged");

    const auto mismatchPreview = previewPuzzleCampaignSaveTransfer(store.directory(), previewTarget, "other-story");
    check(mismatchPreview && mismatchPreview.value().slots[0].state
        == PuzzleCampaignSaveSlotPreviewState::CampaignMismatch,
        "preview identifies a different campaign ID without rewriting payload identity");
    const auto traversalPreview = previewPuzzleCampaignSaveTransfer(store.directory(),
        temporaryRoot / ".." / "outside", campaignDefinition.id);
    check(!traversalPreview, "preview rejects parent-directory traversal in a target path");
    const auto linkedSource = temporaryRoot / "linked campaign save source";
    std::filesystem::create_directory_symlink(store.directory(), linkedSource, filesystemError);
    if (!filesystemError)
    {
        const auto linkedPreview = previewPuzzleCampaignSaveTransfer(linkedSource, previewTarget,
            campaignDefinition.id);
        check(!linkedPreview, "preview rejects a symlinked source directory");
        std::filesystem::remove(linkedSource, filesystemError);
        check(!filesystemError, "remove only the isolated symlink fixture");
    }
    else
    {
        std::cerr << "SKIP: directory symlink creation is unavailable in this test environment\n";
        filesystemError.clear();
    }
    const auto unsafeIdPreview = previewPuzzleCampaignSaveTransfer(store.directory(), previewTarget, "../other");
    check(!unsafeIdPreview, "preview rejects an unsafe campaign ID");

    std::filesystem::create_directories(previewTarget, filesystemError);
    check(!filesystemError, "create isolated preview target fixture");
    {
        std::ofstream conflict(previewTarget / "slot-2.astro-save", std::ios::binary);
        conflict << "preexisting target";
    }
    const auto conflictPreview = previewPuzzleCampaignSaveTransfer(store.directory(), previewTarget,
        campaignDefinition.id);
    check(conflictPreview && conflictPreview.value().targetDirectoryExists
        && conflictPreview.value().conflictingSlots == 1
        && conflictPreview.value().slots[2].primaryConflict,
        "preview identifies destination slot conflicts without replacing them");
    check(readBytes(primarySlotZero) == sourcePrimaryBeforePreview
        && readBytes(backupSlotZero) == sourceBackupBeforePreview,
        "repeated preview and target collision inspection preserve source bytes");

    auto isolatedTransferStoreResult = SaveStore::open(temporaryRoot / "Revalidation Source", 10);
    check(static_cast<bool>(isolatedTransferStoreResult), "create isolated preflight source store");
    if (isolatedTransferStoreResult)
    {
        auto isolatedTransferStore = std::move(isolatedTransferStoreResult).value();
        check(static_cast<bool>(savePuzzleCampaignProgress(isolatedTransferStore, 0, scene, profile,
            "1.0.0", campaignSession)), "write first isolated preflight source slot");
        check(static_cast<bool>(scene.setProperty(playerId, "name",
            astro::scene::PropertyValue{std::string("Changed name")})),
            "set an equal-length source change before the second save");
        check(static_cast<bool>(savePuzzleCampaignProgress(isolatedTransferStore, 0, scene, profile,
            "1.0.0", campaignSession)), "create isolated source backup before preview");
        const auto isolatedTarget = temporaryRoot / "Revalidation Target";
        const auto shownPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
            isolatedTarget, campaignDefinition.id);
        check(shownPreview && shownPreview.value().slots[0].state == PuzzleCampaignSaveSlotPreviewState::Valid
            && shownPreview.value().slots[0].sourcePrimaryExists && shownPreview.value().slots[0].sourceBackupExists
            && !shownPreview.value().targetDirectoryExists,
            "create a clean preview with primary and backup source files");
        if (shownPreview)
        {
            const auto sourcePrimary = isolatedTransferStore.directory() / "slot-0.astro-save";
            const auto sourceBackup = isolatedTransferStore.directory() / "slot-0.astro-save.bak";
            const auto primaryBeforeRevalidation = readBytes(sourcePrimary);
            const auto backupBeforeRevalidation = readBytes(sourceBackup);
            const auto ready = revalidatePuzzleCampaignSaveTransferPreview(shownPreview.value());
            check(ready && ready.value().state == PuzzleCampaignSaveTransferPreflightState::Ready
                && !std::filesystem::exists(isolatedTarget),
                "unchanged preview is ready without creating a destination");
            check(readBytes(sourcePrimary) == primaryBeforeRevalidation
                && readBytes(sourceBackup) == backupBeforeRevalidation,
                "successful read-only revalidation preserves source bytes");

            check(static_cast<bool>(scene.setProperty(playerId, "name",
                astro::scene::PropertyValue{std::string("Another name")})),
                "change source content without changing its serialized field length");
            check(static_cast<bool>(savePuzzleCampaignProgress(isolatedTransferStore, 0, scene, profile,
                "1.0.0", campaignSession)), "update isolated source after preview");
            const auto updatedPrimary = readBytes(sourcePrimary);
            const auto updatedBackup = readBytes(sourceBackup);
            check(updatedPrimary.size() == primaryBeforeRevalidation.size()
                && updatedPrimary != primaryBeforeRevalidation,
                "same-size primary contents change after the preview");
            const auto staleSource = revalidatePuzzleCampaignSaveTransferPreview(shownPreview.value());
            check(staleSource && staleSource.value().state == PuzzleCampaignSaveTransferPreflightState::StaleSource,
                "same-size source edits invalidate the displayed preview");

            const auto refreshedPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                isolatedTarget, campaignDefinition.id);
            check(static_cast<bool>(refreshedPreview), "refresh the preview after a source change");
            if (refreshedPreview)
            {
                std::filesystem::create_directories(isolatedTarget, filesystemError);
                check(!filesystemError, "create only the isolated target fixture");
                {
                    std::ofstream emptyConflict(isolatedTarget / "slot-0.astro-save", std::ios::binary);
                }
                std::filesystem::create_directory(isolatedTarget / "slot-1.astro-save.bak", filesystemError);
                check(!filesystemError, "create only an isolated directory collision fixture");
                const auto staleDestination = revalidatePuzzleCampaignSaveTransferPreview(refreshedPreview.value());
                check(staleDestination
                    && staleDestination.value().state == PuzzleCampaignSaveTransferPreflightState::StaleDestination,
                    "a destination created after preview requires a fresh review");
                const auto raceConflictPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                    isolatedTarget, campaignDefinition.id);
                check(raceConflictPreview && raceConflictPreview.value().slots[0].primaryConflict
                    && raceConflictPreview.value().slots[1].backupConflict
                    && raceConflictPreview.value().conflictingSlots == 2,
                    "zero-byte files and directories at expected slot paths count as destination collisions");
                if (raceConflictPreview)
                {
                    const auto blocked = revalidatePuzzleCampaignSaveTransferPreview(raceConflictPreview.value());
                    check(blocked && blocked.value().state
                        == PuzzleCampaignSaveTransferPreflightState::DestinationConflict,
                        "an unchanged collision blocks a future transfer by default");
                    const auto refusedCopy = copyPuzzleCampaignSaveTransfer(raceConflictPreview.value(), true);
                    check(refusedCopy && refusedCopy.value().state
                        == PuzzleCampaignSaveTransferCopyState::DestinationConflict,
                        "copy operation preserves existing destination collisions");
                }
                check(readBytes(sourcePrimary) == updatedPrimary && readBytes(sourceBackup) == updatedBackup,
                    "stale and conflicting preflight checks leave isolated source saves unchanged");
            }

            const auto confirmationTarget = temporaryRoot / "Confirmation Target";
            const auto confirmationPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                confirmationTarget, campaignDefinition.id);
            check(static_cast<bool>(confirmationPreview), "preview isolated confirmation target");
            if (confirmationPreview)
            {
                const auto notConfirmed = copyPuzzleCampaignSaveTransfer(confirmationPreview.value(), false);
                check(notConfirmed && notConfirmed.value().state
                    == PuzzleCampaignSaveTransferCopyState::ConfirmationRequired
                    && !std::filesystem::exists(confirmationTarget),
                    "copy refuses to write or create a directory without explicit confirmation");
            }

            const auto handledAbortTarget = temporaryRoot / "Handled Abort Target";
            const auto interruptedStart = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                handledAbortTarget, campaignDefinition.id);
            std::string capturedJournal;
#ifndef _WIN32
            bool inFlightAcknowledgeRejected = false;
#endif
            if (interruptedStart)
            {
                const auto interruptedRun = copyPuzzleCampaignSaveTransfer(interruptedStart.value(), true,
                    [&](const std::size_t completed, const std::size_t) {
                        if (completed == 1)
                        {
#ifndef _WIN32
                            const auto activeRecovery = previewPuzzleCampaignSaveTransfer(
                                isolatedTransferStore.directory(), handledAbortTarget, campaignDefinition.id);
                            if (activeRecovery && activeRecovery.value().pendingRecovery)
                            {
                                const auto attemptedAcknowledgement = acknowledgePuzzleCampaignSaveTransferRecovery(
                                    *activeRecovery.value().pendingRecovery, true);
                                inFlightAcknowledgeRejected = !attemptedAcknowledgement
                                    && std::filesystem::exists(handledAbortTarget / ".astro-campaign-transfer.pending");
                            }
#endif
                            capturedJournal = readBytes(handledAbortTarget / ".astro-campaign-transfer.pending");
                            return false;
                        }
                        return true;
                    });
#ifdef _WIN32
                check(interruptedRun && interruptedRun.value().state
                    == PuzzleCampaignSaveTransferCopyState::CancelledRolledBack
                    && !std::filesystem::exists(handledAbortTarget),
                    "copy rollback removes its created files and target directory after cancellation");
#else
                check(interruptedRun && interruptedRun.value().state
                    == PuzzleCampaignSaveTransferCopyState::CancelledRolledBack
                    && capturedJournal.starts_with("ASTRO_CAMPAIGN_TRANSFER 1\n")
                    && !std::filesystem::exists(handledAbortTarget),
                    "copy writes its recovery manifest before data and removes it after a handled rollback");
#endif
#ifndef _WIN32
                check(inFlightAcknowledgeRejected,
                    "acknowledgement cannot remove a recovery journal while its copy transaction holds the lease");
#endif
            }
            else check(false, "preview isolated interrupted-transfer recovery target");

            const auto interruptedTarget = temporaryRoot / "Interrupted Transfer Target";
            const auto childExit = launchInterruptedCopyChild(isolatedTransferStore.directory(), interruptedTarget,
                campaignDefinition.id);
            const auto interruptedJournalPath = interruptedTarget / ".astro-campaign-transfer.pending";
            check(childExit == 77 && std::filesystem::exists(interruptedJournalPath),
                "child process exits abruptly after the first artifact while preserving its recovery journal");
            const auto recoveryPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                interruptedTarget, campaignDefinition.id);
            check(recoveryPreview && recoveryPreview.value().pendingRecovery
                && recoveryPreview.value().pendingRecovery->journalValid
                && recoveryPreview.value().pendingRecovery->artifacts.size() == 2
                && recoveryPreview.value().pendingRecovery->artifacts[0].state
                    == PuzzleCampaignSaveTransferRecoveryArtifactState::Complete
                && recoveryPreview.value().pendingRecovery->artifacts[1].state
                    == PuzzleCampaignSaveTransferRecoveryArtifactState::Missing,
                "restarting after a process interruption reports committed and not-yet-written artifacts");
            if (recoveryPreview && recoveryPreview.value().pendingRecovery)
            {
                const auto recoveryPreflight = revalidatePuzzleCampaignSaveTransferPreview(recoveryPreview.value());
                const auto blockedRetry = copyPuzzleCampaignSaveTransfer(recoveryPreview.value(), true);
                check(recoveryPreflight && recoveryPreflight.value().state
                    == PuzzleCampaignSaveTransferPreflightState::RecoveryRequired
                    && blockedRetry && blockedRetry.value().state == PuzzleCampaignSaveTransferCopyState::RecoveryRequired,
                    "pending recovery diagnostics block a new copy until the record is reviewed");
                const auto refusedAcknowledge = acknowledgePuzzleCampaignSaveTransferRecovery(
                    *recoveryPreview.value().pendingRecovery, false);
                check(!refusedAcknowledge && std::filesystem::exists(interruptedJournalPath),
                    "recovery record cannot be dismissed without explicit confirmation");
                const auto reviewedJournalBytes = readBytes(interruptedJournalPath);
                { std::ofstream journal(interruptedJournalPath, std::ios::binary | std::ios::app); journal << "changed"; }
                const auto staleAcknowledge = acknowledgePuzzleCampaignSaveTransferRecovery(
                    *recoveryPreview.value().pendingRecovery, true);
                check(!staleAcknowledge && std::filesystem::exists(interruptedJournalPath)
                    && readBytes(interruptedJournalPath) == reviewedJournalBytes + "changed",
                    "acknowledgement refuses a journal changed since the displayed review");
                { std::ofstream journal(interruptedJournalPath, std::ios::binary | std::ios::trunc);
                    journal.write(reviewedJournalBytes.data(), static_cast<std::streamsize>(reviewedJournalBytes.size())); }
#ifdef _WIN32
                const HANDLE competingWriter = CreateFileW(interruptedJournalPath.c_str(), GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                const bool writerLockAcquired = competingWriter != INVALID_HANDLE_VALUE;
                OVERLAPPED writerLockState{};
                const bool writerRangeLocked = writerLockAcquired
                    && LockFileEx(competingWriter, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                        0, MAXDWORD, MAXDWORD, &writerLockState) != FALSE;
                const auto lockedAcknowledge = acknowledgePuzzleCampaignSaveTransferRecovery(
                    *recoveryPreview.value().pendingRecovery, true);
                if (writerRangeLocked)
                    UnlockFileEx(competingWriter, 0, MAXDWORD, MAXDWORD, &writerLockState);
                if (writerLockAcquired) CloseHandle(competingWriter);
                check(writerRangeLocked && !lockedAcknowledge
                    && std::filesystem::exists(interruptedJournalPath)
                    && readBytes(interruptedJournalPath) == reviewedJournalBytes,
                    "active competing writer handle makes acknowledgement fail closed and preserves the journal");
                const auto acknowledged = acknowledgePuzzleCampaignSaveTransferRecovery(
                    *recoveryPreview.value().pendingRecovery, true);
                check(acknowledged && !std::filesystem::exists(interruptedJournalPath)
                    && std::filesystem::exists(interruptedTarget / "slot-0.astro-save"),
                    "handle-verified acknowledgement removes only the byte-reviewed journal and preserves every save artifact");
#else
                auto lockChild = startJournalLockChild(interruptedJournalPath);
                check(lockChild.ready, "a separate process acquires the POSIX advisory journal lock");
                const auto lockedAcknowledge = acknowledgePuzzleCampaignSaveTransferRecovery(
                    *recoveryPreview.value().pendingRecovery, true);
                const bool releasedChild = releaseJournalLockChild(lockChild);
                check(lockChild.ready && !lockedAcknowledge && releasedChild
                    && std::filesystem::exists(interruptedJournalPath)
                    && readBytes(interruptedJournalPath) == reviewedJournalBytes,
                    "a cooperating process lock makes acknowledgement fail closed and preserves the journal");
                const auto acknowledged = acknowledgePuzzleCampaignSaveTransferRecovery(
                    *recoveryPreview.value().pendingRecovery, true);
                check(acknowledged && !std::filesystem::exists(interruptedJournalPath)
                    && std::filesystem::exists(interruptedTarget / "slot-0.astro-save"),
                    "POSIX acknowledgement removes only the byte-reviewed journal and preserves every save artifact");
#endif
                const auto retainedSlotPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                    interruptedTarget, campaignDefinition.id);
#ifdef _WIN32
                check(retainedSlotPreview && !retainedSlotPreview.value().pendingRecovery
                    && retainedSlotPreview.value().slots[0].primaryConflict,
                    "acknowledging the journal does not make an existing partial slot overwriteable");
#else
                check(retainedSlotPreview && !retainedSlotPreview.value().pendingRecovery
                    && retainedSlotPreview.value().slots[0].primaryConflict,
                    "POSIX acknowledgement leaves save artifacts conflicting with fresh transfers");
#endif
            }

            struct InvalidJournalCase final
            {
                std::string name;
                std::string bytes;
                PuzzleCampaignSaveTransferRecoveryJournalState expectedState =
                    PuzzleCampaignSaveTransferRecoveryJournalState::Malformed;
                bool directory = false;
            };
            const std::vector<InvalidJournalCase> invalidJournalCases{
                {"Unsupported journal version", "ASTRO_CAMPAIGN_TRANSFER 2\n", PuzzleCampaignSaveTransferRecoveryJournalState::Malformed},
                {"Truncated journal", "ASTRO_CAMPAIGN_TRANSFER 1\nCAMPAIGN " + campaignDefinition.id
                    + "\nCOUNT 1\nITEM 0 P 8 1\n", PuzzleCampaignSaveTransferRecoveryJournalState::Malformed},
                {"Oversized journal", std::string(4097, 'x'), PuzzleCampaignSaveTransferRecoveryJournalState::Oversized},
                {"Non-file journal path", {}, PuzzleCampaignSaveTransferRecoveryJournalState::Unsafe, true},
            };
            for (std::size_t index = 0; index < invalidJournalCases.size(); ++index)
            {
                const auto& fixture = invalidJournalCases[index];
                const auto invalidTarget = temporaryRoot / ("Invalid Journal Target " + std::to_string(index));
                std::filesystem::create_directories(invalidTarget);
                const auto invalidJournalPath = invalidTarget / ".astro-campaign-transfer.pending";
                if (fixture.directory)
                {
                    std::filesystem::create_directory(invalidJournalPath);
                    std::ofstream nested(invalidJournalPath / "keep.txt", std::ios::binary);
                    nested << "journal-path occupant";
                }
                else
                {
                    std::ofstream journal(invalidJournalPath, std::ios::binary);
                    journal.write(fixture.bytes.data(), static_cast<std::streamsize>(fixture.bytes.size()));
                }
                const std::string candidateBefore = "candidate save " + fixture.name;
                const auto candidatePath = invalidTarget / "slot-0.astro-save";
                { std::ofstream candidate(candidatePath, std::ios::binary); candidate << candidateBefore; }
                const auto invalidPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                    invalidTarget, campaignDefinition.id);
                check(invalidPreview && invalidPreview.value().pendingRecovery
                    && invalidPreview.value().pendingRecovery->present
                    && !invalidPreview.value().pendingRecovery->journalValid
                    && invalidPreview.value().pendingRecovery->journalState == fixture.expectedState
                    && invalidPreview.value().pendingRecovery->artifacts.empty()
                    && !invalidPreview.value().pendingRecovery->diagnostic.empty(),
                    "invalid recovery journal is categorized without inferring target artifact paths");
                if (invalidPreview && invalidPreview.value().pendingRecovery)
                {
                    const auto blockedCopy = copyPuzzleCampaignSaveTransfer(invalidPreview.value(), true);
                    const auto blockedResume = resumePuzzleCampaignSaveTransfer(invalidPreview.value(), true);
                    const auto refusedAcknowledge = acknowledgePuzzleCampaignSaveTransferRecovery(
                        *invalidPreview.value().pendingRecovery, true);
                    check(blockedCopy && blockedCopy.value().state == PuzzleCampaignSaveTransferCopyState::RecoveryRequired
                        && blockedResume && blockedResume.value().state == PuzzleCampaignSaveTransferCopyState::RecoveryRequired
                        && !refusedAcknowledge && std::filesystem::exists(invalidJournalPath)
                        && readBytes(candidatePath) == candidateBefore,
                        "invalid journal blocks copy/resume/acknowledgement and preserves journal plus candidate save");
                    if (fixture.directory)
                        check(readBytes(invalidJournalPath / "keep.txt") == "journal-path occupant",
                            "unsafe journal directory and its contents are preserved");
                    if (index == 0 && fixture.expectedState == PuzzleCampaignSaveTransferRecoveryJournalState::Malformed)
                    {
                        const auto denied = archiveMalformedPuzzleCampaignSaveTransferJournal(
                            *invalidPreview.value().pendingRecovery, false);
                        check(!denied && std::filesystem::exists(invalidJournalPath),
                            "journal archive requires a separate explicit confirmation");
#ifdef _WIN32
                        const HANDLE archiveLock = CreateFileW(invalidJournalPath.c_str(),
                            GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                        OVERLAPPED archiveLockState{};
                        const bool archiveLockAcquired = archiveLock != INVALID_HANDLE_VALUE
                            && LockFileEx(archiveLock, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                                0, MAXDWORD, MAXDWORD, &archiveLockState) != FALSE;
                        const auto blockedArchive = archiveMalformedPuzzleCampaignSaveTransferJournal(
                            *invalidPreview.value().pendingRecovery, true);
                        if (archiveLockAcquired)
                            UnlockFileEx(archiveLock, 0, MAXDWORD, MAXDWORD, &archiveLockState);
                        if (archiveLock != INVALID_HANDLE_VALUE) CloseHandle(archiveLock);
#else
                        auto archiveLock = startJournalLockChild(invalidJournalPath);
                        const auto blockedArchive = archiveLock.ready
                            ? archiveMalformedPuzzleCampaignSaveTransferJournal(
                                *invalidPreview.value().pendingRecovery, true)
                            : astro::core::Result<std::filesystem::path>::failure({
                                astro::core::ErrorCode::InvalidState, "separate-process lock unavailable"});
                        const bool archiveLockAcquired = archiveLock.ready;
                        const bool archiveLockReleased = archiveLockAcquired && releaseJournalLockChild(archiveLock);
#endif
                        bool archiveAppearedWhileLocked = false;
                        std::error_code archiveEnumerateError;
                        for (std::filesystem::directory_iterator iterator(invalidTarget, archiveEnumerateError), end;
                            iterator != end && !archiveEnumerateError; iterator.increment(archiveEnumerateError))
                            archiveAppearedWhileLocked = archiveAppearedWhileLocked
                                || iterator->path().filename().string().starts_with(
                                    ".astro-campaign-transfer.invalid-");
#ifdef _WIN32
                        check(archiveLockAcquired && !blockedArchive
                            && !archiveAppearedWhileLocked && !archiveEnumerateError
                            && readBytes(invalidJournalPath) == fixture.bytes
                            && readBytes(candidatePath) == candidateBefore,
                            "Windows journal lock blocks concurrent malformed-journal archival without changing files");
#else
                        check(archiveLockAcquired && archiveLockReleased && !blockedArchive
                            && !archiveAppearedWhileLocked && !archiveEnumerateError
                            && readBytes(invalidJournalPath) == fixture.bytes
                            && readBytes(candidatePath) == candidateBefore,
                            "separate POSIX journal lock blocks concurrent archival and preserves source plus save data");
#endif
                        const auto missingArchive = inspectMalformedPuzzleCampaignSaveArchive(
                            *invalidPreview.value().pendingRecovery);
                        check(missingArchive && missingArchive.value().pathKind
                                == PuzzleCampaignSaveArchivePathKind::Missing
                            && readBytes(invalidJournalPath) == fixture.bytes
                            && readBytes(candidatePath) == candidateBefore,
                            "archive inspection reports a missing destination without changing source or save files");
                        const auto archived = archiveMalformedPuzzleCampaignSaveTransferJournal(
                            *invalidPreview.value().pendingRecovery, true);
                        check(archived && std::filesystem::exists(invalidJournalPath)
                            && readBytes(invalidJournalPath) == fixture.bytes
                            && readBytes(archived.value()) == fixture.bytes
                            && readBytes(candidatePath) == candidateBefore,
                            "confirmed archive stores an exact copy and preserves the malformed journal plus candidate save");
                        if (archived)
                        {
                            const auto exactInspection = inspectMalformedPuzzleCampaignSaveArchive(
                                *invalidPreview.value().pendingRecovery);
                            check(exactInspection && exactInspection.value().archivePath == archived.value()
                                && exactInspection.value().pathKind == PuzzleCampaignSaveArchivePathKind::RegularFile
                                && exactInspection.value().byteSize == fixture.bytes.size()
                                && exactInspection.value().contentMatch
                                    == PuzzleCampaignSaveArchiveContentMatch::ExactMatch,
                                "archive inspection identifies an exact bounded archive copy");
                            const auto exactConflictCopy = exactInspection
                                ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                    *invalidPreview.value().pendingRecovery, exactInspection.value())
                                : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                                    astro::core::ErrorCode::InvalidState, "exact archive inspection unavailable"});
                            check(!exactConflictCopy,
                                "copy-aside preview refuses an exact archive that is already reusable");
                            const auto staleStagingPath = std::filesystem::path(
                                archived.value().string() + ".tmp-crashed-process");
                            { std::ofstream staleStaging(staleStagingPath, std::ios::binary);
                                staleStaging << "unidentified interrupted staging data"; }
                            const auto retryWithStaleStaging = archiveMalformedPuzzleCampaignSaveTransferJournal(
                                *invalidPreview.value().pendingRecovery, true);
                            check(retryWithStaleStaging && retryWithStaleStaging.value() == archived.value()
                                && readBytes(staleStagingPath) == "unidentified interrupted staging data"
                                && readBytes(archived.value()) == fixture.bytes,
                                "stale operation-like staging file is preserved and does not block reuse of a verified final archive");
                            const auto interruptedArchiveBytes = fixture.bytes.substr(0,
                                std::max<std::size_t>(1, fixture.bytes.size() / 2));
                            { std::ofstream archive(archived.value(), std::ios::binary | std::ios::trunc);
                                archive.write(interruptedArchiveBytes.data(),
                                    static_cast<std::streamsize>(interruptedArchiveBytes.size())); }
                            const auto prefixInspection = inspectMalformedPuzzleCampaignSaveArchive(
                                *invalidPreview.value().pendingRecovery);
                            const auto refusedPartialArchive = archiveMalformedPuzzleCampaignSaveTransferJournal(
                                *invalidPreview.value().pendingRecovery, true);
                            check(prefixInspection && prefixInspection.value().pathKind
                                    == PuzzleCampaignSaveArchivePathKind::RegularFile
                                && prefixInspection.value().contentMatch
                                    == PuzzleCampaignSaveArchiveContentMatch::PrefixMatch
                                && !refusedPartialArchive
                                && readBytes(invalidJournalPath) == fixture.bytes
                                && readBytes(archived.value()) == interruptedArchiveBytes
                                && readBytes(candidatePath) == candidateBefore,
                                "partial archive residue is preserved and blocks retry without changing the malformed source or save");
                            { std::ofstream archive(archived.value(), std::ios::binary | std::ios::trunc);
                                archive.write(fixture.bytes.data(), static_cast<std::streamsize>(fixture.bytes.size())); }
                            const auto repeatedArchive = archiveMalformedPuzzleCampaignSaveTransferJournal(
                                *invalidPreview.value().pendingRecovery, true);
                            check(repeatedArchive && repeatedArchive.value() == archived.value()
                                && readBytes(invalidJournalPath) == fixture.bytes
                                && readBytes(archived.value()) == fixture.bytes,
                                "repeated archive confirmation reuses the exact archive without retiring the source");
                            { std::ofstream archive(archived.value(), std::ios::binary | std::ios::trunc);
                                archive << "existing archive owner"; }
                            const auto collisionPreview = previewPuzzleCampaignSaveTransfer(
                                isolatedTransferStore.directory(), invalidTarget, campaignDefinition.id);
                            const auto collisionArchive = collisionPreview && collisionPreview.value().pendingRecovery
                                ? archiveMalformedPuzzleCampaignSaveTransferJournal(
                                    *collisionPreview.value().pendingRecovery, true)
                                : astro::core::Result<std::filesystem::path>::failure({
                                    astro::core::ErrorCode::InvalidState, "collision preview unavailable"});
                            check(collisionPreview && !collisionArchive
                                && collisionPreview.value().pendingRecovery
                                && [&] {
                                    const auto inspection = inspectMalformedPuzzleCampaignSaveArchive(
                                        *collisionPreview.value().pendingRecovery);
                                    return inspection
                                        && inspection.value().pathKind == PuzzleCampaignSaveArchivePathKind::RegularFile
                                        && inspection.value().contentMatch
                                            == PuzzleCampaignSaveArchiveContentMatch::Different
                                        && inspection.value().byteSize == std::string_view("existing archive owner").size();
                                }()
                                && readBytes(invalidJournalPath) == fixture.bytes
                                && readBytes(archived.value()) == "existing archive owner"
                                && readBytes(candidatePath) == candidateBefore,
                                "archive collision preserves the malformed source and refuses to replace the archive occupant");
                            if (collisionPreview && collisionPreview.value().pendingRecovery)
                            {
                                std::error_code objectError;
                                const auto collisionInspection = inspectMalformedPuzzleCampaignSaveArchive(
                                    *collisionPreview.value().pendingRecovery);
                                const auto copyPreview = collisionInspection
                                    ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                        *collisionPreview.value().pendingRecovery, collisionInspection.value())
                                    : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                                        astro::core::ErrorCode::InvalidState, "conflict inspection unavailable"});
                                const auto deniedCopy = copyPreview
                                    ? preserveMalformedPuzzleCampaignSaveArchiveConflict(
                                        *collisionPreview.value().pendingRecovery, copyPreview.value(), false)
                                    : astro::core::Result<std::filesystem::path>::failure({
                                        astro::core::ErrorCode::InvalidState, "copy preview unavailable"});
                                check(copyPreview && !copyPreview.value().reviewedBytes.empty()
                                    && copyPreview.value().reviewedBytes.size() == std::string_view("existing archive owner").size()
                                    && std::string(copyPreview.value().reviewedBytes.begin(),
                                        copyPreview.value().reviewedBytes.end()) == "existing archive owner"
                                    && !std::filesystem::exists(copyPreview ? copyPreview.value().destinationPath
                                        : invalidTarget / "missing-copy-preview")
                                    && !deniedCopy && readBytes(archived.value()) == "existing archive owner"
                                    && readBytes(invalidJournalPath) == fixture.bytes
                                    && readBytes(candidatePath) == candidateBefore,
                                    "copy preview is read-only and a missing confirmation creates no file");

                                if (copyPreview)
                                {
                                    { std::ofstream destination(copyPreview.value().destinationPath, std::ios::binary);
                                        destination << "destination owner sentinel"; }
                                    const auto collisionCopy = preserveMalformedPuzzleCampaignSaveArchiveConflict(
                                        *collisionPreview.value().pendingRecovery, copyPreview.value(), true);
                                    check(!collisionCopy
                                        && readBytes(copyPreview.value().destinationPath) == "destination owner sentinel"
                                        && readBytes(archived.value()) == "existing archive owner"
                                        && readBytes(invalidJournalPath) == fixture.bytes
                                        && readBytes(candidatePath) == candidateBefore,
                                        "copy-aside publication refuses to replace a destination that appears after preview");
                                    std::filesystem::remove(copyPreview.value().destinationPath, objectError);
                                }

                                auto staleCopyPreview = collisionInspection
                                    ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                        *collisionPreview.value().pendingRecovery, collisionInspection.value())
                                    : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                                        astro::core::ErrorCode::InvalidState, "conflict inspection unavailable"});
                                { std::ofstream changed(archived.value(), std::ios::binary | std::ios::trunc);
                                    changed << "changed after preview"; }
                                const auto staleCopy = staleCopyPreview
                                    ? preserveMalformedPuzzleCampaignSaveArchiveConflict(
                                        *collisionPreview.value().pendingRecovery, staleCopyPreview.value(), true)
                                    : astro::core::Result<std::filesystem::path>::failure({
                                        astro::core::ErrorCode::InvalidState, "stale copy preview unavailable"});
                                check(staleCopyPreview && !staleCopy
                                    && !std::filesystem::exists(staleCopyPreview ? staleCopyPreview.value().destinationPath
                                        : invalidTarget / "missing-stale-copy")
                                    && readBytes(invalidJournalPath) == fixture.bytes
                                    && readBytes(candidatePath) == candidateBefore,
                                    "changed conflict source after preview blocks copy and preserves saves");
                                { std::ofstream owner(archived.value(), std::ios::binary | std::ios::trunc);
                                    owner << "existing archive owner"; }

                                const auto currentInspection = inspectMalformedPuzzleCampaignSaveArchive(
                                    *collisionPreview.value().pendingRecovery);
                                const auto approvedCopyPreview = currentInspection
                                    ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                        *collisionPreview.value().pendingRecovery, currentInspection.value())
                                    : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                                        astro::core::ErrorCode::InvalidState, "fresh conflict inspection unavailable"});
                                const auto preservedCopy = approvedCopyPreview
                                    ? preserveMalformedPuzzleCampaignSaveArchiveConflict(
                                        *collisionPreview.value().pendingRecovery, approvedCopyPreview.value(), true)
                                    : astro::core::Result<std::filesystem::path>::failure({
                                        astro::core::ErrorCode::InvalidState, "fresh copy preview unavailable"});
                                check(approvedCopyPreview && preservedCopy
                                    && preservedCopy.value() == approvedCopyPreview.value().destinationPath
                                    && readBytes(preservedCopy.value()) == "existing archive owner"
                                    && readBytes(archived.value()) == "existing archive owner"
                                    && readBytes(invalidJournalPath) == fixture.bytes
                                    && readBytes(candidatePath) == candidateBefore,
                                    "confirmed copy-aside writes exact bytes to a distinct adjacent file and preserves source/journal/slot");
                                const auto racedCopyPreview = inspectMalformedPuzzleCampaignSaveArchive(
                                    *collisionPreview.value().pendingRecovery);
                                const auto racedCopyPlan = racedCopyPreview
                                    ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                        *collisionPreview.value().pendingRecovery, racedCopyPreview.value())
                                    : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                                        astro::core::ErrorCode::InvalidState, "race inspection unavailable"});
                                const auto racedDestination = racedCopyPlan
                                    ? racedCopyPlan.value().destinationPath
                                    : invalidTarget / "missing-race-copy";
                                const auto racedCopy = racedCopyPlan
                                    ? testing::preserveMalformedCampaignArchiveConflictWithHook(
                                        *collisionPreview.value().pendingRecovery, racedCopyPlan.value(),
                                        [racedDestination](const ArchiveBoundary boundary) {
                                            if (boundary != ArchiveBoundary::VerifiedBeforePublish) return;
                                            std::ofstream output(racedDestination, std::ios::binary);
                                            output << "racing destination owner";
                                            output.flush();
                                        })
                                    : astro::core::Result<std::filesystem::path>::failure({
                                        astro::core::ErrorCode::InvalidState, "race copy preview unavailable"});
                                check(racedCopyPlan && !racedCopy
                                    && readBytes(racedDestination) == "racing destination owner"
                                    && readBytes(archived.value()) == "existing archive owner"
                                    && readBytes(invalidJournalPath) == fixture.bytes
                                    && readBytes(candidatePath) == candidateBefore,
                                    "destination appearing after staging is never replaced by copy-aside publication");
                                std::filesystem::remove(racedDestination, objectError);

                                std::filesystem::remove(archived.value(), objectError);
                                objectError.clear();
                                { std::ofstream oversized(archived.value(), std::ios::binary);
                                    oversized << std::string(4097, 'x'); }
                                const auto oversizedInspection = inspectMalformedPuzzleCampaignSaveArchive(
                                    *collisionPreview.value().pendingRecovery);
                                const auto oversizedCopy = oversizedInspection
                                    ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                        *collisionPreview.value().pendingRecovery, oversizedInspection.value())
                                    : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                                        astro::core::ErrorCode::InvalidState, "oversized inspection unavailable"});
                                check(oversizedInspection && oversizedInspection.value().pathKind
                                        == PuzzleCampaignSaveArchivePathKind::RegularFile
                                    && oversizedInspection.value().byteSize == 4097
                                    && oversizedInspection.value().contentMatch
                                        == PuzzleCampaignSaveArchiveContentMatch::Oversized
                                    && !oversizedCopy,
                                    "archive inspector reports oversized occupants without reading them");
                                std::filesystem::remove(archived.value(), objectError);
                                objectError.clear();
                                const bool directoryCreated = std::filesystem::create_directory(
                                    archived.value(), objectError);
                                if (directoryCreated)
                                {
                                    const auto directoryInspection = inspectMalformedPuzzleCampaignSaveArchive(
                                        *collisionPreview.value().pendingRecovery);
                                    const auto directoryCopy = directoryInspection
                                        ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                            *collisionPreview.value().pendingRecovery, directoryInspection.value())
                                        : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                                            astro::core::ErrorCode::InvalidState, "directory inspection unavailable"});
                                    check(directoryInspection && directoryInspection.value().pathKind
                                            == PuzzleCampaignSaveArchivePathKind::Directory
                                        && !directoryCopy
                                        && readBytes(invalidJournalPath) == fixture.bytes
                                        && readBytes(candidatePath) == candidateBefore,
                                        "archive inspector classifies a directory without following or modifying it");
                                    std::filesystem::remove(archived.value(), objectError);
                                }
                                const auto referent = invalidTarget / "archive-link-referent.txt";
                                { std::ofstream output(referent, std::ios::binary); output << "link referent sentinel"; }
                                objectError.clear();
                                std::filesystem::create_symlink(referent, archived.value(), objectError);
                                if (!objectError)
                                {
                                    const auto linkInspection = inspectMalformedPuzzleCampaignSaveArchive(
                                        *collisionPreview.value().pendingRecovery);
                                    const auto linkCopy = linkInspection
                                        ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                                            *collisionPreview.value().pendingRecovery, linkInspection.value())
                                        : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                                            astro::core::ErrorCode::InvalidState, "link inspection unavailable"});
                                    check(linkInspection && linkInspection.value().pathKind
                                            == PuzzleCampaignSaveArchivePathKind::SymlinkOrReparsePoint
                                        && !linkCopy
                                        && readBytes(referent) == "link referent sentinel"
                                        && readBytes(invalidJournalPath) == fixture.bytes
                                        && readBytes(candidatePath) == candidateBefore,
                                        "archive inspector identifies a link without reading its referent or modifying data");
                                    std::filesystem::remove(archived.value(), objectError);
                                }
                            }
                        }
                    }
                    if (index == 1 && fixture.expectedState == PuzzleCampaignSaveTransferRecoveryJournalState::Malformed)
                    {
                        { std::ofstream journal(invalidJournalPath, std::ios::binary | std::ios::app); journal << "changed"; }
                        const auto staleArchive = archiveMalformedPuzzleCampaignSaveTransferJournal(
                            *invalidPreview.value().pendingRecovery, true);
                        bool archiveAppeared = false;
                        std::error_code enumerateError;
                        for (std::filesystem::directory_iterator iterator(invalidTarget, enumerateError), end;
                            iterator != end && !enumerateError; iterator.increment(enumerateError))
                            archiveAppeared = archiveAppeared
                                || iterator->path().filename().string().starts_with(".astro-campaign-transfer.invalid-");
                        check(!staleArchive && readBytes(candidatePath) == candidateBefore
                            && !archiveAppeared && !enumerateError,
                            "journal changed after review cannot be archived and candidate saves remain untouched");
                    }
                }
            }

            {
                const auto unicodeTarget = temporaryRoot / std::filesystem::path(u8"Проверка копии Ω");
                std::filesystem::create_directories(unicodeTarget);
                const auto journalPath = unicodeTarget / ".astro-campaign-transfer.pending";
                const auto candidatePath = unicodeTarget / "slot-0.astro-save";
                const std::string malformedJournal = "ASTRO_CAMPAIGN_TRANSFER 2\n";
                const std::string ownerBytes = "unknown owner at Unicode path";
                const std::string candidateBytes = "candidate save sentinel";
                { std::ofstream journal(journalPath, std::ios::binary);
                    journal.write(malformedJournal.data(), static_cast<std::streamsize>(malformedJournal.size())); }
                { std::ofstream candidate(candidatePath, std::ios::binary);
                    candidate.write(candidateBytes.data(), static_cast<std::streamsize>(candidateBytes.size())); }
                const auto transferPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                    unicodeTarget, campaignDefinition.id);
                const auto archivePath = transferPreview && transferPreview.value().pendingRecovery
                    ? campaignArchivePath(unicodeTarget, transferPreview.value().pendingRecovery->journalFingerprint)
                    : unicodeTarget / ".missing-expected-archive";
                { std::ofstream owner(archivePath, std::ios::binary);
                    owner.write(ownerBytes.data(), static_cast<std::streamsize>(ownerBytes.size())); }
                const auto inspection = transferPreview && transferPreview.value().pendingRecovery
                    ? inspectMalformedPuzzleCampaignSaveArchive(*transferPreview.value().pendingRecovery)
                    : astro::core::Result<PuzzleCampaignSaveArchiveInspection>::failure({
                        astro::core::ErrorCode::InvalidState, "Unicode path recovery preview unavailable"});
                const auto copyPreview = inspection && transferPreview && transferPreview.value().pendingRecovery
                    ? previewMalformedPuzzleCampaignSaveArchiveConflictCopy(
                        *transferPreview.value().pendingRecovery, inspection.value())
                    : astro::core::Result<PuzzleCampaignSaveArchiveConflictCopyPreview>::failure({
                        astro::core::ErrorCode::InvalidState, "Unicode path conflict inspection unavailable"});
                const auto copied = copyPreview && transferPreview && transferPreview.value().pendingRecovery
                    ? preserveMalformedPuzzleCampaignSaveArchiveConflict(
                        *transferPreview.value().pendingRecovery, copyPreview.value(), true)
                    : astro::core::Result<std::filesystem::path>::failure({
                        astro::core::ErrorCode::InvalidState, "Unicode path copy preview unavailable"});
                check(transferPreview && transferPreview.value().pendingRecovery && inspection && copyPreview && copied
                    && copied.value() == copyPreview.value().destinationPath
                    && copyPreview.value().sourcePath.parent_path() == unicodeTarget
                    && copied.value().parent_path() == unicodeTarget
                    && readBytes(copied.value()) == ownerBytes && readBytes(archivePath) == ownerBytes
                    && readBytes(journalPath) == malformedJournal && readBytes(candidatePath) == candidateBytes,
                    "copy-aside preserves exact bytes and source/journal/slot through a Unicode parent path");
            }

            struct ArchiveInterruptionCase final
            {
                ArchiveBoundary boundary;
                bool archivePublished = false;
                bool stagingContainsFullBytes = false;
            };
            const std::string interruptedJournalBytes = "ASTRO_CAMPAIGN_TRANSFER 2\n";
            const std::string interruptionCandidateBytes = "candidate save must remain unchanged";
            const std::vector<ArchiveInterruptionCase> archiveInterruptionCases{
                {ArchiveBoundary::PartialWrite, false, false},
                {ArchiveBoundary::VerifiedBeforePublish, false, true},
                {ArchiveBoundary::PublishedBeforeStagingCleanup, true, true},
            };
            for (const auto& interruption : archiveInterruptionCases)
            {
                const auto caseName = archiveBoundaryName(interruption.boundary);
                const auto target = temporaryRoot / ("Archive interruption " + caseName);
                std::filesystem::create_directories(target);
                const auto journalPath = target / ".astro-campaign-transfer.pending";
                const auto candidatePath = target / "slot-0.astro-save";
                { std::ofstream journal(journalPath, std::ios::binary);
                    journal.write(interruptedJournalBytes.data(), static_cast<std::streamsize>(interruptedJournalBytes.size())); }
                { std::ofstream candidate(candidatePath, std::ios::binary); candidate << interruptionCandidateBytes; }

                const auto interruptionPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                    target, campaignDefinition.id);
                const auto markerPath = target / "subprocess-paused.marker";
                const bool childKilledAtBoundary = interruptionPreview && interruptionPreview.value().pendingRecovery
                    && launchInterruptedArchiveChild(isolatedTransferStore.directory(), target,
                        campaignDefinition.id, interruption.boundary, markerPath);
                check(childKilledAtBoundary && std::filesystem::exists(markerPath),
                    "archive child process is terminated at the requested durable-operation boundary");
                std::error_code markerError;
                std::filesystem::remove(markerPath, markerError);

                const auto archivePath = interruptionPreview && interruptionPreview.value().pendingRecovery
                    ? campaignArchivePath(target, interruptionPreview.value().pendingRecovery->journalFingerprint)
                    : target / ".missing-expected-archive";
                const bool finalExists = std::filesystem::exists(archivePath);
                auto stagingPaths = campaignArchiveStagingPaths(archivePath);
                std::size_t expectedStagingCount = 1;
#ifdef _WIN32
                if (interruption.boundary == ArchiveBoundary::PublishedBeforeStagingCleanup)
                    expectedStagingCount = 0;
#endif
                bool stagingMatchesBoundary = stagingPaths.size() == expectedStagingCount;
                std::vector<std::pair<std::filesystem::path, std::string>> residueBeforeRetry;
                for (const auto& stagingPath : stagingPaths)
                {
                    const auto bytes = readBytes(stagingPath);
                    stagingMatchesBoundary = stagingMatchesBoundary && !bytes.empty()
                        && (interruption.stagingContainsFullBytes
                            ? bytes == interruptedJournalBytes : bytes.size() < interruptedJournalBytes.size());
                    residueBeforeRetry.emplace_back(stagingPath, bytes);
                }
                bool publishedStateMatches = finalExists == interruption.archivePublished;
                if (interruption.archivePublished)
                    publishedStateMatches = publishedStateMatches && readBytes(archivePath) == interruptedJournalBytes;
#ifdef _WIN32
                if (interruption.boundary == ArchiveBoundary::PublishedBeforeStagingCleanup)
                    stagingMatchesBoundary = stagingMatchesBoundary && stagingPaths.empty();
#else
                if (interruption.boundary == ArchiveBoundary::PublishedBeforeStagingCleanup)
                    stagingMatchesBoundary = stagingMatchesBoundary && stagingPaths.size() == 1
                        && readBytes(stagingPaths.front()) == interruptedJournalBytes;
#endif
                check(childKilledAtBoundary && publishedStateMatches && stagingMatchesBoundary
                    && readBytes(journalPath) == interruptedJournalBytes
                    && readBytes(candidatePath) == interruptionCandidateBytes,
                    "interrupted archive leaves only the expected final/staging state and preserves journal plus slot bytes");

                const auto retry = interruptionPreview && interruptionPreview.value().pendingRecovery
                    ? archiveMalformedPuzzleCampaignSaveTransferJournal(*interruptionPreview.value().pendingRecovery, true)
                    : astro::core::Result<std::filesystem::path>::failure({astro::core::ErrorCode::InvalidState,
                        "archive interruption preview unavailable"});
                const auto stagingPathsAfterRetry = campaignArchiveStagingPaths(archivePath);
                bool oldResiduePreserved = stagingPathsAfterRetry == stagingPaths;
                for (const auto& [stagingPath, bytes] : residueBeforeRetry)
                    oldResiduePreserved = oldResiduePreserved && readBytes(stagingPath) == bytes;
                check(retry && retry.value() == archivePath && readBytes(archivePath) == interruptedJournalBytes
                    && oldResiduePreserved && readBytes(journalPath) == interruptedJournalBytes
                    && readBytes(candidatePath) == interruptionCandidateBytes,
                    "retry publishes or reuses an exact archive while preserving interruption residue and source data");
            }

            struct ConflictCopyInterruptionCase final
            {
                ArchiveBoundary boundary;
                bool copyPublished = false;
                bool stagingContainsFullBytes = false;
            };
            const std::string conflictJournalBytes = "ASTRO_CAMPAIGN_TRANSFER 2\n";
            const std::string conflictOwnerBytes = "unidentified archive owner";
            const std::string conflictSlotBytes = "candidate save remains unchanged";
            const std::vector<ConflictCopyInterruptionCase> conflictCopyInterruptionCases{
                {ArchiveBoundary::VerifiedBeforePublish, false, true},
                {ArchiveBoundary::PublishedBeforeStagingCleanup, true, true},
            };
            for (const auto& interruption : conflictCopyInterruptionCases)
            {
                const auto caseName = archiveBoundaryName(interruption.boundary);
                const auto target = temporaryRoot / ("Conflict copy interruption " + caseName);
                std::filesystem::create_directories(target);
                const auto journalPath = target / ".astro-campaign-transfer.pending";
                const auto candidatePath = target / "slot-0.astro-save";
                { std::ofstream journal(journalPath, std::ios::binary);
                    journal.write(conflictJournalBytes.data(), static_cast<std::streamsize>(conflictJournalBytes.size())); }
                { std::ofstream candidate(candidatePath, std::ios::binary);
                    candidate.write(conflictSlotBytes.data(), static_cast<std::streamsize>(conflictSlotBytes.size())); }

                const auto transferPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                    target, campaignDefinition.id);
                const auto archivePath = transferPreview && transferPreview.value().pendingRecovery
                    ? campaignArchivePath(target, transferPreview.value().pendingRecovery->journalFingerprint)
                    : target / ".missing-expected-archive";
                { std::ofstream owner(archivePath, std::ios::binary);
                    owner.write(conflictOwnerBytes.data(), static_cast<std::streamsize>(conflictOwnerBytes.size())); }
                const auto markerPath = target / "subprocess-paused.marker";
                const bool childKilledAtBoundary = transferPreview && transferPreview.value().pendingRecovery
                    && launchInterruptedArchiveConflictCopyChild(isolatedTransferStore.directory(), target,
                        campaignDefinition.id, interruption.boundary, markerPath);
                check(childKilledAtBoundary && std::filesystem::exists(markerPath),
                    "conflict-copy child is terminated at the requested publication boundary");

                const auto destinationName = readBytes(markerPath);
                std::error_code markerError;
                std::filesystem::remove(markerPath, markerError);
                const auto copyPath = destinationName.empty() ? target / ".missing-copy-destination"
                    : target / std::filesystem::path(destinationName);
                const bool copyExists = std::filesystem::exists(copyPath);
                auto stagingPaths = campaignArchiveStagingPaths(copyPath);
                bool stagingMatchesBoundary = !stagingPaths.empty();
                for (const auto& stagingPath : stagingPaths)
                    stagingMatchesBoundary = stagingMatchesBoundary
                        && readBytes(stagingPath) == conflictOwnerBytes;
                if (interruption.boundary == ArchiveBoundary::PublishedBeforeStagingCleanup)
                {
#ifdef _WIN32
                    stagingMatchesBoundary = stagingPaths.empty();
#else
                    stagingMatchesBoundary = stagingPaths.size() == 1
                        && readBytes(stagingPaths.front()) == conflictOwnerBytes;
#endif
                }
                check(childKilledAtBoundary && !destinationName.empty()
                    && copyExists == interruption.copyPublished && stagingMatchesBoundary
                    && (!copyExists || readBytes(copyPath) == conflictOwnerBytes)
                    && readBytes(archivePath) == conflictOwnerBytes
                    && readBytes(journalPath) == conflictJournalBytes
                    && readBytes(candidatePath) == conflictSlotBytes,
                    "interrupted conflict copy leaves only the expected copy/staging state and preserves archive, journal, and slot");
            }

            const auto resumeTarget = temporaryRoot / "Resumed Transfer Target";
            const auto resumeChildExit = launchInterruptedCopyChild(isolatedTransferStore.directory(), resumeTarget,
                campaignDefinition.id);
            const auto resumePreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                resumeTarget, campaignDefinition.id);
            check(resumeChildExit == 77 && resumePreview && resumePreview.value().pendingRecovery,
                "a second isolated process interruption produces a resumable preview");
            if (resumePreview && resumePreview.value().pendingRecovery)
            {
                const auto refusedResume = resumePuzzleCampaignSaveTransfer(resumePreview.value(), false);
                check(refusedResume && refusedResume.value().state == PuzzleCampaignSaveTransferCopyState::ConfirmationRequired
                    && std::filesystem::exists(resumeTarget / ".astro-campaign-transfer.pending"),
                    "resume requires explicit confirmation and leaves the recovery journal untouched");
                const auto resumed = resumePuzzleCampaignSaveTransfer(resumePreview.value(), true);
                check(resumed && resumed.value().state == PuzzleCampaignSaveTransferCopyState::Resumed
                    && resumed.value().filesSkipped == 1 && resumed.value().filesCopied == 1
                    && resumed.value().filesAttempted == 1
                    && !std::filesystem::exists(resumeTarget / ".astro-campaign-transfer.pending"),
                    "confirmed resume skips the complete artifact, creates the missing one and clears its journal");
                check(readBytes(resumeTarget / "slot-0.astro-save") == updatedPrimary
                    && readBytes(resumeTarget / "slot-0.astro-save.bak") == updatedBackup,
                    "resumed transfer preserves exact source bytes for both primary and backup artifacts");
            }

            const auto changedTarget = temporaryRoot / "Changed Resume Target";
            const auto changedChildExit = launchInterruptedCopyChild(isolatedTransferStore.directory(), changedTarget,
                campaignDefinition.id);
            auto changedPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                changedTarget, campaignDefinition.id);
            check(changedChildExit == 77 && changedPreview && changedPreview.value().pendingRecovery,
                "prepare a separate interrupted transfer for changed-target protection");
            if (changedPreview && changedPreview.value().pendingRecovery)
            {
                const auto changedFile = changedTarget / "slot-0.astro-save";
                { std::ofstream output(changedFile, std::ios::binary | std::ios::trunc); output << "changed target"; }
                const auto refused = resumePuzzleCampaignSaveTransfer(changedPreview.value(), true);
                check(refused && refused.value().state == PuzzleCampaignSaveTransferCopyState::RecoveryRequired
                    && readBytes(changedFile) == "changed target"
                    && !std::filesystem::exists(changedTarget / "slot-0.astro-save.bak")
                    && std::filesystem::exists(changedTarget / ".astro-campaign-transfer.pending"),
                    "resume refuses a target changed after review without overwriting or deleting it");
            }

            const auto partialTarget = temporaryRoot / "Partial Resume Target";
            const auto partialChildExit = launchInterruptedCopyChild(isolatedTransferStore.directory(), partialTarget,
                campaignDefinition.id);
            auto partialPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                partialTarget, campaignDefinition.id);
            check(partialChildExit == 77 && partialPreview && partialPreview.value().pendingRecovery,
                "prepare an isolated interrupted transfer for partial-file recovery policy");
            if (partialPreview && partialPreview.value().pendingRecovery)
            {
                const auto partialPath = partialTarget / "slot-0.astro-save";
                const std::string partialBytes = updatedPrimary.substr(0, std::min<std::size_t>(7, updatedPrimary.size()));
                { std::ofstream output(partialPath, std::ios::binary | std::ios::trunc); output.write(
                    partialBytes.data(), static_cast<std::streamsize>(partialBytes.size())); }
                partialPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(), partialTarget,
                    campaignDefinition.id);
                check(partialPreview && partialPreview.value().pendingRecovery
                    && partialPreview.value().pendingRecovery->artifacts[0].state
                        == PuzzleCampaignSaveTransferRecoveryArtifactState::Partial,
                    "recovery inspection classifies a shorter regular target file as partial");
                if (partialPreview && partialPreview.value().pendingRecovery)
                {
                    const auto refused = resumePuzzleCampaignSaveTransfer(partialPreview.value(), true);
                    check(refused && refused.value().state == PuzzleCampaignSaveTransferCopyState::DestinationConflict
                        && readBytes(partialPath) == partialBytes
                        && !std::filesystem::exists(partialTarget / "slot-0.astro-save.bak")
                        && std::filesystem::exists(partialTarget / ".astro-campaign-transfer.pending"),
                        "resume leaves a reviewed partial target byte-for-byte unchanged and retains its journal");
                }
            }

            const auto staleSourceTarget = temporaryRoot / "Stale Source Resume Target";
            const auto staleSourceChildExit = launchInterruptedCopyChild(isolatedTransferStore.directory(),
                staleSourceTarget, campaignDefinition.id);
            auto staleSourcePreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                staleSourceTarget, campaignDefinition.id);
            check(staleSourceChildExit == 77 && staleSourcePreview && staleSourcePreview.value().pendingRecovery,
                "prepare an isolated interrupted transfer for source revalidation");
            if (staleSourcePreview && staleSourcePreview.value().pendingRecovery)
            {
                { std::ofstream output(sourcePrimary, std::ios::binary | std::ios::trunc); output << "changed source"; }
                const auto refused = resumePuzzleCampaignSaveTransfer(staleSourcePreview.value(), true);
                check(refused && refused.value().state == PuzzleCampaignSaveTransferCopyState::StaleSource
                    && !std::filesystem::exists(staleSourceTarget / "slot-0.astro-save.bak")
                    && std::filesystem::exists(staleSourceTarget / ".astro-campaign-transfer.pending"),
                    "resume revalidates source content and leaves target artifacts and journal unchanged when stale");
                { std::ofstream output(sourcePrimary, std::ios::binary | std::ios::trunc); output << updatedPrimary; }
            }

            const auto cancelTarget = temporaryRoot / "Cancelled Transfer Target";
            const auto cancelPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                cancelTarget, campaignDefinition.id);
            check(static_cast<bool>(cancelPreview), "preview isolated cancellation target");
            if (cancelPreview)
            {
                const auto cancelled = copyPuzzleCampaignSaveTransfer(cancelPreview.value(), true,
                    [](const std::size_t completed, const std::size_t) { return completed != 1; });
                check(cancelled && cancelled.value().state
                    == PuzzleCampaignSaveTransferCopyState::CancelledRolledBack
                    && cancelled.value().filesRolledBack == 1 && !std::filesystem::exists(cancelTarget),
                    "cancellation after one file rolls back the created file and empty target directory");
            }

            const auto raceTarget = temporaryRoot / "Raced Transfer Target";
            const auto racePreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                raceTarget, campaignDefinition.id);
            check(static_cast<bool>(racePreview), "preview isolated exclusive-create race target");
            if (racePreview)
            {
                const auto competingBackup = raceTarget / "slot-0.astro-save.bak";
                bool injected = false;
                const auto raced = copyPuzzleCampaignSaveTransfer(racePreview.value(), true,
                    [&](const std::size_t completed, const std::size_t) {
                        if (completed == 1)
                        {
                            std::ofstream competingFile(competingBackup, std::ios::binary);
                            competingFile << "external writer won the race";
                            injected = static_cast<bool>(competingFile);
                        }
                        return true;
                    });
                check(injected, "inject a competing destination file after the first exclusive create");
                check(raced && raced.value().state == PuzzleCampaignSaveTransferCopyState::RollbackIncomplete
                    && !std::filesystem::exists(raceTarget / "slot-0.astro-save")
                    && readBytes(competingBackup) == "external writer won the race"
                    && !raced.value().remainingArtifacts.empty(),
                    "exclusive create refuses the raced file, rolls back owned output and reports remaining directory");
            }

            const auto successfulTarget = temporaryRoot / "Successful Transfer Target";
            const auto successfulPreview = previewPuzzleCampaignSaveTransfer(isolatedTransferStore.directory(),
                successfulTarget, campaignDefinition.id);
            check(static_cast<bool>(successfulPreview), "preview isolated successful-copy target");
            if (successfulPreview)
            {
                const auto copied = copyPuzzleCampaignSaveTransfer(successfulPreview.value(), true);
                check(copied && copied.value().state == PuzzleCampaignSaveTransferCopyState::Copied
                    && copied.value().filesCopied == 2
                    && copied.value().bytesCopied == updatedPrimary.size() + updatedBackup.size(),
                    "confirmed copy transfers the primary and backup artifacts");
                check(readBytes(successfulTarget / "slot-0.astro-save") == updatedPrimary
                    && readBytes(successfulTarget / "slot-0.astro-save.bak") == updatedBackup,
                    "successful copy preserves exact source bytes in isolated target");
                const auto targetInspection = SaveStore::inspectExistingSlot(successfulTarget, 0);
                check(targetInspection && targetInspection.value().info.fromBackup == false,
                    "copied primary remains loadable by the normal save inspector");
            }
            check(readBytes(sourcePrimary) == updatedPrimary && readBytes(sourceBackup) == updatedBackup,
                "cancelled, failed and successful copy attempts preserve source artifacts");
        }
    }

    const auto currentName = scene.find(playerId)->name;
    PuzzleLevelDefinition incompatibleIntro{"intro", "Changed rules", 4, 3, 3, 42, 300,
        {0,1,0,2,0,1,2,1,2,0,1,2}};
    check(static_cast<bool>(PuzzleLevelFile::write(temporaryRoot, incompatibleIntro)),
        "publish same level ID with changed authored board schema in isolated fixture");
    const auto rejectedSchema = loadPuzzleCampaignProgress(store, 0, scene, profile,
        "1.0.0", temporaryRoot, campaignDefinition.id);
    check(!rejectedSchema && rejectedSchema.error().code == astro::core::ErrorCode::VersionMismatch
        && scene.find(playerId)->name == currentName,
        "reject changed authored level schema without partial Scene restoration");

    std::filesystem::remove_all(temporaryRoot, filesystemError);
    check(!filesystemError, "remove only the isolated campaign save fixture");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
