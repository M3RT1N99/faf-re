// The renderer test (android/native, card 4 of the app): the engine's renderer on the phone.
// The activity hands over its surface, the screen of the message layer
// (include/win32/Win32AndroidHost.h). On a thread of its own the test makes a window on it, as the
// game makes its frame, and the engine's gal creates its Vulkan device for that window
// (gal::Device::Create, the backend Windows uses). It then clears the frame in changing colours
// and presents it for a few seconds, timed, while the thread takes its messages as the game's loop
// does. No effects and no textures: those need the shader compiler and the texture codec of
// CLIENT_PLAN.md's C2. What the device said, the engine's log lines included, is the report the
// app shows and shares. Each line goes to the report file as it comes, so the file holds what the
// test got to if the process dies in the driver.

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <fcntl.h>
#include <jni.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>
#include <Win32AndroidHost.h>

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"

namespace
{
  std::mutex gLock;
  std::string gReport;
  int gReportFile = -1;
  std::thread gThread;
  std::atomic<bool> gStop{false};
  std::atomic<bool> gRunning{false};

  void Report(std::string line)
  {
    // The report goes to Java as modified UTF-8: plain ASCII keeps it valid whatever a log line holds.
    for (char& c : line) {
      if (static_cast<unsigned char>(c) >= 0x80u || c == '\0') {
        c = '?';
      }
    }
    __android_log_print(ANDROID_LOG_INFO, "fafre-gal", "%s", line.c_str());
    line += '\n';
    const std::lock_guard<std::mutex> lock(gLock);
    gReport += line;
    if (gReportFile >= 0) {
      // Unbuffered: the kernel has the line before the next driver call.
      const ssize_t written = ::write(gReportFile, line.data(), line.size());
      (void)written;
    }
  }

  void CloseReportFile()
  {
    const std::lock_guard<std::mutex> lock(gLock);
    if (gReportFile >= 0) {
      ::close(gReportFile);
      gReportFile = -1;
    }
  }

  /** The engine's log lines (gpg::Logf, Warnf) into the report. */
  class ReportLogTarget final : public gpg::LogTarget
  {
  public:
    ReportLogTarget()
      : gpg::LogTarget(true)
    {}

    void OnMessage(
      gpg::LogSeverity, const msvc8::string& message, const msvc8::vector<msvc8::string>&, int
    ) override
    {
      Report(std::string("log: ") + message.c_str());
    }
  };

  /** An ARGB colour going round the hue circle, one turn every four seconds. */
  std::uint32_t Colour(const double seconds)
  {
    const double turn = std::fmod(seconds / 4.0, 1.0) * 6.0;
    const double f = turn - std::floor(turn);
    double r = 0, g = 0, b = 0;
    switch (static_cast<int>(turn)) {
    case 0: r = 1; g = f; break;
    case 1: r = 1 - f; g = 1; break;
    case 2: g = 1; b = f; break;
    case 3: g = 1 - f; b = 1; break;
    case 4: r = f; b = 1; break;
    default: r = 1; b = 1 - f; break;
    }
    const auto byte = [](const double v) { return static_cast<std::uint32_t>(v * 255.0 + 0.5); };
    return 0xFF000000u | (byte(r) << 16) | (byte(g) << 8) | byte(b);
  }

  std::string VersionText(const std::uint32_t version)
  {
    return std::to_string(VK_API_VERSION_MAJOR(version)) + "." + std::to_string(VK_API_VERSION_MINOR(version)) + "." +
      std::to_string(VK_API_VERSION_PATCH(version));
  }

