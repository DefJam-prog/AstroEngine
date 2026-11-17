#include "astro/runtime/PuzzleCampaign.hpp"

#include "astro/runtime/PuzzleLevel.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#else
#include <chrono>
#endif

namespace astro::runtime
{
namespace
{
constexpr std::string_view campaignExtension = ".astro-puzzle-campaign";

core::Error campaignError(const core::ErrorCode code, std::string message)
{
    return {code, std::move(message)};
}

bool validId(const std::string_view id)
{
    return !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](const unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '-' || c == '_';
    });
}

bool validUtf8(const std::string_view value)
{
    const auto continuation = [&value](const std::size_t index) {
        return index < value.size() && (static_cast<unsigned char>(value[index]) & 0xC0) == 0x80;
    };
    for (std::size_t i = 0; i < value.size();)
    {
        const auto lead = static_cast<unsigned char>(value[i]);
        if (lead <= 0x7F) { ++i; continue; }
        if (lead >= 0xC2 && lead <= 0xDF && continuation(i + 1)) { i += 2; continue; }
        if (lead >= 0xE0 && lead <= 0xEF && continuation(i + 1) && continuation(i + 2))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xE0 || second >= 0xA0) && (lead != 0xED || second < 0xA0)) { i += 3; continue; }
        }
        if (lead >= 0xF0 && lead <= 0xF4 && continuation(i + 1) && continuation(i + 2) && continuation(i + 3))
        {
            const auto second = static_cast<unsigned char>(value[i + 1]);
            if ((lead != 0xF0 || second >= 0x90) && (lead != 0xF4 || second <= 0x8F)) { i += 4; continue; }
        }
        return false;
    }
    return true;
}

bool validName(const std::string_view value)
{
    return !value.empty() && value.size() <= 256 && validUtf8(value)
        && std::none_of(value.begin(), value.end(), [](const unsigned char c) { return c < 0x20 || c == 0x7F; });
}

bool readWord(std::istringstream& line, std::string& value)
{
    line >> std::ws;
    return static_cast<bool>(line >> value);
}

bool readQuoted(std::istringstream& line, std::string& value)
{
    line >> std::ws;
    if (line.peek() != '"') return false;
    line.get();
    value.clear();
    char current = 0;
    while (line.get(current))
    {
        if (current == '"') return validUtf8(value);
        if (current == '\\')
        {
            char escaped = 0;
            if (!line.get(escaped) || (escaped != '\\' && escaped != '"')) return false;
            value.push_back(escaped);
            continue;
        }
        if (static_cast<unsigned char>(current) < 0x20 || current == 0x7F) return false;
        value.push_back(current);
    }
    return false;
}

bool readUnsigned(std::istringstream& line, std::uint64_t& value)
{
    std::string token;
    if (!readWord(line, token) || token.empty()) return false;
    if (!std::all_of(token.begin(), token.end(), [](const unsigned char c) { return c >= '0' && c <= '9'; }))
        return false;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value, 10);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

bool lineFinished(std::istringstream& line)
{
    line >> std::ws;
    return line.eof();
}

core::Result<void> validateReferences(const std::filesystem::path& projectRoot,
    const PuzzleCampaignDefinition& campaign)
{
    for (const auto& levelId : campaign.orderedLevelIds)
    {
        auto level = PuzzleLevelFile::read(projectRoot, levelId);
        if (!level)
            return core::Result<void>::failure(campaignError(level.error().code,
                "Campaign references unavailable Puzzle level '" + levelId + "': " + level.error().message));
    }
    return core::Result<void>::success();
}

bool remainsInsideProject(const std::filesystem::path& projectRoot, const std::filesystem::path& candidate)
{
    std::error_code ec;
    const auto canonicalRoot = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec) return false;
    const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return false;
    const auto relative = canonicalCandidate.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != std::filesystem::path("..");
}

core::Result<std::filesystem::path> publishAtomically(const std::filesystem::path& directory,
    const std::filesystem::path& destination, const std::string& contents)
{
    static std::atomic<std::uint64_t> sequence{0};
    std::filesystem::path temporary;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        const auto suffix = std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64())
            + L"." + std::to_wstring(sequence.fetch_add(1, std::memory_order_relaxed));
        temporary = directory / (L".campaign.tmp." + suffix);
        file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (file != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
                "Cannot create temporary Puzzle campaign file"));
    }
    if (file == INVALID_HANDLE_VALUE)
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
            "Could not reserve a unique temporary Puzzle campaign file"));
    DWORD written = 0;
    const bool writeOk = WriteFile(file, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr)
        && written == contents.size() && FlushFileBuffers(file);
    const auto writeError = writeOk ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!writeOk)
    {
        DeleteFileW(temporary.c_str());
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
            "Cannot write Puzzle campaign file (Windows error " + std::to_string(writeError) + ")"));
    }
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const auto error = GetLastError();
        DeleteFileW(temporary.c_str());
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
            "Cannot publish Puzzle campaign file (Windows error " + std::to_string(error) + ")"));
    }
