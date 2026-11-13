#pragma once

#include "astro/core/Result.hpp"

#include <filesystem>
#include <map>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace astro::core
{
class ConfigurationService
{
public:
    Result<void> load(const std::filesystem::path& path);
    Result<void> save(const std::filesystem::path& path) const;
    Result<void> set(std::string key, std::string value);
    [[nodiscard]] Result<std::string> get(std::string_view key) const;
    [[nodiscard]] bool contains(std::string_view key) const;

private:
    mutable std::shared_mutex mutex_;
    std::map<std::string, std::string, std::less<>> values_;
};
}
