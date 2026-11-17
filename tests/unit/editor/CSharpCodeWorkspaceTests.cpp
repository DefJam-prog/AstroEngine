#include "CSharpCodeWorkspace.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
int failures{};
void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main()
{
    using astro::editor::CSharpCodeWorkspace;
    const auto root = std::filesystem::temp_directory_path()
        / ("astro-code-workspace-" + std::to_string(static_cast<long long>(
            std::filesystem::file_time_type::clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(root / "Scripts" / "Игрок");
    {
        std::ofstream source(root / "Scripts" / "Игрок" / "Движение.cs", std::ios::binary);
        source << "// Привет, мир\nclass Player {}\n";
    }

    CSharpCodeWorkspace workspace;
    check(static_cast<bool>(workspace.open(root, std::filesystem::path("Игрок") / "Движение.cs")),
        "opens UTF-8 C# source under Scripts");
    check(workspace.text().find("Привет, мир") != std::string::npos && !workspace.dirty(),
        "preserves UTF-8 and begins clean");
    workspace.setText(workspace.text() + "// changed\n");
    check(workspace.dirty(), "tracks unsaved edits");
    check(static_cast<bool>(workspace.save()) && !workspace.dirty(), "saves and clears dirty state");
    CSharpCodeWorkspace reopened;
    check(static_cast<bool>(reopened.open(root, std::filesystem::path("Игрок") / "Движение.cs"))
        && reopened.text().find("// changed") != std::string::npos, "reopens saved content");
    check(!workspace.open(root, ".. /outside.cs"), "rejects malformed external path");
    check(!workspace.open(root, "../outside.cs"), "rejects traversal");
    check(!workspace.open(root, "readme.txt"), "rejects non-C# file");
    check(static_cast<bool>(workspace.create(root, std::filesystem::path("Новый") / "Скрипт.cs")),
        "creates a new UTF-8 C# source file");
    check(std::filesystem::exists(root / "Scripts" / "Новый" / "Скрипт.cs"), "creates nested file under Scripts");
    check(!workspace.create(root, std::filesystem::path("Новый") / "Скрипт.cs"), "refuses to replace an existing script");
    {
        std::ofstream invalid(root / "Scripts" / "invalid.cs", std::ios::binary);
        invalid.put(static_cast<char>(0xff));
    }
    check(!workspace.open(root, "invalid.cs"), "rejects invalid UTF-8 source bytes");
    workspace.setText(std::string(4 * 1024 * 1024 + 1, 'x'));
    check(!workspace.save(), "rejects source larger than the bounded editor buffer policy");

    const auto diagnostics = CSharpCodeWorkspace::parseDiagnostics(
        "  /_/AstroEngine/Scripts/Игрок/Движение.cs(12,4): error CS1002: ; expected [Project.csproj]\r\n"
        "  /_/AstroEngine/Scripts/Игрок/Движение.cs(15,2): warning CS0168: variable declared [Project.csproj]\n");
    check(diagnostics.size() == 2, "parses compiler errors and warnings");
    if (diagnostics.size() == 2)
    {
        check(diagnostics[0].sourcePath.find("Scripts/Игрок/Движение.cs") != std::string::npos
            && diagnostics[0].line == 12 && diagnostics[0].column == 4
            && diagnostics[0].severity == "error" && diagnostics[0].code == "CS1002",
            "maps deterministic compiler path and source location");
        check(diagnostics[1].line == 15 && diagnostics[1].severity == "warning", "retains warning location");
    }

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return failures == 0 ? 0 : 1;
}
