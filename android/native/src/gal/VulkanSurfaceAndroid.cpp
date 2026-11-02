// The Android half of the Vulkan backend's platform unit (gpg/gal/backends/vulkan/VulkanSurface.h),
// in place of VulkanSurface.cpp, which knows Win32 windows. The window a head carries is a window
// of the message layer; its surface is the ANativeWindow the host gave its top-level window
// (include/win32/Win32AndroidHost.h).

#include <android/native_window.h>

#include <Win32AndroidHost.h>

#define VK_USE_PLATFORM_ANDROID_KHR
#include "gpg/gal/backends/vulkan/VulkanSurface.h"

namespace gpg::gal::vulkan
{
  const char* PlatformSurfaceExtension() noexcept
  {
    return VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
  }

  VkSurfaceKHR CreateWindowSurface(const VkInstance instance, void* const window, VkResult& result)
  {
    ANativeWindow* const surface = Win32Android_GetWindowSurface(static_cast<HWND>(window));
    if (surface == nullptr) {
      // The window is not on a surface (yet, or any more).
      result = VK_ERROR_SURFACE_LOST_KHR;
      return VK_NULL_HANDLE;
    }
    const auto create =
      reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(Vk().vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
    if (create == nullptr) {
      result = VK_ERROR_EXTENSION_NOT_PRESENT;
      return VK_NULL_HANDLE;
    }
    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = surface;
    VkSurfaceKHR vulkanSurface = VK_NULL_HANDLE;
    result = create(instance, &info, nullptr, &vulkanSurface);
    return (result == VK_SUCCESS) ? vulkanSurface : VK_NULL_HANDLE;
  }
} // namespace gpg::gal::vulkan