#else
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    temporary = directory / (".campaign.tmp." + std::to_string(stamp) + "."
        + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::out);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output)
        {
            output.close();
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
            return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
                "Cannot write temporary Puzzle campaign file"));
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    if (ec)
    {
        const auto message = ec.message();
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
            "Cannot atomically publish Puzzle campaign file: " + message));
    }
#endif
    return core::Result<std::filesystem::path>::success(destination);
}

std::vector<std::string_view> splitLines(const std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t begin = 0;
    while (begin < text.size())
    {
        const auto end = text.find('\n', begin);
        const auto length = end == std::string_view::npos ? text.size() - begin : end - begin;
        auto line = text.substr(begin, length);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return lines;
}

core::Result<std::filesystem::path> campaignPath(const std::filesystem::path& projectRoot,
    const std::string_view campaignId)
{
    if (!validId(campaignId))
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign ID must use 1-64 ASCII letters, digits, hyphens or underscores"));
    std::error_code ec;
    if (!std::filesystem::is_directory(projectRoot, ec) || ec)
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
            "Puzzle campaigns require an existing project directory"));
    const auto directory = PuzzleCampaignFile::campaignsDirectory(projectRoot);
    const auto file = directory / (std::string(campaignId) + std::string(campaignExtension));
    if (!remainsInsideProject(projectRoot, directory) || !remainsInsideProject(projectRoot, file))
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign path resolves outside the project directory"));
    return core::Result<std::filesystem::path>::success(file);
}

core::Result<PuzzleCampaignDefinition> readCampaignDocument(const std::filesystem::path& projectRoot,
    const std::string& campaignId)
{
    auto resolved = campaignPath(projectRoot, campaignId);
    if (!resolved) return core::Result<PuzzleCampaignDefinition>::failure(resolved.error());
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(resolved.value(), ec);
    if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::IoError,
            "Puzzle campaign file is missing or is not a regular non-link file"));
    std::ifstream input(resolved.value(), std::ios::binary);
    if (!input)
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::IoError,
            "Cannot open Puzzle campaign file"));
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > maxPuzzleCampaignFileBytes)
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign file exceeds the 128 KiB limit"));
    input.seekg(0, std::ios::beg);
    std::string contents(static_cast<std::size_t>(length), '\0');
    if (!contents.empty() && !input.read(contents.data(), static_cast<std::streamsize>(contents.size())))
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::IoError,
            "Cannot read complete Puzzle campaign file"));
    auto parsed = parsePuzzleCampaign(contents);
    if (!parsed) return parsed;
    if (parsed.value().id != campaignId)
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign document ID does not match its filename"));
    return parsed;
}
}

