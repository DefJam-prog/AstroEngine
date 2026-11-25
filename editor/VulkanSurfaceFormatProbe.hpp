#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct SDL_Window;

namespace astro::editor
{
    struct VulkanSurfaceFormatReport
    {
        std::string deviceName;
        std::string availableFormats;
        std::string selectedVkFormat;
        std::string selectedBgfxFormat;
        std::uint32_t nativeFormat = 0;
        std::uint32_t colorSpace = 0;
        std::uint32_t surfaceCurrentWidth = 0;
        std::uint32_t surfaceCurrentHeight = 0;
        std::uint32_t surfaceMinimumWidth = 0;
        std::uint32_t surfaceMinimumHeight = 0;
        std::uint32_t surfaceMaximumWidth = 0;
        std::uint32_t surfaceMaximumHeight = 0;
        bool surfaceCurrentExtentDefined = false;
    };

    [[nodiscard]] std::optional<VulkanSurfaceFormatReport> inspectVulkanSurfaceFormat(
        SDL_Window* window, std::uint16_t vendorId, std::uint16_t deviceId, std::string& error);
}
