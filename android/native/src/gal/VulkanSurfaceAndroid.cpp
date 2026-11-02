// The Android half of the Vulkan backend's platform unit (gpg/gal/backends/vulkan/VulkanSurface.h),
// in place of VulkanSurface.cpp, which knows Win32 windows. The window a head carries is the
// ANativeWindow of the activity's surface.

#include <android/native_window.h>

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
    const auto create =
      reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(Vk().vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
    if (create == nullptr) {
      result = VK_ERROR_EXTENSION_NOT_PRESENT;
      return VK_NULL_HANDLE;
    }
    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = static_cast<ANativeWindow*>(window);
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    result = create(instance, &info, nullptr, &surface);
    return (result == VK_SUCCESS) ? surface : VK_NULL_HANDLE;
  }
} // namespace gpg::gal::vulkan