core::Result<void> validatePuzzleCampaign(const PuzzleCampaignDefinition& campaign)
{
    if (!validId(campaign.id))
        return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign ID must use 1-64 ASCII letters, digits, hyphens or underscores"));
    if (!validName(campaign.name))
        return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign name must contain 1-256 valid UTF-8 bytes"));
    if (campaign.orderedLevelIds.empty() || campaign.orderedLevelIds.size() > maxPuzzleCampaignLevels)
        return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign must contain between 1 and 256 ordered levels"));
    if (!validId(campaign.startLevelId))
        return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign start level ID is invalid"));

    std::unordered_set<std::string> levelIds;
    levelIds.reserve(campaign.orderedLevelIds.size());
    for (const auto& levelId : campaign.orderedLevelIds)
    {
        if (!validId(levelId))
            return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
                "Puzzle campaign contains an invalid level ID"));
        if (!levelIds.emplace(levelId).second)
            return core::Result<void>::failure(campaignError(core::ErrorCode::AlreadyExists,
                "Puzzle campaign contains a duplicate level ID"));
    }
    if (!levelIds.contains(campaign.startLevelId))
        return core::Result<void>::failure(campaignError(core::ErrorCode::NotFound,
            "Puzzle campaign start level is not in the ordered level list"));
    if (campaign.transitions.size() < campaign.orderedLevelIds.size()
        || campaign.transitions.size() > maxPuzzleCampaignTransitions)
        return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign transition count must include at least one default per level and be at most 4096"));

    std::unordered_map<std::string, std::uint64_t> previousThreshold;
    std::unordered_set<std::string> defaults;
    std::unordered_set<std::string> defaultSeen;
    std::unordered_map<std::string, std::vector<std::string>> graph;
    for (const auto& transition : campaign.transitions)
    {
        if (!levelIds.contains(transition.sourceLevelId))
            return core::Result<void>::failure(campaignError(core::ErrorCode::NotFound,
                "Puzzle campaign transition source is not listed in the campaign"));
        if (defaultSeen.contains(transition.sourceLevelId))
            return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
                "A transition appears after its source level's DEFAULT rule"));

        if (transition.kind == PuzzleCampaignRuleKind::ScoreAtLeast)
        {
            if (!transition.targetLevelId || !validId(*transition.targetLevelId)
                || !levelIds.contains(*transition.targetLevelId) || transition.scoreThreshold == 0)
                return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
                    "SCORE_AT_LEAST requires a positive threshold and a target level in this campaign"));
            const auto previous = previousThreshold.find(transition.sourceLevelId);
            if (previous != previousThreshold.end() && transition.scoreThreshold >= previous->second)
                return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
                    "SCORE_AT_LEAST thresholds must be strictly descending for each source level"));
            previousThreshold[transition.sourceLevelId] = transition.scoreThreshold;
            graph[transition.sourceLevelId].push_back(*transition.targetLevelId);
        }
        else if (transition.kind == PuzzleCampaignRuleKind::Default)
        {
            if (transition.scoreThreshold != 0)
                return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
                    "DEFAULT rule cannot contain a score threshold"));
            if (!defaults.emplace(transition.sourceLevelId).second)
                return core::Result<void>::failure(campaignError(core::ErrorCode::AlreadyExists,
                    "Each campaign level may define only one DEFAULT rule"));
            defaultSeen.emplace(transition.sourceLevelId);
            if (transition.targetLevelId)
            {
                if (!validId(*transition.targetLevelId) || !levelIds.contains(*transition.targetLevelId))
                    return core::Result<void>::failure(campaignError(core::ErrorCode::NotFound,
                        "DEFAULT transition target is not listed in the campaign"));
                graph[transition.sourceLevelId].push_back(*transition.targetLevelId);
            }
        }
        else
            return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
                "Puzzle campaign contains an unknown transition condition"));
    }
    if (defaults.size() != levelIds.size())
        return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
            "Every campaign level must define exactly one DEFAULT rule"));

    std::unordered_set<std::string> reachable;
    std::vector<std::string> pending{campaign.startLevelId};
    reachable.emplace(campaign.startLevelId);
    for (std::size_t index = 0; index < pending.size(); ++index)
    {
        const auto found = graph.find(pending[index]);
        if (found == graph.end()) continue;
        for (const auto& target : found->second)
            if (reachable.emplace(target).second) pending.push_back(target);
    }
    if (reachable.size() != levelIds.size())
        return core::Result<void>::failure(campaignError(core::ErrorCode::ValidationError,
            "Every listed campaign level must be reachable from START"));
    return core::Result<void>::success();
}

