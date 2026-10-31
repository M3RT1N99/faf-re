// Engine addition (android/CLIENT_PLAN.md, C1): the Android half of the Vulkan backend's platform
// unit (VulkanSurface.h), in place of VulkanSurface.cpp. The window a head carries is the
// ANativeWindow of the activity's surface.

#include <android/native_window.h>

#define VK_USE_PLATFORM_ANDROID_KHR
#include "VulkanSurface.h"

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

  void WindowClientSize(void* const window, std::uint32_t& width, std::uint32_t& height)
  {
    auto* const nativeWindow = static_cast<ANativeWindow*>(window);
    const int32_t windowWidth = nativeWindow != nullptr ? ANativeWindow_getWidth(nativeWindow) : 0;
    const int32_t windowHeight = nativeWindow != nullptr ? ANativeWindow_getHeight(nativeWindow) : 0;
    width = static_cast<std::uint32_t>(windowWidth > 1 ? windowWidth : 1);
    height = static_cast<std::uint32_t>(windowHeight > 1 ? windowHeight : 1);
  }
} // namespace gpg::gal::vulkan
