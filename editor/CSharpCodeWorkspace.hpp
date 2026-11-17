#pragma once

#include "astro/core/Result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace astro::editor
{
struct CSharpDiagnostic final
{
    std::string sourcePath;
    int line{};
    int column{};
    std::string severity;
    std::string code;
    std::string message;
};

class CSharpCodeWorkspace final
{
public:
    [[nodiscard]] core::Result<void> open(const std::filesystem::path& projectRoot,
        const std::filesystem::path& relativeScript);
    [[nodiscard]] core::Result<void> create(const std::filesystem::path& projectRoot,
        const std::filesystem::path& relativeScript);
    [[nodiscard]] core::Result<void> save();
    [[nodiscard]] core::Result<std::filesystem::path> resolve(const std::filesystem::path& projectRoot,
        const std::filesystem::path& relativeScript, bool allowMissingLeaf) const;

    void setText(std::string text);
    [[nodiscard]] const std::string& text() const noexcept { return text_; }
    [[nodiscard]] std::string& mutableText() noexcept { return text_; }
    [[nodiscard]] const std::string& savedText() const noexcept { return savedText_; }
    [[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return projectRoot_; }
    [[nodiscard]] const std::filesystem::path& relativeScript() const noexcept { return relativeScript_; }
    [[nodiscard]] bool dirty() const noexcept { return text_ != savedText_; }

    [[nodiscard]] static std::vector<CSharpDiagnostic> parseDiagnostics(const std::string& output);
private:
    std::filesystem::path projectRoot_;
    std::filesystem::path relativeScript_;
    std::string text_;
    std::string savedText_;
};
}