core::Result<PuzzleCampaignDefinition> parsePuzzleCampaign(const std::string_view text)
{
    if (text.empty() || text.size() > maxPuzzleCampaignFileBytes)
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign file is empty or exceeds the 128 KiB limit"));
    if (text.starts_with("\xEF\xBB\xBF") || text.find('\0') != std::string_view::npos || !validUtf8(text))
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign must be valid UTF-8 without BOM or NUL bytes"));
    const auto lines = splitLines(text);
    if (lines.size() < 8)
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign is truncated"));
    const auto makeLine = [](const std::string_view raw) {
        std::istringstream stream{std::string(raw)};
        stream.imbue(std::locale::classic());
        return stream;
    };
    std::size_t cursor = 0;
    auto header = makeLine(lines[cursor++]);
    std::string tag;
    std::uint64_t version = 0;
    if (!readWord(header, tag) || tag != "ASTRO_PUZZLE_CAMPAIGN" || !readUnsigned(header, version)
        || !lineFinished(header))
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign header is malformed"));
    if (version != 1)
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::VersionMismatch,
            "Unsupported Puzzle campaign format version"));

    PuzzleCampaignDefinition campaign;
    auto parseNamedLine = [&](const std::string_view expectedTag, std::string& value) -> bool {
        if (cursor >= lines.size()) return false;
        auto line = makeLine(lines[cursor++]);
        std::string actualTag;
        return readWord(line, actualTag) && actualTag == expectedTag && readQuoted(line, value) && lineFinished(line);
    };
    if (!parseNamedLine("ID", campaign.id) || !parseNamedLine("NAME", campaign.name)
        || !parseNamedLine("START", campaign.startLevelId))
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign ID, NAME, or START record is malformed"));

    if (cursor >= lines.size())
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign LEVELS record is missing"));
    auto levelsLine = makeLine(lines[cursor++]);
    std::uint64_t levelCount = 0;
    if (!readWord(levelsLine, tag) || tag != "LEVELS" || !readUnsigned(levelsLine, levelCount)
        || !lineFinished(levelsLine) || levelCount == 0 || levelCount > maxPuzzleCampaignLevels)
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign LEVELS record is malformed or outside supported bounds"));
    campaign.orderedLevelIds.reserve(static_cast<std::size_t>(levelCount));
    for (std::uint64_t expectedIndex = 0; expectedIndex < levelCount; ++expectedIndex)
    {
        if (cursor >= lines.size())
            return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
                "Puzzle campaign level list is truncated"));
        auto line = makeLine(lines[cursor++]);
        std::uint64_t actualIndex = 0;
        std::string levelId;
        if (!readWord(line, tag) || tag != "LEVEL" || !readUnsigned(line, actualIndex)
            || actualIndex != expectedIndex || !readQuoted(line, levelId) || !lineFinished(line))
            return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
                "Puzzle campaign LEVEL records must have contiguous indices and one quoted ID each"));
        campaign.orderedLevelIds.push_back(std::move(levelId));
    }

    if (cursor >= lines.size())
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign TRANSITIONS record is missing"));
    auto transitionsLine = makeLine(lines[cursor++]);
    std::uint64_t transitionCount = 0;
    if (!readWord(transitionsLine, tag) || tag != "TRANSITIONS" || !readUnsigned(transitionsLine, transitionCount)
        || !lineFinished(transitionsLine) || transitionCount < levelCount
        || transitionCount > maxPuzzleCampaignTransitions)
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign TRANSITIONS count is malformed or outside supported bounds"));
    campaign.transitions.reserve(static_cast<std::size_t>(transitionCount));
    for (std::uint64_t i = 0; i < transitionCount; ++i)
    {
        if (cursor >= lines.size())
            return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
                "Puzzle campaign transition list is truncated"));
        auto line = makeLine(lines[cursor++]);
        PuzzleCampaignTransition transition;
        std::string condition;
        if (!readWord(line, tag) || tag != "TRANSITION" || !readQuoted(line, transition.sourceLevelId)
            || !readWord(line, condition))
            return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
                "Puzzle campaign TRANSITION record is malformed"));
        if (condition == "SCORE_AT_LEAST")
        {
            transition.kind = PuzzleCampaignRuleKind::ScoreAtLeast;
            if (!readUnsigned(line, transition.scoreThreshold) || !readQuoted(line, condition)
                || !lineFinished(line))
                return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
                    "SCORE_AT_LEAST requires an unsigned threshold and quoted target ID"));
            transition.targetLevelId = std::move(condition);
        }
        else if (condition == "DEFAULT")
        {
            transition.kind = PuzzleCampaignRuleKind::Default;
            line >> std::ws;
            if (line.peek() == '"')
            {
                std::string target;
                if (!readQuoted(line, target) || !lineFinished(line))
                    return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
                        "DEFAULT target ID is malformed"));
                transition.targetLevelId = std::move(target);
            }
            else
            {
                std::string terminal;
                if (!readWord(line, terminal) || terminal != "END" || !lineFinished(line))
                    return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
                        "DEFAULT target must be a quoted level ID or the unquoted token END"));
            }
        }
        else
            return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ValidationError,
                "Unknown Puzzle campaign transition condition: " + condition));
        campaign.transitions.push_back(std::move(transition));
    }
    if (cursor >= lines.size() || lines[cursor++] != "END" || cursor != lines.size())
        return core::Result<PuzzleCampaignDefinition>::failure(campaignError(core::ErrorCode::ParseError,
            "Puzzle campaign terminator is missing or trailing records are present"));

    auto valid = validatePuzzleCampaign(campaign);
    if (!valid) return core::Result<PuzzleCampaignDefinition>::failure(valid.error());
    return core::Result<PuzzleCampaignDefinition>::success(std::move(campaign));
}

