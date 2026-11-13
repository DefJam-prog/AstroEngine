#include "astro/core/ConfigurationService.hpp"

#include <fstream>
#include <mutex>
#include <string>

namespace astro::core
{
namespace
{
std::string_view trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
}

Result<void> ConfigurationService::load(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return Result<void>::failure({ErrorCode::IoError, "Cannot open configuration file: " + path.string()});

    std::map<std::string, std::string, std::less<>> parsed;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line))
    {
        ++lineNumber;
        if (lineNumber == 1 && line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
        const auto content = trim(line);
        if (content.empty() || content.starts_with('#') || content.starts_with(';')) continue;
        const auto separator = content.find('=');
        if (separator == std::string_view::npos)
            return Result<void>::failure({ErrorCode::ParseError, "Expected key=value at line " + std::to_string(lineNumber)});
        const auto key = trim(content.substr(0, separator));
        const auto value = trim(content.substr(separator + 1));
        if (key.empty()) return Result<void>::failure({ErrorCode::ParseError, "Empty key at line " + std::to_string(lineNumber)});
        if (!parsed.emplace(key, value).second)
            return Result<void>::failure({ErrorCode::AlreadyExists, "Duplicate key at line " + std::to_string(lineNumber)});
    }
    if (input.bad()) return Result<void>::failure({ErrorCode::IoError, "Failed while reading configuration file: " + path.string()});
    {
        std::unique_lock lock(mutex_);
        values_ = std::move(parsed);
    }
    return Result<void>::success();
}

Result<void> ConfigurationService::save(const std::filesystem::path& path) const
{
    std::map<std::string, std::string, std::less<>> snapshot;
    {
        std::shared_lock lock(mutex_);
        snapshot = values_;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return Result<void>::failure({ErrorCode::IoError, "Cannot write configuration file: " + path.string()});
    output << "# AstroEngine configuration (UTF-8)\n";
    for (const auto& [key, value] : snapshot) output << key << '=' << value << '\n';
    if (!output) return Result<void>::failure({ErrorCode::IoError, "Failed while writing configuration file: " + path.string()});
    return Result<void>::success();
}

Result<void> ConfigurationService::set(std::string key, std::string value)
{
    if (key.empty()) return Result<void>::failure({ErrorCode::InvalidArgument, "Configuration key cannot be empty"});
    if (key.find_first_of("=\r\n") != std::string::npos)
        return Result<void>::failure({ErrorCode::InvalidArgument, "Configuration key contains a reserved character"});
    if (value.find_first_of("\r\n") != std::string::npos)
        return Result<void>::failure({ErrorCode::InvalidArgument, "Configuration value cannot contain a newline"});
    std::unique_lock lock(mutex_);
    values_.insert_or_assign(std::move(key), std::move(value));
    return Result<void>::success();
}

Result<std::string> ConfigurationService::get(std::string_view key) const
{
    std::shared_lock lock(mutex_);
    const auto found = values_.find(key);
    if (found == values_.end()) return Result<std::string>::failure({ErrorCode::NotFound, "Configuration key not found: " + std::string(key)});
    return Result<std::string>::success(found->second);
}

bool ConfigurationService::contains(std::string_view key) const
{
    std::shared_lock lock(mutex_);
    return values_.contains(key);
}
}
