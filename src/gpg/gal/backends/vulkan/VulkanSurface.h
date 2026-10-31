#pragma once

// FAF addition, not in the shipped binary.
//
// The platform half of the Vulkan backend: the instance extension that makes
// window surfaces and the surface for one window. Only this unit knows the
// window system (VK_KHR_win32_surface); another platform replaces it alone.

#include <cstdint>

#include "VulkanLoader.h"

namespace gpg::gal::vulkan
{
  /** The platform's surface extension name. */
  [[nodiscard]] const char* PlatformSurfaceExtension() noexcept;

  /** Creates a surface for `window` (an HWND on Windows); VK_NULL_HANDLE on failure, `result` says why. */
  [[nodiscard]] VkSurfaceKHR CreateWindowSurface(VkInstance instance, void* window, VkResult& result);

  /** The size of `window`'s client area in pixels, at least 1x1: the swap chain's when a head gives none. */
  void WindowClientSize(void* window, std::uint32_t& width, std::uint32_t& height);
} // namespace gpg::gal::vulkan
