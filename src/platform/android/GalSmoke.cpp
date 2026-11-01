// Engine addition (android/CLIENT_PLAN.md, C2 groundwork): the engine's renderer on the phone.
// The activity hands over its surface; on a thread of its own the engine's gal creates its Vulkan
// device on that ANativeWindow (gal::Device::Create, the backend Windows uses), then clears the
// frame in changing colours and presents it for a few seconds, timed. No effects and no textures:
// those need the shader compiler and the texture codec of C2. What the device said, the engine's
// log lines included, is the report the app shows and shares.

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
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

#include "gpg/core/utils/Logging.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/Head.hpp"

namespace
{
  std::mutex gLock;
  std::string gReport;
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
    const std::lock_guard<std::mutex> lock(gLock);
    gReport += line;
    gReport += '\n';
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
    application.apiVersion = VK_API_VERSION_1_1;
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
    }
    vkDestroyInstance(instance, nullptr);
  }

  void Run(ANativeWindow* const window, const int seconds)
  {
    static ReportLogTarget sLogTarget;
    ReportVulkanDevices();
    const int width = ANativeWindow_getWidth(window);
    const int height = ANativeWindow_getHeight(window);
    Report("surface " + std::to_string(width) + "x" + std::to_string(height));
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
        device->BeginScene();
        device->Clear(true, true, true, Colour(elapsed), 1.0f, 0);
        device->EndScene();
        device->Present();
        ++frames;
        const auto after = std::chrono::steady_clock::now();
        if (frames > 1) {
          slowestMs = std::max(slowestMs, std::chrono::duration<double, std::milli>(after - previous).count());
        }
        previous = after;
      }
      const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
      std::snprintf(
        line, sizeof(line), "PASS: %d frames in %.1f s (%.1f per second, slowest %.1f ms)", frames, total,
        total > 0 ? frames / total : 0.0, slowestMs
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
    ANativeWindow_release(window);
    gRunning.store(false);
  }
} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_m3rt1n99_fafre_GalSmoke_start(JNIEnv* const env, jclass, jobject const surface, jint const seconds)
{
  if (gRunning.load()) {
    return JNI_FALSE;
  }
  if (gThread.joinable()) {
    gThread.join();
  }
  ANativeWindow* const window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) {
    Report("FAIL: no window for the surface");
    return JNI_FALSE;
  }
  {
    const std::lock_guard<std::mutex> lock(gLock);
    gReport.clear();
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
