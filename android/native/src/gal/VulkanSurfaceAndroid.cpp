// The Android half of the Vulkan backend's platform unit (gpg/gal/backends/vulkan/VulkanSurface.h), in
// place of VulkanSurface.cpp, whose POSIX half it keeps: the window is SDL's, and SDL makes its surface
// (SDL_Vulkan_CreateSurface).
//
// What Android adds: the window's surface is the activity's (SDL's ANativeWindow,
// SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER). It goes when the activity goes to the back, a new one comes
// when it is in front again, and the same surface can change its size. The patched DeviceVulkan.cpp
// (patches/README.md) lets go of a surface that is no longer current and takes the next once there is
// one. Between the two, the platform's swap chain (Android's own libvulkan, the same on every device)
// answers VK_ERROR_SURFACE_LOST_KHR for a surface whose window has gone, and draws nothing.
//
// The surface taken is held by a reference of its own, so that its size can be read while the activity
// changes it: SDL lets go of its own when the activity's surface goes.

#include <android/log.h>
#include <android/native_window.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

// Vulkan's Android surface names, before the backend's header includes Vulkan's (and SDL's after it).
#define VK_USE_PLATFORM_ANDROID_KHR
#include "gpg/gal/backends/vulkan/VulkanSurface.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

namespace platform::android
{
  // src/app/GameMainAndroid.cpp: the report hears of the first frame.
  void FirstFramePresented();
} // namespace platform::android

namespace
{
  /// The surface a VkSurfaceKHR was made from, with a reference of this unit's, and its size then.
  struct TakenSurface
  {
    SDL_Window* window = nullptr;
    ANativeWindow* surface = nullptr;
    std::int32_t width = 0;
    std::int32_t height = 0;
  };

  std::mutex gMutex;
  TakenSurface gTaken;
  /// The window of the last surface taken: the program has one (Android's SDL makes no other).
  SDL_Window* gWindow = nullptr;

  /// The activity's surface `window` is on now, or null while the activity has none.
  [[nodiscard]] ANativeWindow* SurfaceOf(SDL_Window* const window)
  {
    if (window == nullptr) {
      return nullptr;
    }
    return static_cast<ANativeWindow*>(
      SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)
    );
  }

  /// The program's window: the one whose surface was taken last, else SDL's first.
  [[nodiscard]] SDL_Window* ProgramWindow()
  {
    {
      std::lock_guard<std::mutex> lock(gMutex);
      if (gWindow != nullptr) {
        return gWindow;
      }
    }
    int count = 0;
    SDL_Window** const windows = SDL_GetWindows(&count);
    SDL_Window* const first = (windows != nullptr && count > 0) ? windows[0] : nullptr;
    SDL_free(windows);
    return first;
  }
} // namespace

namespace gpg::gal::vulkan
{
  const char* PlatformSurfaceExtension() noexcept
  {
    return VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
  }

  VkSurfaceKHR CreateWindowSurface(const VkInstance instance, void* const window, VkResult& result)
  {
    SDL_Window* const sdlWindow = static_cast<SDL_Window*>(window);
    if (sdlWindow == nullptr) {
      result = VK_ERROR_INITIALIZATION_FAILED;
      return VK_NULL_HANDLE;
    }
    ANativeWindow* const surface = SurfaceOf(sdlWindow);
    if (surface == nullptr) {
      // The activity is in the back: the window has no surface until it is in front again.
      result = VK_ERROR_SURFACE_LOST_KHR;
      return VK_NULL_HANDLE;
    }
    ANativeWindow_acquire(surface);
    VkSurfaceKHR vulkanSurface = VK_NULL_HANDLE;
    const bool created = SDL_Vulkan_CreateSurface(sdlWindow, instance, nullptr, &vulkanSurface);
    // SDL makes it from the surface it has then: one that came or went meanwhile is the next one to take.
    const bool moved = SurfaceOf(sdlWindow) != surface;
    if (!created || moved) {
      if (created) {
        Vk().vkDestroySurfaceKHR(instance, vulkanSurface, nullptr);
      } else {
        __android_log_print(ANDROID_LOG_WARN, "faf", "Vulkan: no surface for the window: %s", SDL_GetError());
      }
      ANativeWindow_release(surface);
      result = moved ? VK_ERROR_SURFACE_LOST_KHR : VK_ERROR_INITIALIZATION_FAILED;
      return VK_NULL_HANDLE;
    }
    std::lock_guard<std::mutex> lock(gMutex);
    if (gTaken.surface != nullptr) {
      ANativeWindow_release(gTaken.surface);
    }
    gTaken = TakenSurface{sdlWindow, surface, ANativeWindow_getWidth(surface), ANativeWindow_getHeight(surface)};
    gWindow = sdlWindow;
    result = VK_SUCCESS;
    return vulkanSurface;
  }

  // These are not in VulkanSurface.h: a Windows window's surface stays while the program runs. The
  // patched DeviceVulkan.cpp declares them.

  void DestroyWindowSurface(const VkInstance instance, const VkSurfaceKHR surface) noexcept
  {
    Vk().vkDestroySurfaceKHR(instance, surface, nullptr);
    std::lock_guard<std::mutex> lock(gMutex);
    if (gTaken.surface != nullptr) {
      ANativeWindow_release(gTaken.surface);
    }
    gTaken = TakenSurface{};
  }

  bool WindowSurfaceIsCurrent() noexcept
  {
    // Under the lock, the taken surface's reference keeps it while its size is read.
    std::lock_guard<std::mutex> lock(gMutex);
    return gTaken.surface != nullptr && SurfaceOf(gTaken.window) == gTaken.surface &&
      ANativeWindow_getWidth(gTaken.surface) == gTaken.width && ANativeWindow_getHeight(gTaken.surface) == gTaken.height;
  }

  void WaitForWindowSurface() noexcept
  {
    // The program can get here with the activity in the back. SDL takes the next surface on the
    // activity's thread, so nothing of the program's need run for it to come.
    while (SurfaceOf(ProgramWindow()) == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  void WindowSurfacePresented() noexcept
  {
    platform::android::FirstFramePresented();
  }
} // namespace gpg::gal::vulkan