core::Result<std::string> serializePuzzleCampaign(const PuzzleCampaignDefinition& campaign)
{
    auto valid = validatePuzzleCampaign(campaign);
    if (!valid) return core::Result<std::string>::failure(valid.error());
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "ASTRO_PUZZLE_CAMPAIGN 1\n"
           << "ID " << std::quoted(campaign.id) << '\n'
           << "NAME " << std::quoted(campaign.name) << '\n'
           << "START " << std::quoted(campaign.startLevelId) << '\n'
           << "LEVELS " << campaign.orderedLevelIds.size() << '\n';
    for (std::size_t index = 0; index < campaign.orderedLevelIds.size(); ++index)
        output << "LEVEL " << index << ' ' << std::quoted(campaign.orderedLevelIds[index]) << '\n';
    output << "TRANSITIONS " << campaign.transitions.size() << '\n';
    for (const auto& transition : campaign.transitions)
    {
        output << "TRANSITION " << std::quoted(transition.sourceLevelId) << ' ';
        if (transition.kind == PuzzleCampaignRuleKind::ScoreAtLeast)
            output << "SCORE_AT_LEAST " << transition.scoreThreshold << ' '
                   << std::quoted(*transition.targetLevelId);
        else
        {
            output << "DEFAULT ";
            if (transition.targetLevelId) output << std::quoted(*transition.targetLevelId);
            else output << "END";
        }
        output << '\n';
    }
    output << "END\n";
    if (!output)
        return core::Result<std::string>::failure(campaignError(core::ErrorCode::InternalError,
            "Could not serialize Puzzle campaign"));
    auto text = output.str();
    if (text.size() > maxPuzzleCampaignFileBytes)
        return core::Result<std::string>::failure(campaignError(core::ErrorCode::ValidationError,
            "Serialized Puzzle campaign exceeds the 128 KiB limit"));
    return core::Result<std::string>::success(std::move(text));
}

core::Result<std::optional<std::string>> resolvePuzzleCampaignTransition(
    const PuzzleCampaignDefinition& campaign, const std::string_view completedLevelId, const std::uint64_t finalScore)
{
    auto valid = validatePuzzleCampaign(campaign);
    if (!valid)
        return core::Result<std::optional<std::string>>::failure(valid.error());
    if (!std::any_of(campaign.orderedLevelIds.begin(), campaign.orderedLevelIds.end(),
            [completedLevelId](const std::string& id) { return id == completedLevelId; }))
        return core::Result<std::optional<std::string>>::failure(campaignError(core::ErrorCode::NotFound,
            "Completed Puzzle level is not listed in this campaign"));

    for (const auto& transition : campaign.transitions)
    {
        if (transition.sourceLevelId != completedLevelId) continue;
        if (transition.kind == PuzzleCampaignRuleKind::ScoreAtLeast && finalScore >= transition.scoreThreshold)
            return core::Result<std::optional<std::string>>::success(transition.targetLevelId);
        if (transition.kind == PuzzleCampaignRuleKind::Default)
            return core::Result<std::optional<std::string>>::success(transition.targetLevelId);
    }
    return core::Result<std::optional<std::string>>::failure(campaignError(core::ErrorCode::InternalError,
        "Validated Puzzle campaign has no matching transition or DEFAULT rule"));
}

std::filesystem::path PuzzleCampaignFile::campaignsDirectory(const std::filesystem::path& projectRoot)
{
    return projectRoot / L"Puzzle" / L"Campaigns";
}

core::Result<PuzzleCampaignDefinition> PuzzleCampaignFile::read(const std::filesystem::path& projectRoot,
    const std::string& campaignId)
{
    auto parsed = readCampaignDocument(projectRoot, campaignId);
    if (!parsed) return parsed;
    auto references = validateReferences(projectRoot, parsed.value());
    if (!references) return core::Result<PuzzleCampaignDefinition>::failure(references.error());
    return parsed;
}

bool PuzzleCampaignCatalogEntry::referencesValid() const noexcept
{
    return definition.has_value() && std::all_of(levelReferences.begin(), levelReferences.end(),
        [](const PuzzleCampaignLevelReference& reference) { return reference.available; });
}

