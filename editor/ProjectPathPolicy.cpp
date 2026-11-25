#include "ProjectPathPolicy.hpp"

namespace astro::editor
{
astro::core::Result<std::filesystem::path> resolveNewProjectDirectory(
    const std::filesystem::path& parentDirectory, const std::string_view projectName)
{
    using astro::core::ErrorCode;
    using astro::core::Result;

    const bool invalidName = projectName.empty() || projectName == "." || projectName == ".."
        || projectName.find('/') != std::string_view::npos
        || projectName.find('\\') != std::string_view::npos;
    if (parentDirectory.empty() || invalidName)
        return Result<std::filesystem::path>::failure({ErrorCode::InvalidArgument,
            "A parent directory and a single-component project name are required"});

    try
    {
        const std::u8string utf8Name(reinterpret_cast<const char8_t*>(projectName.data()), projectName.size());
        const std::filesystem::path nameComponent(utf8Name);
        if (nameComponent.has_root_path() || nameComponent.has_parent_path()
            || nameComponent.filename() != nameComponent)
            return Result<std::filesystem::path>::failure({ErrorCode::InvalidArgument,
                "Project name must be a single directory name"});
        return Result<std::filesystem::path>::success(parentDirectory / nameComponent);
    }
    catch (const std::filesystem::filesystem_error&)
    {
        return Result<std::filesystem::path>::failure({ErrorCode::InvalidArgument,
            "Project name is not a valid UTF-8 directory name"});
    }
}
}
