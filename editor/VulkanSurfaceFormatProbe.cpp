#define VK_USE_PLATFORM_WIN32_KHR 1
#include <windows.h>
#include <vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "VulkanSurfaceFormatProbe.hpp"

#include <array>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace astro::editor
{
    namespace
    {
        const char* vkFormatName(const VkFormat format) noexcept
        {
            switch (format)
            {
            case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
            case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
            case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
            case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
            default: return "VK_FORMAT_OTHER";
            }
        }

        struct VulkanSurfaceLifetime
        {
            bool libraryLoaded = false;
            VkInstance instance = VK_NULL_HANDLE;
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            PFN_vkDestroySurfaceKHR destroySurface = nullptr;
            PFN_vkDestroyInstance destroyInstance = nullptr;

            ~VulkanSurfaceLifetime()
            {
                if (surface != VK_NULL_HANDLE && destroySurface)
                    destroySurface(instance, surface, nullptr);
                if (instance != VK_NULL_HANDLE && destroyInstance)
                    destroyInstance(instance, nullptr);
                if (libraryLoaded) SDL_Vulkan_UnloadLibrary();
            }
        };
    }

    std::optional<VulkanSurfaceFormatReport> inspectVulkanSurfaceFormat(SDL_Window* window,
        const std::uint16_t vendorId, const std::uint16_t deviceId, std::string& error)
    {
        if (!window)
        {
            error = "SDL window is unavailable";
            return std::nullopt;
        }

        VulkanSurfaceLifetime lifetime;
        if (!SDL_Vulkan_LoadLibrary(nullptr))
        {
            error = std::string("SDL could not load the Vulkan loader: ") + SDL_GetError();
            return std::nullopt;
        }
        lifetime.libraryLoaded = true;
        const auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            SDL_Vulkan_GetVkGetInstanceProcAddr());
        if (!getInstanceProcAddr)
        {
            error = std::string("SDL did not expose vkGetInstanceProcAddr: ") + SDL_GetError();
            return std::nullopt;
        }
        const auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
            getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!createInstance)
        {
            error = "Vulkan loader does not expose vkCreateInstance";
            return std::nullopt;
        }

        Uint32 extensionCount = 0;
        const auto* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if (!extensions || extensionCount == 0)
        {
            error = std::string("SDL could not list Vulkan surface extensions: ") + SDL_GetError();
            return std::nullopt;
        }
        VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        applicationInfo.pApplicationName = "AstroEngine Vulkan format probe";
        applicationInfo.applicationVersion = 1;
        applicationInfo.pEngineName = "AstroEngine";
        applicationInfo.engineVersion = 1;
        applicationInfo.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        instanceInfo.pApplicationInfo = &applicationInfo;
        instanceInfo.enabledExtensionCount = extensionCount;
        instanceInfo.ppEnabledExtensionNames = extensions;
        if (createInstance(&instanceInfo, nullptr, &lifetime.instance) != VK_SUCCESS)
        {
            error = "vkCreateInstance failed for the diagnostic surface query";
            return std::nullopt;
        }

        const auto instanceFunction = [&](const char* name) {
            return getInstanceProcAddr(lifetime.instance, name);
        };
        const auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(instanceFunction("vkDestroyInstance"));
        const auto enumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
            instanceFunction("vkEnumeratePhysicalDevices"));
        const auto getPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            instanceFunction("vkGetPhysicalDeviceProperties"));
        const auto createWin32Surface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
            instanceFunction("vkCreateWin32SurfaceKHR"));
        lifetime.destroySurface = reinterpret_cast<PFN_vkDestroySurfaceKHR>(instanceFunction("vkDestroySurfaceKHR"));
        lifetime.destroyInstance = destroyInstance;
        if (!destroyInstance || !enumeratePhysicalDevices || !getPhysicalDeviceProperties
            || !createWin32Surface || !lifetime.destroySurface)
        {
            error = "Vulkan surface-query functions are unavailable from the loader";
            return std::nullopt;
        }

        const auto nativeWindow = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!nativeWindow)
        {
            error = "SDL did not expose the Win32 HWND for Vulkan surface inspection";
            return std::nullopt;
        }
        VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        surfaceInfo.hinstance = GetModuleHandleW(nullptr);
        surfaceInfo.hwnd = nativeWindow;
        if (createWin32Surface(lifetime.instance, &surfaceInfo, nullptr, &lifetime.surface) != VK_SUCCESS)
        {
            error = "vkCreateWin32SurfaceKHR failed for the Editor HWND";
            return std::nullopt;
        }

        const auto getSurfaceFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
            instanceFunction("vkGetPhysicalDeviceSurfaceFormatsKHR"));
        const auto getSurfaceCapabilities = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
            instanceFunction("vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
        if (!getSurfaceFormats || !getSurfaceCapabilities)
        {
            error = "Vulkan loader does not expose the required surface query functions";
            return std::nullopt;
        }
        std::uint32_t physicalDeviceCount = 0;
        if (enumeratePhysicalDevices(lifetime.instance, &physicalDeviceCount, nullptr) != VK_SUCCESS
            || physicalDeviceCount == 0)
        {
            error = "Vulkan reported no physical devices for the Editor surface";
            return std::nullopt;
        }
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        if (enumeratePhysicalDevices(lifetime.instance, &physicalDeviceCount, physicalDevices.data()) != VK_SUCCESS)
        {
            error = "Vulkan physical-device enumeration failed";
            return std::nullopt;
        }

        VkPhysicalDevice matchingDevice = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties matchingProperties{};
        for (const auto device : physicalDevices)
        {
            VkPhysicalDeviceProperties properties{};
            getPhysicalDeviceProperties(device, &properties);
            if (properties.vendorID == vendorId && properties.deviceID == deviceId)
            {
                matchingDevice = device;
                matchingProperties = properties;
                break;
            }
        }
        if (matchingDevice == VK_NULL_HANDLE)
        {
            error = "Vulkan did not find the same vendor/device selected by BGFX";
            return std::nullopt;
        }

        VkSurfaceCapabilitiesKHR surfaceCapabilities{};
        if (getSurfaceCapabilities(matchingDevice, lifetime.surface, &surfaceCapabilities) != VK_SUCCESS)
        {
            error = "Vulkan surface-capabilities query failed";
            return std::nullopt;
        }

        std::uint32_t formatCount = 0;
        if (getSurfaceFormats(matchingDevice, lifetime.surface, &formatCount, nullptr) != VK_SUCCESS
            || formatCount == 0)
        {
            error = "Vulkan reported no surface formats for the Editor HWND";
            return std::nullopt;
        }
        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        if (getSurfaceFormats(matchingDevice, lifetime.surface, &formatCount, surfaceFormats.data()) != VK_SUCCESS)
        {
            error = "Vulkan surface-format query failed";
            return std::nullopt;
        }

        std::ostringstream available;
        bool first = true;
        for (const auto& format : surfaceFormats)
        {
            if (format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) continue;
            if (!first) available << ',';
            first = false;
            available << vkFormatName(format.format) << '(' << static_cast<int>(format.format) << ')';
        }

        // Match BGFX's current SwapChainVK::findSurfaceFormat preference for
        // formatColor=BGRA8 and sRGB disabled: BGRA8_UNORM first, then RGBA8_UNORM.
        constexpr std::array requestedFormats{
            std::pair{VK_FORMAT_B8G8R8A8_UNORM, "BGRA8"},
            std::pair{VK_FORMAT_R8G8B8A8_UNORM, "RGBA8"}
        };
        for (const auto& requested : requestedFormats)
        {
            for (const auto& surfaceFormat : surfaceFormats)
            {
                if (surfaceFormat.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
                    || surfaceFormat.format != requested.first) continue;
                return VulkanSurfaceFormatReport{
                    matchingProperties.deviceName,
                    available.str(),
                    vkFormatName(surfaceFormat.format),
                    requested.second,
                    static_cast<std::uint32_t>(surfaceFormat.format),
                    static_cast<std::uint32_t>(surfaceFormat.colorSpace),
                    surfaceCapabilities.currentExtent.width,
                    surfaceCapabilities.currentExtent.height,
                    surfaceCapabilities.minImageExtent.width,
                    surfaceCapabilities.minImageExtent.height,
                    surfaceCapabilities.maxImageExtent.width,
                    surfaceCapabilities.maxImageExtent.height,
                    surfaceCapabilities.currentExtent.width != (std::numeric_limits<std::uint32_t>::max)()
                        && surfaceCapabilities.currentExtent.height != (std::numeric_limits<std::uint32_t>::max)()
                };
            }
        }

        error = "No BGFX-preferred BGRA8/RGBA8 UNORM surface format is available";
        return std::nullopt;
    }
}