  /**
   * What DeviceVulkan::CreateLogicalDevice requires (dynamic rendering, synchronization2, timeline
   * semaphores, scalar block layout) and what the textures will need, so one run tells all.
   */
  void ReportFeatures(
    const VkPhysicalDevice device, const VkPhysicalDeviceProperties& properties, const std::uint32_t instanceVersion,
    const PFN_vkGetPhysicalDeviceFeatures2 features2
  )
  {
    if (features2 == nullptr || properties.apiVersion < VK_API_VERSION_1_3 || instanceVersion < VK_API_VERSION_1_3) {
      return;
    }
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.pNext = &features13;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features12;
    features2(device, &features);
    const auto attachable = [device](const VkFormat format) {
      VkFormatProperties formatProperties{};
      vkGetPhysicalDeviceFormatProperties(device, format, &formatProperties);
      return (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U ? 1 : 0;
    };
    const VkPhysicalDeviceFeatures& core = features.features;
    char line[400];
    std::snprintf(
      line, sizeof(line),
      "  features: dynamic rendering %d, synchronization2 %d, timeline semaphores %d, scalar block layout %d, "
      "textures BC %d ETC2 %d ASTC %d, anisotropy %d, wireframe %d, depth D24S8 %d D32S8 %d, 2D textures up to %u",
      features13.dynamicRendering, features13.synchronization2, features12.timelineSemaphore, features12.scalarBlockLayout,
      core.textureCompressionBC, core.textureCompressionETC2, core.textureCompressionASTC_LDR, core.samplerAnisotropy,
      core.fillModeNonSolid, attachable(VK_FORMAT_D24_UNORM_S8_UINT), attachable(VK_FORMAT_D32_SFLOAT_S8_UINT),
      properties.limits.maxImageDimension2D
    );
    Report(line);
  }

  /** What the phone's Vulkan offers, apart from the engine: the backend wants 1.3 and a queue that presents. */
  void ReportVulkanDevices()
  {
    // vkEnumerateInstanceVersion is Vulkan 1.1 (Android 9): looked up, as the stub for API 26 lacks it.
    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    const auto enumerateVersion =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));
    if (enumerateVersion != nullptr) {
      (void)enumerateVersion(&loaderVersion);
    }
    Report("Vulkan loader " + VersionText(loaderVersion));
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "faf-re renderer test";
    // 1.3 where the loader has it: the feature query below reads Vulkan 1.2's and 1.3's features.
    application.apiVersion = loaderVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &application;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) {
      Report("vkCreateInstance failed");
      return;
    }
    const auto properties2 =
      reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));
    const auto features2 =
      reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    std::uint32_t count = 0U;
    (void)vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    (void)vkEnumeratePhysicalDevices(instance, &count, devices.data());
    for (const VkPhysicalDevice device : devices) {
      VkPhysicalDeviceDriverProperties driver{};
      driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
      VkPhysicalDeviceProperties2 properties{};
      properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      properties.pNext = &driver;
      if (properties2 != nullptr) {
        properties2(device, &properties);
      } else {
        vkGetPhysicalDeviceProperties(device, &properties.properties);
      }
      const VkPhysicalDeviceProperties& p = properties.properties;
      char line[400];
      std::snprintf(
        line, sizeof(line), "Vulkan device: %s, Vulkan %s, driver %s %s (0x%08X), vendor 0x%04X device 0x%04X, type %d%s", p.deviceName,
        VersionText(p.apiVersion).c_str(), driver.driverName, driver.driverInfo, p.driverVersion, p.vendorID, p.deviceID,
        static_cast<int>(p.deviceType), p.apiVersion < VK_API_VERSION_1_3 ? " (below the 1.3 the backend needs)" : ""
      );
      Report(line);
      ReportFeatures(device, p, application.apiVersion, features2);
    }
    vkDestroyInstance(instance, nullptr);
  }

  /// Takes this thread's messages, as the game's loop does between frames.
  void PumpMessages()
  {
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
      (void)::TranslateMessage(&message);
      (void)::DispatchMessageW(&message);
    }
  }

  /// The test's window: a top-level window of the message layer on the activity's surface, in
  /// front, as the host shows the game's frame.
  HWND CreateTestWindow(ANativeWindow* const surface, const int width, const int height)
  {
    static const bool sRegistered = [] {
      WNDCLASSW windowClass{};
      windowClass.lpfnWndProc = &::DefWindowProcW;
      windowClass.lpszClassName = L"FafGalSmoke";
      return ::RegisterClassW(&windowClass) != 0;
    }();
    if (!sRegistered) {
      return nullptr;
    }
    Win32Android_SetScreenSize(width, height);
    Win32Android_SetScreenSurface(surface);
    Win32Android_SetForeground(TRUE);
    const HWND window = ::CreateWindowExW(0, L"FafGalSmoke", L"", WS_POPUP, 0, 0, width, height, nullptr, nullptr, nullptr, nullptr);
    if (window == nullptr) {
      return nullptr;
    }
    (void)::ShowWindow(window, SW_SHOW);
    PumpMessages();
    return window;
  }

  void Run(ANativeWindow* const surface, const int seconds)
  {
    static ReportLogTarget sLogTarget;
    ReportVulkanDevices();
    const int width = ANativeWindow_getWidth(surface);
    const int height = ANativeWindow_getHeight(surface);
    Report("surface " + std::to_string(width) + "x" + std::to_string(height));
    const HWND window = CreateTestWindow(surface, width, height);
    if (window == nullptr) {
      Report("FAIL: no window on the surface (error " + std::to_string(::GetLastError()) + ")");
      Win32Android_SetScreenSurface(nullptr);
      ANativeWindow_release(surface);
      CloseReportFile();
      gRunning.store(false);
      return;
    }
    try {
      gpg::gal::DeviceContext context(gpg::gal::DeviceApi::Vulkan);
      context.mVSync = true;
      gpg::gal::Head head;
      head.mHandle = window;
      head.mWindow = window;
      head.mWindowed = true;
      head.mWidth = static_cast<std::uint32_t>(width);
      head.mHeight = static_cast<std::uint32_t>(height);
      context.AddHead(head);

      Report("creating the engine's Vulkan device");
      const auto created = std::chrono::steady_clock::now();
      gpg::gal::Device* const device = gpg::gal::Device::Create(&context);
      const double setupMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - created).count();
      char line[160];
      std::snprintf(line, sizeof(line), "device ready in %.0f ms", setupMs);
      Report(line);

      const auto started = std::chrono::steady_clock::now();
      int frames = 0;
      double slowestMs = 0;
      auto previous = started;
      for (;;) {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - started).count();
        if (gStop.load() || elapsed >= seconds) {
          break;
        }
        PumpMessages();
        device->BeginScene();
        device->Clear(true, true, true, Colour(elapsed), 1.0f, 0);
        device->EndScene();
        device->Present();
        ++frames;
        const auto after = std::chrono::steady_clock::now();
        if (frames == 1) {
          std::snprintf(line, sizeof(line), "first frame presented in %.0f ms", std::chrono::duration<double, std::milli>(after - now).count());
          Report(line);
        }
        if (frames > 1) {
          slowestMs = std::max(slowestMs, std::chrono::duration<double, std::milli>(after - previous).count());
        }
        previous = after;
      }
      const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      std::snprintf(
        line, sizeof(line), "PASS: %d frames in %.1f s (%.1f per second, slowest %.1f ms%s)", frames, total,
        total > 0 ? frames / total : 0.0, slowestMs, gStop.load() ? ", stopped early" : ""
      );
      Report(line);
      gpg::gal::Device::DestroyInstance();
      Report("device destroyed");
    } catch (const gpg::gal::Error& error) {
      Report(std::string("FAIL: ") + error.GetFile() + "(" + std::to_string(error.GetLine()) + ") " + error.what());
    } catch (const std::exception& error) {
      Report(std::string("FAIL: ") + error.what());
    } catch (...) {
      Report("FAIL: unknown exception");
    }
    Win32Android_SetForeground(FALSE);
    PumpMessages();
    (void)::DestroyWindow(window);
    Win32Android_SetScreenSurface(nullptr);
    ANativeWindow_release(surface);
    CloseReportFile();
    gRunning.store(false);
  }
} // namespace

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_GalSmoke_start(
  JNIEnv* const env, jclass, jobject const surface, jint const seconds, jstring const reportPath
)
{
  if (gRunning.load()) {
    return JNI_FALSE;
  }
  if (gThread.joinable()) {
    gThread.join();
  }
  {
    const char* const path = env->GetStringUTFChars(reportPath, nullptr);
    const std::lock_guard<std::mutex> lock(gLock);
    gReport.clear();
    if (gReportFile >= 0) {
      ::close(gReportFile);
    }
    // Appended: the activity starts the file with the screen's facts.
    gReportFile = ::open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    env->ReleaseStringUTFChars(reportPath, path);
  }
  ANativeWindow* const window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) {
    Report("FAIL: no window for the surface");
    CloseReportFile();
    return JNI_FALSE;
  }
  gStop.store(false);
  gRunning.store(true);
  gThread = std::thread(Run, window, static_cast<int>(seconds));
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_GalSmoke_stop(JNIEnv*, jclass)
{
  gStop.store(true);
  if (gThread.joinable()) {
    gThread.join();
  }
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_GalSmoke_running(JNIEnv*, jclass)
{
  return gRunning.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL Java_io_github_m3rt1n99_fafre_GalSmoke_report(JNIEnv* const env, jclass)
{
  const std::lock_guard<std::mutex> lock(gLock);
  return env->NewStringUTF(gReport.c_str());
}
