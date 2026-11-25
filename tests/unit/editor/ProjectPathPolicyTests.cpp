#include "ProjectPathPolicy.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

namespace
{
int failures = 0;

void check(const bool condition, const std::string_view message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}
}

int main()
{
    const std::filesystem::path parent = std::filesystem::temp_directory_path() / "AstroEngine-parent";
    const auto resolved = astro::editor::resolveNewProjectDirectory(parent, "Космическая игра");
    check(resolved && resolved.value() == parent / std::filesystem::path(u8"Космическая игра"),
        "a selected parent and UTF-8 project name produce a new child path");
    check(!astro::editor::resolveNewProjectDirectory(parent, ""), "empty project names are rejected");
    check(!astro::editor::resolveNewProjectDirectory(parent, ".."), "parent traversal is rejected");
    check(!astro::editor::resolveNewProjectDirectory(parent, "child\\nested"),
        "backslash path separators are rejected");
    check(!astro::editor::resolveNewProjectDirectory(parent, "child/nested"),
        "slash path separators are rejected");
    check(!astro::editor::resolveNewProjectDirectory({}, "Game"), "empty parent paths are rejected");
    return failures == 0 ? 0 : 1;
}