core::Result<std::vector<PuzzleCampaignCatalogEntry>> PuzzleCampaignFile::list(
    const std::filesystem::path& projectRoot)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(projectRoot, ec) || ec)
        return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::failure(campaignError(
            core::ErrorCode::IoError, "Puzzle campaign catalog requires an existing project directory"));
    const auto directory = campaignsDirectory(projectRoot);
    if (!std::filesystem::exists(directory, ec))
    {
        if (ec) return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::failure(campaignError(
            core::ErrorCode::IoError, "Cannot inspect Puzzle campaigns directory: " + ec.message()));
        return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::success({});
    }
    const auto directoryStatus = std::filesystem::symlink_status(directory, ec);
    if (ec || std::filesystem::is_symlink(directoryStatus) || !std::filesystem::is_directory(directoryStatus)
        || !remainsInsideProject(projectRoot, directory))
        return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::failure(campaignError(
            core::ErrorCode::ValidationError, "Puzzle campaigns directory is invalid or resolves outside the project"));

    std::vector<PuzzleCampaignCatalogEntry> entries;
    std::filesystem::directory_iterator current(directory, ec), end;
    if (ec) return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::failure(campaignError(
        core::ErrorCode::IoError, "Cannot enumerate Puzzle campaigns directory: " + ec.message()));
    while (current != end)
    {
        const auto& path = current->path();
        if (path.extension() == campaignExtension)
        {
            if (entries.size() >= 4096)
                return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::failure(campaignError(
                    core::ErrorCode::ValidationError, "Puzzle campaign catalog exceeds the 4096-file limit"));
            const auto stem = path.stem().u8string();
            const std::string id(reinterpret_cast<const char*>(stem.data()), stem.size());
            if (!validId(id))
                entries.push_back({id, std::nullopt, "Filename does not use a safe Puzzle campaign ID", {}});
            else
            {
                const auto status = current->symlink_status(ec);
                if (ec)
                    return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::failure(campaignError(
                        core::ErrorCode::IoError, "Cannot inspect Puzzle campaign catalog entry: " + ec.message()));
                if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
                    entries.push_back({id, std::nullopt, "Catalog entry is not a regular non-link file", {}});
                else
                {
                    auto loaded = readCampaignDocument(projectRoot, id);
                    if (!loaded) entries.push_back({id, std::nullopt, loaded.error().message, {}});
                    else
                    {
                        PuzzleCampaignCatalogEntry entry{id, std::move(loaded).value(), {}, {}};
                        for (const auto& levelId : entry.definition->orderedLevelIds)
                        {
                            const auto level = PuzzleLevelFile::read(projectRoot, levelId);
                            if (level) entry.levelReferences.push_back({levelId, true, {}});
                            else entry.levelReferences.push_back({levelId, false, level.error().message});
                        }
                        entries.push_back(std::move(entry));
                    }
                }
            }
        }
        current.increment(ec);
        if (ec) return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::failure(campaignError(
            core::ErrorCode::IoError, "Failed while enumerating Puzzle campaigns directory: " + ec.message()));
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return core::Result<std::vector<PuzzleCampaignCatalogEntry>>::success(std::move(entries));
}

core::Result<std::filesystem::path> PuzzleCampaignFile::write(const std::filesystem::path& projectRoot,
    const PuzzleCampaignDefinition& campaign)
{
    auto valid = validatePuzzleCampaign(campaign);
    if (!valid) return core::Result<std::filesystem::path>::failure(valid.error());
    auto resolved = campaignPath(projectRoot, campaign.id);
    if (!resolved) return resolved;
    auto references = validateReferences(projectRoot, campaign);
    if (!references) return core::Result<std::filesystem::path>::failure(references.error());
    auto contents = serializePuzzleCampaign(campaign);
    if (!contents) return core::Result<std::filesystem::path>::failure(contents.error());

    const auto directory = campaignsDirectory(projectRoot);
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec || !remainsInsideProject(projectRoot, directory))
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
            "Cannot create or validate project Puzzle/Campaigns directory"));
    const auto status = std::filesystem::symlink_status(resolved.value(), ec);
    if (!ec && std::filesystem::exists(status)
        && (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)))
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::ValidationError,
            "Puzzle campaign destination is not a regular non-link file"));
    if (ec && ec != std::errc::no_such_file_or_directory)
        return core::Result<std::filesystem::path>::failure(campaignError(core::ErrorCode::IoError,
            "Cannot inspect Puzzle campaign destination: " + ec.message()));
    return publishAtomically(directory, resolved.value(), contents.value());
}
}
