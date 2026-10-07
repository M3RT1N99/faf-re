// The Vulkan sections of libfafdeviceprobe.so: the instance/device/feature report, and an offscreen
// render of the test pattern read back into deviceprobe-vulkan.png. libvulkan.so is opened with
// dlopen (the NDK's loader, as libfaf_android.so uses it), so the executable has no NEEDED entry for
// it and a device without Vulkan gives a report instead of a load failure.

#include "Probe.h"

#define VK_NO_PROTOTYPES 1
#include <vulkan/vulkan.h>

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

namespace faf_probe
{
  namespace
  {
    // The pattern shaders, compiled by build_runner.py with the NDK's glslc (-mfmt=num) from
    // port/deviceprobe/shaders/pattern.{vert,frag}.
    const uint32_t kPatternVert[] = {
#include "pattern.vert.inc"
    };
    const uint32_t kPatternFrag[] = {
#include "pattern.frag.inc"
    };

    std::string Hex(const unsigned long long v, const int digits = 0)
    {
      char text[32];
      snprintf(text, sizeof(text), "0x%0*llx", digits, v);
      return text;
    }

    const char* ResultName(const VkResult r)
    {
      switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
        default: return "VkResult";
      }
    }

    std::string ResultText(const VkResult r)
    {
      return std::string(ResultName(r)) + " (" + std::to_string(static_cast<int>(r)) + ")";
    }

    std::string VersionText(const uint32_t v)
    {
      return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) + "." +
             std::to_string(VK_API_VERSION_PATCH(v));
    }

    const char* VendorName(const uint32_t id)
    {
      switch (id) {
        case 0x1002: return "AMD";
        case 0x1010: return "Imagination";
        case 0x10DE: return "NVIDIA";
        case 0x13B5: return "ARM";
        case 0x144D: return "Samsung";
        case 0x5143: return "Qualcomm";
        case 0x8086: return "Intel";
        case 0x1AE0: return "Google";
        case 0x19E5: return "Huawei";
        case 0x10005: return "Mesa";
        default: return "";
      }
    }

    // The vendor's own reading of driverVersion where it is not the VK_MAKE_API_VERSION layout.
    std::string DriverVersionText(const uint32_t vendor, const uint32_t v)
    {
      if (vendor == 0x10DE) {
        return std::to_string((v >> 22) & 0x3ffu) + "." + std::to_string((v >> 14) & 0xffu) + "." +
               std::to_string((v >> 6) & 0xffu) + "." + std::to_string(v & 0x3fu);
      }
      return std::to_string(v >> 22) + "." + std::to_string((v >> 12) & 0x3ffu) + "." + std::to_string(v & 0xfffu);
    }

    const char* DeviceTypeName(const VkPhysicalDeviceType t)
    {
      switch (t) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated";
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete";
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual";
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return "cpu";
        default: return "other";
      }
    }

    // ------------------------------------------------------------------------------------------------
    // Loader and instance
    // ------------------------------------------------------------------------------------------------

    struct Vk
    {
      void* library = nullptr;
      PFN_vkGetInstanceProcAddr gipa = nullptr;
      VkInstance instance = VK_NULL_HANDLE;
      uint32_t instanceVersion = VK_API_VERSION_1_0;
      std::vector<std::string> instanceExtensions;
      uint32_t layerCount = 0;
      double loadMs = 0.0;
      double instanceMs = 0.0;
      bool properties2 = false;  // vkGetPhysicalDeviceProperties2 (1.1) or the KHR extension
      std::string error;
      std::string step;

      PFN_vkDestroyInstance destroyInstance = nullptr;

      template <typename F> F Get(const char* name) const
      {
        return reinterpret_cast<F>(gipa(instance, name));
      }

      ~Vk()
      {
        if (instance != VK_NULL_HANDLE && destroyInstance != nullptr) {
          // Queued in a forked section until its result is out (faf_probe::Teardown).
          faf_probe::Teardown([destroy = destroyInstance, handle = instance] { destroy(handle, nullptr); });
        }
        // libvulkan.so stays loaded: drivers register atexit handlers.
      }

      // Opens libvulkan.so and creates an instance; false with step/error set.
      bool Open()
      {
        double t0 = NowMs();
        step = "dlopen libvulkan.so";
        library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) {
          const char* why = dlerror();
          error = why != nullptr ? why : "dlopen failed";
          return false;
        }
        gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
        if (gipa == nullptr) {
          error = "libvulkan.so has no vkGetInstanceProcAddr";
          return false;
        }
        loadMs = NowMs() - t0;

        step = "vkEnumerateInstanceVersion";
        if (auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gipa(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"))) {
          uint32_t v = VK_API_VERSION_1_0;
          if (enumerateVersion(&v) == VK_SUCCESS) {
            instanceVersion = v;
          }
        }
        step = "vkEnumerateInstanceExtensionProperties";
        auto enumerateExtensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
          gipa(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
        auto enumerateLayers = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
          gipa(VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties"));
        auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
        if (enumerateExtensions == nullptr || createInstance == nullptr) {
          error = "the loader returns no vkCreateInstance / vkEnumerateInstanceExtensionProperties";
          return false;
        }
        uint32_t count = 0;
        if (enumerateExtensions(nullptr, &count, nullptr) == VK_SUCCESS && count > 0) {
          std::vector<VkExtensionProperties> list(count);
          if (enumerateExtensions(nullptr, &count, list.data()) >= VK_SUCCESS) {
            for (uint32_t i = 0; i < count; ++i) {
              instanceExtensions.push_back(list[i].extensionName);
            }
          }
        }
        if (enumerateLayers != nullptr) {
          enumerateLayers(&layerCount, nullptr);
        }

        step = "vkCreateInstance";
        std::vector<const char*> enabled;
        const bool hasProps2Ext = std::find(instanceExtensions.begin(), instanceExtensions.end(),
                                            std::string(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) != instanceExtensions.end();
        if (instanceVersion < VK_API_VERSION_1_1 && hasProps2Ext) {
          enabled.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "faf-re device probe";
        app.applicationVersion = 1;
        app.pEngineName = "faf-re";
        app.engineVersion = 1;
        app.apiVersion = instanceVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : instanceVersion;
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<uint32_t>(enabled.size());
        info.ppEnabledExtensionNames = enabled.data();
        t0 = NowMs();
        const VkResult r = createInstance(&info, nullptr, &instance);
        instanceMs = NowMs() - t0;
        if (r != VK_SUCCESS) {
          error = ResultText(r);
          instance = VK_NULL_HANDLE;
          return false;
        }
        destroyInstance = Get<PFN_vkDestroyInstance>("vkDestroyInstance");
        properties2 = app.apiVersion >= VK_API_VERSION_1_1 || !enabled.empty();
        return true;
      }

      PFN_vkGetPhysicalDeviceProperties2 Properties2() const
      {
        auto f = Get<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2");
        if (f == nullptr) {
          f = Get<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2KHR");
        }
        return properties2 ? f : nullptr;
      }
    };

    std::vector<std::string> DeviceExtensions(const Vk& vk, VkPhysicalDevice gpu)
    {
      std::vector<std::string> names;
      auto enumerate = vk.Get<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
      uint32_t count = 0;
      if (enumerate != nullptr && enumerate(gpu, nullptr, &count, nullptr) == VK_SUCCESS && count > 0) {
        std::vector<VkExtensionProperties> list(count);
        if (enumerate(gpu, nullptr, &count, list.data()) >= VK_SUCCESS) {
          for (uint32_t i = 0; i < count; ++i) {
            names.push_back(list[i].extensionName);
          }
        }
      }
      std::sort(names.begin(), names.end());
      return names;
    }

    bool Has(const std::vector<std::string>& list, const char* name)
    {
      return std::find(list.begin(), list.end(), std::string(name)) != list.end();
    }

    // ------------------------------------------------------------------------------------------------
    // The report
    // ------------------------------------------------------------------------------------------------

    struct FormatEntry
    {
      const char* name;
      VkFormat format;
    };

    // The formats the plan asks about (docs/port/renderer.md, the M7 phone probe): the DXT family FA's
    // textures use, the compressed formats a phone samples natively, the depth formats D3D9's D24S8
    // maps to, and the colour targets.
    const FormatEntry kFormats[] = {
      {"BC1_RGB_UNORM", VK_FORMAT_BC1_RGB_UNORM_BLOCK},
      {"BC1_RGBA_UNORM", VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
      {"BC2_UNORM", VK_FORMAT_BC2_UNORM_BLOCK},
      {"BC3_UNORM", VK_FORMAT_BC3_UNORM_BLOCK},
      {"BC4_UNORM", VK_FORMAT_BC4_UNORM_BLOCK},
      {"BC5_UNORM", VK_FORMAT_BC5_UNORM_BLOCK},
      {"BC7_UNORM", VK_FORMAT_BC7_UNORM_BLOCK},
      {"ETC2_R8G8B8_UNORM", VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK},
      {"ETC2_R8G8B8A1_UNORM", VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK},
      {"ETC2_R8G8B8A8_UNORM", VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK},
      {"EAC_R11_UNORM", VK_FORMAT_EAC_R11_UNORM_BLOCK},
      {"ASTC_4x4_UNORM", VK_FORMAT_ASTC_4x4_UNORM_BLOCK},
      {"ASTC_6x6_UNORM", VK_FORMAT_ASTC_6x6_UNORM_BLOCK},
      {"ASTC_8x8_UNORM", VK_FORMAT_ASTC_8x8_UNORM_BLOCK},
      {"D16_UNORM", VK_FORMAT_D16_UNORM},
      {"X8_D24_UNORM_PACK32", VK_FORMAT_X8_D24_UNORM_PACK32},
      {"D24_UNORM_S8_UINT", VK_FORMAT_D24_UNORM_S8_UINT},
      {"D32_SFLOAT", VK_FORMAT_D32_SFLOAT},
      {"D32_SFLOAT_S8_UINT", VK_FORMAT_D32_SFLOAT_S8_UINT},
      {"R8G8B8A8_UNORM", VK_FORMAT_R8G8B8A8_UNORM},
      {"B8G8R8A8_UNORM", VK_FORMAT_B8G8R8A8_UNORM},
      {"R8G8B8A8_SRGB", VK_FORMAT_R8G8B8A8_SRGB},
      {"A2B10G10R10_UNORM", VK_FORMAT_A2B10G10R10_UNORM_PACK32},
      {"R16G16B16A16_SFLOAT", VK_FORMAT_R16G16B16A16_SFLOAT},
      {"R32_SFLOAT", VK_FORMAT_R32_SFLOAT},
      {"R5G6B5_UNORM", VK_FORMAT_R5G6B5_UNORM_PACK16},
      {"B5G6R5_UNORM", VK_FORMAT_B5G6R5_UNORM_PACK16},
      {"B4G4R4A4_UNORM", VK_FORMAT_B4G4R4A4_UNORM_PACK16},
      {"A1R5G5B5_UNORM", VK_FORMAT_A1R5G5B5_UNORM_PACK16},
      {"R8_UNORM", VK_FORMAT_R8_UNORM},
      {"R8G8_UNORM", VK_FORMAT_R8G8_UNORM},
    };

    // Extensions that change what the backend can do (named so a reader need not scan the list).
    const char* const kNotableExtensions[] = {
      "VK_ANDROID_external_memory_android_hardware_buffer",
      "VK_KHR_external_memory_fd",
      "VK_KHR_external_semaphore_fd",
      "VK_KHR_driver_properties",
      "VK_KHR_maintenance1",
      "VK_KHR_dynamic_rendering",
      "VK_KHR_timeline_semaphore",
      "VK_KHR_synchronization2",
      "VK_KHR_pipeline_library",
      "VK_EXT_graphics_pipeline_library",
      "VK_EXT_extended_dynamic_state",
      "VK_EXT_custom_border_color",
      "VK_EXT_border_color_swizzle",
      "VK_EXT_depth_clip_enable",
      "VK_EXT_depth_clamp_zero_one",
      "VK_EXT_index_type_uint8",
      "VK_EXT_4444_formats",
      "VK_EXT_line_rasterization",
      "VK_EXT_texture_compression_astc_hdr",
      "VK_EXT_pipeline_creation_cache_control",
      "VK_EXT_pipeline_creation_feedback",
      "VK_KHR_shader_float16_int8",
      "VK_EXT_calibrated_timestamps",
      "VK_KHR_portability_subset",
    };

    std::string FeatureFlags(const VkFormatFeatureFlags f)
    {
      Json j;
      j.Bool("sampled", (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0);
      j.Bool("filter_linear", (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0);
      j.Bool("color_attachment", (f & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0);
      j.Bool("blend", (f & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0);
      j.Bool("depth_stencil", (f & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0);
      j.Bool("storage", (f & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0);
      j.Bool("blit_src", (f & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0);
      j.Bool("blit_dst", (f & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0);
      return j.Text();
    }

    std::string QueueFlags(const VkQueueFlags f)
    {
      std::string out;
      const struct { VkQueueFlags bit; const char* name; } names[] = {
        {VK_QUEUE_GRAPHICS_BIT, "graphics"}, {VK_QUEUE_COMPUTE_BIT, "compute"}, {VK_QUEUE_TRANSFER_BIT, "transfer"},
        {VK_QUEUE_SPARSE_BINDING_BIT, "sparse"}, {VK_QUEUE_PROTECTED_BIT, "protected"}};
      for (const auto& n : names) {
        if ((f & n.bit) != 0) {
          out += out.empty() ? "" : "|";
          out += n.name;
        }
      }
      return out;
    }

    std::string MemoryFlags(const VkMemoryPropertyFlags f)
    {
      std::string out;
      const struct { VkMemoryPropertyFlags bit; const char* name; } names[] = {
        {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, "device_local"}, {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, "host_visible"},
        {VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "host_coherent"}, {VK_MEMORY_PROPERTY_HOST_CACHED_BIT, "host_cached"},
        {VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, "lazily_allocated"}, {VK_MEMORY_PROPERTY_PROTECTED_BIT, "protected"}};
      for (const auto& n : names) {
        if ((f & n.bit) != 0) {
          out += out.empty() ? "" : "|";
          out += n.name;
        }
      }
      return out;
    }

    std::string SampleCounts(const VkSampleCountFlags f)
    {
      Json a('[');
      for (int bit = 0; bit < 7; ++bit) {
        if ((f & (1u << bit)) != 0) {
          a.Add(std::to_string(1 << bit));
        }
      }
      return a.Text();
    }

    struct DeviceFacts
    {
      std::string name;
      std::string json;
      std::string summary;
    };

    DeviceFacts DescribeDevice(const Vk& vk, VkPhysicalDevice gpu)
    {
      DeviceFacts facts;
      auto getProperties = vk.Get<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
      auto getFeatures = vk.Get<PFN_vkGetPhysicalDeviceFeatures>("vkGetPhysicalDeviceFeatures");
      auto getFormat = vk.Get<PFN_vkGetPhysicalDeviceFormatProperties>("vkGetPhysicalDeviceFormatProperties");
      auto getQueues = vk.Get<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
      auto getMemory = vk.Get<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties");

      VkPhysicalDeviceProperties p{};
      getProperties(gpu, &p);
      facts.name = p.deviceName;
      const std::vector<std::string> extensions = DeviceExtensions(vk, gpu);

      Json d;
      d.Str("name", p.deviceName);
      d.Str("type", DeviceTypeName(p.deviceType));
      d.Str("api_version", VersionText(p.apiVersion));
      d.Str("vendor_id", Hex(p.vendorID, 4));
      d.Str("vendor", VendorName(p.vendorID));
      d.Str("device_id", Hex(p.deviceID, 4));
      d.Str("driver_version_raw", Hex(p.driverVersion, 8));
      d.Str("driver_version", DriverVersionText(p.vendorID, p.driverVersion));
      {
        std::string uuid;
        for (const uint8_t b : p.pipelineCacheUUID) {
          char two[3];
          snprintf(two, sizeof(two), "%02x", b);
          uuid += two;
        }
        d.Str("pipeline_cache_uuid", uuid);
      }

      // Driver name and info (Vulkan 1.2 core, or VK_KHR_driver_properties).
      std::string driverName;
      std::string driverInfo;
      auto properties2 = vk.Properties2();
      if (properties2 != nullptr && (p.apiVersion >= VK_API_VERSION_1_2 || Has(extensions, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME))) {
        VkPhysicalDeviceDriverProperties driver{};
        driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &driver;
        properties2(gpu, &props2);
        driverName = driver.driverName;
        driverInfo = driver.driverInfo;
        Json drv;
        drv.Num("id", driver.driverID);
        drv.Str("name", driver.driverName);
        drv.Str("info", driver.driverInfo);
        drv.Str("conformance", std::to_string(driver.conformanceVersion.major) + "." +
                                 std::to_string(driver.conformanceVersion.minor) + "." +
                                 std::to_string(driver.conformanceVersion.subminor) + "." +
                                 std::to_string(driver.conformanceVersion.patch));
        d.Raw("driver", drv.Text());
      } else {
        d.Null("driver");
      }

      VkPhysicalDeviceFeatures f{};
      getFeatures(gpu, &f);
      Json features;
      features.Bool("textureCompressionBC", f.textureCompressionBC);
      features.Bool("textureCompressionETC2", f.textureCompressionETC2);
      features.Bool("textureCompressionASTC_LDR", f.textureCompressionASTC_LDR);
      features.Bool("fillModeNonSolid", f.fillModeNonSolid);
      features.Bool("samplerAnisotropy", f.samplerAnisotropy);
      features.Bool("depthClamp", f.depthClamp);
      features.Bool("depthBiasClamp", f.depthBiasClamp);
      features.Bool("depthBounds", f.depthBounds);
      features.Bool("independentBlend", f.independentBlend);
      features.Bool("dualSrcBlend", f.dualSrcBlend);
      features.Bool("logicOp", f.logicOp);
      features.Bool("wideLines", f.wideLines);
      features.Bool("largePoints", f.largePoints);
      features.Bool("geometryShader", f.geometryShader);
      features.Bool("tessellationShader", f.tessellationShader);
      features.Bool("multiDrawIndirect", f.multiDrawIndirect);
      features.Bool("drawIndirectFirstInstance", f.drawIndirectFirstInstance);
      features.Bool("sampleRateShading", f.sampleRateShading);
      features.Bool("shaderClipDistance", f.shaderClipDistance);
      features.Bool("shaderFloat64", f.shaderFloat64);
      features.Bool("shaderInt16", f.shaderInt16);
      features.Bool("occlusionQueryPrecise", f.occlusionQueryPrecise);
      features.Bool("fragmentStoresAndAtomics", f.fragmentStoresAndAtomics);
      features.Bool("imageCubeArray", f.imageCubeArray);
      features.Bool("fullDrawIndexUint32", f.fullDrawIndexUint32);
      d.Raw("features", features.Text());

      // Formats (optimal tiling, the layout textures and targets use).
      Json formats;
      auto sampled = [&](VkFormat format) {
        VkFormatProperties fp{};
        getFormat(gpu, format, &fp);
        return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
      };
      auto depth = [&](VkFormat format) {
        VkFormatProperties fp{};
        getFormat(gpu, format, &fp);
        return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
      };
      for (const FormatEntry& e : kFormats) {
        VkFormatProperties fp{};
        getFormat(gpu, e.format, &fp);
        formats.Raw(e.name, FeatureFlags(fp.optimalTilingFeatures));
      }
      d.Raw("formats_optimal_tiling", formats.Text());
      const bool bc13 = sampled(VK_FORMAT_BC1_RGBA_UNORM_BLOCK) && sampled(VK_FORMAT_BC2_UNORM_BLOCK) && sampled(VK_FORMAT_BC3_UNORM_BLOCK);
      const bool etc2 = sampled(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK) && sampled(VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK);
      const bool astc = sampled(VK_FORMAT_ASTC_4x4_UNORM_BLOCK);
      const bool d24s8 = depth(VK_FORMAT_D24_UNORM_S8_UINT);
      const bool d32 = depth(VK_FORMAT_D32_SFLOAT);
      const bool d32s8 = depth(VK_FORMAT_D32_SFLOAT_S8_UINT);
      Json answers;
      answers.Bool("bc1_3_sampled", bc13);
      answers.Bool("etc2_sampled", etc2);
      answers.Bool("astc_4x4_sampled", astc);
      answers.Bool("d24s8_attachment", d24s8);
      answers.Bool("d32_attachment", d32);
      answers.Bool("d32s8_attachment", d32s8);
      answers.Bool("fill_mode_non_solid", f.fillModeNonSolid);
      answers.Bool("sampler_anisotropy", f.samplerAnisotropy);
      answers.Bool("depth_clamp", f.depthClamp);
      answers.Bool("independent_blend", f.independentBlend);
      answers.Num("max_texture_2d", p.limits.maxImageDimension2D);
      answers.Bool("timestamps_graphics", p.limits.timestampComputeAndGraphics);
      answers.Bool("custom_border_color", Has(extensions, "VK_EXT_custom_border_color"));
      answers.Bool("ahardwarebuffer_import", Has(extensions, "VK_ANDROID_external_memory_android_hardware_buffer"));
      d.Raw("answers", answers.Text());

      const VkPhysicalDeviceLimits& l = p.limits;
      Json limits;
      limits.Num("maxImageDimension2D", l.maxImageDimension2D);
      limits.Num("maxImageDimensionCube", l.maxImageDimensionCube);
      limits.Num("maxImageArrayLayers", l.maxImageArrayLayers);
      limits.Real("maxSamplerAnisotropy", l.maxSamplerAnisotropy, 1);
      limits.Num("maxColorAttachments", l.maxColorAttachments);
      limits.Num("maxPushConstantsSize", l.maxPushConstantsSize);
      limits.Num("maxUniformBufferRange", l.maxUniformBufferRange);
      limits.Num("maxStorageBufferRange", l.maxStorageBufferRange);
      limits.Num("maxBoundDescriptorSets", l.maxBoundDescriptorSets);
      limits.Num("maxPerStageDescriptorSamplers", l.maxPerStageDescriptorSamplers);
      limits.Num("maxPerStageDescriptorSampledImages", l.maxPerStageDescriptorSampledImages);
      limits.Num("maxPerStageDescriptorUniformBuffers", l.maxPerStageDescriptorUniformBuffers);
      limits.Num("maxVertexInputAttributes", l.maxVertexInputAttributes);
      limits.Num("maxVertexInputBindings", l.maxVertexInputBindings);
      limits.Num("maxFragmentOutputAttachments", l.maxFragmentOutputAttachments);
      limits.Num("maxViewports", l.maxViewports);
      limits.Num("maxMemoryAllocationCount", l.maxMemoryAllocationCount);
      limits.Unsigned("bufferImageGranularity", l.bufferImageGranularity);
      limits.Unsigned("minUniformBufferOffsetAlignment", l.minUniformBufferOffsetAlignment);
      limits.Unsigned("nonCoherentAtomSize", l.nonCoherentAtomSize);
      limits.Unsigned("optimalBufferCopyRowPitchAlignment", l.optimalBufferCopyRowPitchAlignment);
      limits.Raw("framebufferColorSampleCounts", SampleCounts(l.framebufferColorSampleCounts));
      limits.Raw("framebufferDepthSampleCounts", SampleCounts(l.framebufferDepthSampleCounts));
      limits.Bool("timestampComputeAndGraphics", l.timestampComputeAndGraphics);
      limits.Real("timestampPeriod", l.timestampPeriod, 3);
      limits.Raw("pointSizeRange", "[" + RealText(l.pointSizeRange[0], 1) + "," + RealText(l.pointSizeRange[1], 1) + "]");
      limits.Raw("lineWidthRange", "[" + RealText(l.lineWidthRange[0], 1) + "," + RealText(l.lineWidthRange[1], 1) + "]");
      d.Raw("limits", limits.Text());

      uint32_t queueCount = 0;
      getQueues(gpu, &queueCount, nullptr);
      std::vector<VkQueueFamilyProperties> queues(queueCount);
      getQueues(gpu, &queueCount, queues.data());
      Json q('[');
      bool graphicsTimestamps = false;
      for (uint32_t i = 0; i < queueCount; ++i) {
        Json one;
        one.Num("index", i);
        one.Str("flags", QueueFlags(queues[i].queueFlags));
        one.Num("count", queues[i].queueCount);
        one.Num("timestamp_valid_bits", queues[i].timestampValidBits);
        q.Add(one.Text());
        graphicsTimestamps = graphicsTimestamps ||
                             ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && queues[i].timestampValidBits > 0);
      }
      d.Raw("queue_families", q.Text());

      VkPhysicalDeviceMemoryProperties m{};
      getMemory(gpu, &m);
      Json heaps('[');
      for (uint32_t i = 0; i < m.memoryHeapCount; ++i) {
        Json h;
        h.Num("mb", static_cast<long long>(m.memoryHeaps[i].size / (1024ull * 1024ull)));
        h.Bool("device_local", (m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0);
        heaps.Add(h.Text());
      }
      Json types('[');
      for (uint32_t i = 0; i < m.memoryTypeCount; ++i) {
        Json t;
        t.Num("heap", m.memoryTypes[i].heapIndex);
        t.Str("flags", MemoryFlags(m.memoryTypes[i].propertyFlags));
        types.Add(t.Text());
      }
      d.Raw("memory_heaps", heaps.Text());
      d.Raw("memory_types", types.Text());

      Json notable;
      for (const char* name : kNotableExtensions) {
        notable.Bool(name, Has(extensions, name));
      }
      d.Raw("notable_extensions", notable.Text());
      d.Num("extension_count", static_cast<long long>(extensions.size()));
      Json all('[');
      for (const std::string& e : extensions) {
        all.AddStr(e);
      }
      d.Raw("extensions", all.Text());
      facts.json = d.Text();

      auto yn = [](bool v) { return v ? "yes" : "no"; };
      char line[768];
      snprintf(line, sizeof(line),
               "%s (Vulkan %s, driver %s%s%s) BC1-3 %s, ETC2 %s, ASTC %s, D24S8 %s, D32 %s, fillModeNonSolid %s, "
               "anisotropy %s, depthClamp %s, independentBlend %s, max 2D %u, timestamps %s",
               p.deviceName, VersionText(p.apiVersion).c_str(),
               driverName.empty() ? DriverVersionText(p.vendorID, p.driverVersion).c_str() : driverName.c_str(),
               driverInfo.empty() ? "" : " ", driverInfo.c_str(), yn(bc13), yn(etc2), yn(astc), yn(d24s8), yn(d32),
               yn(f.fillModeNonSolid), yn(f.samplerAnisotropy), yn(f.depthClamp), yn(f.independentBlend),
               l.maxImageDimension2D, yn(l.timestampComputeAndGraphics && graphicsTimestamps));
      facts.summary = line;
      return facts;
    }

    // ------------------------------------------------------------------------------------------------
    // The offscreen render
    // ------------------------------------------------------------------------------------------------

    // Device-level entry points the render uses.
    struct DeviceFns
    {
#define FAF_PROBE_VK_DEVICE_FUNCTIONS(X)                                                                      \
  X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkCreateImage) X(vkDestroyImage)                 \
  X(vkGetImageMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateImageView)    \
  X(vkDestroyImageView) X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer) \
  X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout)            \
  X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateBuffer) X(vkDestroyBuffer)                          \
  X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory)                          \
  X(vkInvalidateMappedMemoryRanges) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers)    \
  X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdBindPipeline) \
  X(vkCmdDraw) X(vkCmdCopyImageToBuffer) X(vkCmdPipelineBarrier) X(vkCreateFence) X(vkDestroyFence)               \
  X(vkWaitForFences) X(vkQueueSubmit) X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkCmdResetQueryPool)           \
  X(vkCmdWriteTimestamp) X(vkGetQueryPoolResults) X(vkCreatePipelineCache) X(vkDestroyPipelineCache)
#define FAF_PROBE_VK_DECLARE(name) PFN_##name name = nullptr;
      FAF_PROBE_VK_DEVICE_FUNCTIONS(FAF_PROBE_VK_DECLARE)
#undef FAF_PROBE_VK_DECLARE

      // The first entry point the driver does not return, or "".
      std::string Load(PFN_vkGetDeviceProcAddr gdpa, VkDevice device)
      {
#define FAF_PROBE_VK_LOAD(name)                                                    \
  name = reinterpret_cast<PFN_##name>(gdpa(device, #name));                        \
  if (name == nullptr) {                                                           \
    return #name;                                                                  \
  }
        FAF_PROBE_VK_DEVICE_FUNCTIONS(FAF_PROBE_VK_LOAD)
#undef FAF_PROBE_VK_LOAD
        return std::string();
      }
    };

    int MemoryType(const VkPhysicalDeviceMemoryProperties& m, const uint32_t bits, const VkMemoryPropertyFlags want,
                   const VkMemoryPropertyFlags prefer)
    {
      int fallback = -1;
      for (uint32_t i = 0; i < m.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) == 0 || (m.memoryTypes[i].propertyFlags & want) != want) {
          continue;
        }
        if ((m.memoryTypes[i].propertyFlags & prefer) == prefer) {
          return static_cast<int>(i);
        }
        if (fallback < 0) {
          fallback = static_cast<int>(i);
        }
      }
      return fallback;
    }

    struct Render
    {
      const Vk& vk;
      VkPhysicalDevice gpu = VK_NULL_HANDLE;
      VkDevice device = VK_NULL_HANDLE;
      DeviceFns fn;
      VkImage image = VK_NULL_HANDLE;
      VkDeviceMemory imageMemory = VK_NULL_HANDLE;
      VkImageView view = VK_NULL_HANDLE;
      VkRenderPass renderPass = VK_NULL_HANDLE;
      VkFramebuffer framebuffer = VK_NULL_HANDLE;
      VkShaderModule vert = VK_NULL_HANDLE;
      VkShaderModule frag = VK_NULL_HANDLE;
      VkPipelineLayout layout = VK_NULL_HANDLE;
      VkPipelineCache cache = VK_NULL_HANDLE;
      VkPipeline pipeline = VK_NULL_HANDLE;
      VkBuffer buffer = VK_NULL_HANDLE;
      VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
      VkCommandPool pool = VK_NULL_HANDLE;
      VkFence fence = VK_NULL_HANDLE;
      VkQueryPool queries = VK_NULL_HANDLE;

      explicit Render(const Vk& v) : vk(v) {}

      ~Render()
      {
        if (device == VK_NULL_HANDLE) {
          return;
        }
        // Queued in a forked section until its result is out (faf_probe::Teardown); the handles are
        // copied, this object is gone by then.
        faf_probe::Teardown([fn = fn, device = device, queries = queries, fence = fence, pool = pool, buffer = buffer,
                             bufferMemory = bufferMemory, pipeline = pipeline, cache = cache, layout = layout,
                             vert = vert, frag = frag, framebuffer = framebuffer, renderPass = renderPass,
                             view = view, image = image, imageMemory = imageMemory] {
          fn.vkDeviceWaitIdle(device);
          if (queries != VK_NULL_HANDLE) fn.vkDestroyQueryPool(device, queries, nullptr);
          if (fence != VK_NULL_HANDLE) fn.vkDestroyFence(device, fence, nullptr);
          if (pool != VK_NULL_HANDLE) fn.vkDestroyCommandPool(device, pool, nullptr);
          if (buffer != VK_NULL_HANDLE) fn.vkDestroyBuffer(device, buffer, nullptr);
          if (bufferMemory != VK_NULL_HANDLE) fn.vkFreeMemory(device, bufferMemory, nullptr);
          if (pipeline != VK_NULL_HANDLE) fn.vkDestroyPipeline(device, pipeline, nullptr);
          if (cache != VK_NULL_HANDLE) fn.vkDestroyPipelineCache(device, cache, nullptr);
          if (layout != VK_NULL_HANDLE) fn.vkDestroyPipelineLayout(device, layout, nullptr);
          if (vert != VK_NULL_HANDLE) fn.vkDestroyShaderModule(device, vert, nullptr);
          if (frag != VK_NULL_HANDLE) fn.vkDestroyShaderModule(device, frag, nullptr);
          if (framebuffer != VK_NULL_HANDLE) fn.vkDestroyFramebuffer(device, framebuffer, nullptr);
          if (renderPass != VK_NULL_HANDLE) fn.vkDestroyRenderPass(device, renderPass, nullptr);
          if (view != VK_NULL_HANDLE) fn.vkDestroyImageView(device, view, nullptr);
          if (image != VK_NULL_HANDLE) fn.vkDestroyImage(device, image, nullptr);
          if (imageMemory != VK_NULL_HANDLE) fn.vkFreeMemory(device, imageMemory, nullptr);
          fn.vkDestroyDevice(device, nullptr);
        });
      }
    };
  } // namespace

  SectionResult RunVulkanReport(const Options& /*options*/)
  {
    SectionResult result;
    Json j;
    Vk vk;
    const double t0 = NowMs();
    if (!vk.Open()) {
      const bool noDriver = vk.step == "dlopen libvulkan.so" || vk.error.find("INCOMPATIBLE_DRIVER") != std::string::npos;
      j.Str("status", noDriver ? "unsupported" : "error");
      j.Str("step", vk.step);
      j.Str("error", vk.error);
      j.Str("instance_version", VersionText(vk.instanceVersion));
      result.json = j.Text();
      result.summary = std::string(noDriver ? "no Vulkan: " : "Vulkan failed: ") + vk.step + ": " + vk.error;
      return result;
    }
    auto enumerate = vk.Get<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
    uint32_t count = 0;
    VkResult r = enumerate != nullptr ? enumerate(vk.instance, &count, nullptr) : VK_ERROR_INITIALIZATION_FAILED;
    std::vector<VkPhysicalDevice> gpus(count);
    if (r == VK_SUCCESS && count > 0) {
      r = enumerate(vk.instance, &count, gpus.data());
      gpus.resize(count);
    }
    Json devices('[');
    std::string summary;
    for (VkPhysicalDevice gpu : gpus) {
      DeviceFacts facts = DescribeDevice(vk, gpu);
      devices.Add(facts.json);
      summary += (summary.empty() ? "" : " | ") + facts.summary;
    }
    j.Str("status", gpus.empty() ? "unsupported" : "ok");
    j.Str("instance_version", VersionText(vk.instanceVersion));
    j.Real("load_ms", vk.loadMs, 2);
    j.Real("create_instance_ms", vk.instanceMs, 2);
    j.Num("layer_count", vk.layerCount);
    Json instanceExtensions('[');
    for (const std::string& e : vk.instanceExtensions) {
      instanceExtensions.AddStr(e);
    }
    j.Raw("instance_extensions", instanceExtensions.Text());
    j.Num("device_count", static_cast<long long>(gpus.size()));
    if (gpus.empty()) {
      j.Str("error", "vkEnumeratePhysicalDevices: " + ResultText(r) + ", " + std::to_string(count) + " devices");
    }
    j.Raw("devices", devices.Text());
    j.Real("ms", NowMs() - t0, 1);
    result.json = j.Text();
    result.summary = gpus.empty() ? "Vulkan instance " + VersionText(vk.instanceVersion) + " but no physical device"
                                  : "Vulkan " + VersionText(vk.instanceVersion) + ": " + summary;
    return result;
  }

  SectionResult RunVulkanRender(const Options& options)
  {
    SectionResult result;
    Json j;
    const double t0 = NowMs();
    std::string step;
    std::string error;
    auto fail = [&](const std::string& why) {
      error = why;
      return false;
    };
    auto check = [&](VkResult r, const char* what) {
      step = what;
      if (r != VK_SUCCESS) {
        error = std::string(what) + ": " + ResultText(r);
        return false;
      }
      return true;
    };

    Vk vk;
    if (!vk.Open()) {
      const bool noDriver = vk.step == "dlopen libvulkan.so";
      j.Str("status", noDriver ? "unsupported" : "error");
      j.Str("step", vk.step);
      j.Str("error", vk.error);
      result.json = j.Text();
      result.summary = "no Vulkan render: " + vk.step + ": " + vk.error;
      return result;
    }
    Render rd(vk);
    double createDeviceMs = -1.0;
    double pipelineMs = -1.0;
    double pipelineCachedMs = -1.0;
    double submitMs = -1.0;
    double gpuMs = -1.0;
    std::string deviceName;
    PatternCheck pattern;
    std::string png;
    std::string pngError;
    std::string pixels;

    const bool ok = [&]() -> bool {
      auto enumerate = vk.Get<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
      auto getQueues = vk.Get<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
      auto getProperties = vk.Get<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties");
      auto getMemory = vk.Get<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties");
      auto createDevice = vk.Get<PFN_vkCreateDevice>("vkCreateDevice");
      auto gdpa = vk.Get<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
      step = "vkEnumeratePhysicalDevices";
      uint32_t count = 0;
      if (!check(enumerate(vk.instance, &count, nullptr), "vkEnumeratePhysicalDevices")) return false;
      std::vector<VkPhysicalDevice> gpus(count);
      if (count == 0) return fail("no physical device");
      if (!check(enumerate(vk.instance, &count, gpus.data()), "vkEnumeratePhysicalDevices")) return false;
      uint32_t family = UINT32_MAX;
      uint32_t timestampBits = 0;
      for (VkPhysicalDevice gpu : gpus) {
        uint32_t n = 0;
        getQueues(gpu, &n, nullptr);
        std::vector<VkQueueFamilyProperties> queues(n);
        getQueues(gpu, &n, queues.data());
        for (uint32_t i = 0; i < n; ++i) {
          if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            rd.gpu = gpu;
            family = i;
            timestampBits = queues[i].timestampValidBits;
            break;
          }
        }
        if (rd.gpu != VK_NULL_HANDLE) break;
      }
      step = "pick a graphics queue";
      if (rd.gpu == VK_NULL_HANDLE) return fail("no device with a graphics queue");
      VkPhysicalDeviceProperties props{};
      getProperties(rd.gpu, &props);
      deviceName = props.deviceName;
      VkPhysicalDeviceMemoryProperties memory{};
      getMemory(rd.gpu, &memory);

      const float priority = 1.0f;
      VkDeviceQueueCreateInfo queueInfo{};
      queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
      queueInfo.queueFamilyIndex = family;
      queueInfo.queueCount = 1;
      queueInfo.pQueuePriorities = &priority;
      VkDeviceCreateInfo deviceInfo{};
      deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
      deviceInfo.queueCreateInfoCount = 1;
      deviceInfo.pQueueCreateInfos = &queueInfo;
      double t = NowMs();
      if (!check(createDevice(rd.gpu, &deviceInfo, nullptr, &rd.device), "vkCreateDevice")) {
        rd.device = VK_NULL_HANDLE;
        return false;
      }
      createDeviceMs = NowMs() - t;
      step = "vkGetDeviceProcAddr";
      const std::string missing = rd.fn.Load(gdpa, rd.device);
      if (!missing.empty()) {
        rd.fn.vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(gdpa(rd.device, "vkDestroyDevice"));
        if (rd.fn.vkDestroyDevice != nullptr) rd.fn.vkDestroyDevice(rd.device, nullptr);
        rd.device = VK_NULL_HANDLE;
        return fail("the driver returns no " + missing);
      }
      VkQueue queue = VK_NULL_HANDLE;
      rd.fn.vkGetDeviceQueue(rd.device, family, 0, &queue);

      // The colour target.
      VkImageCreateInfo imageInfo{};
      imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
      imageInfo.imageType = VK_IMAGE_TYPE_2D;
      imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
      imageInfo.extent = {static_cast<uint32_t>(kPatternSize), static_cast<uint32_t>(kPatternSize), 1};
      imageInfo.mipLevels = 1;
      imageInfo.arrayLayers = 1;
      imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
      imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      if (!check(rd.fn.vkCreateImage(rd.device, &imageInfo, nullptr, &rd.image), "vkCreateImage")) return false;
      VkMemoryRequirements imageReq{};
      rd.fn.vkGetImageMemoryRequirements(rd.device, rd.image, &imageReq);
      const int imageType = MemoryType(memory, imageReq.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
      step = "image memory type";
      if (imageType < 0) return fail("no memory type for the image");
      VkMemoryAllocateInfo alloc{};
      alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      alloc.allocationSize = imageReq.size;
      alloc.memoryTypeIndex = static_cast<uint32_t>(imageType);
      if (!check(rd.fn.vkAllocateMemory(rd.device, &alloc, nullptr, &rd.imageMemory), "vkAllocateMemory(image)")) return false;
      if (!check(rd.fn.vkBindImageMemory(rd.device, rd.image, rd.imageMemory, 0), "vkBindImageMemory")) return false;
      VkImageViewCreateInfo viewInfo{};
      viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      viewInfo.image = rd.image;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
      viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      if (!check(rd.fn.vkCreateImageView(rd.device, &viewInfo, nullptr, &rd.view), "vkCreateImageView")) return false;

      // Render pass: clear, draw, store, then the image is ready for the copy.
      VkAttachmentDescription attachment{};
      attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
      attachment.samples = VK_SAMPLE_COUNT_1_BIT;
      attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
      VkSubpassDescription subpass{};
      subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
      subpass.colorAttachmentCount = 1;
      subpass.pColorAttachments = &colorRef;
      VkSubpassDependency dependencies[2]{};
      dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
      dependencies[0].dstSubpass = 0;
      dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      dependencies[1].srcSubpass = 0;
      dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
      dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
      dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      VkRenderPassCreateInfo passInfo{};
      passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
      passInfo.attachmentCount = 1;
      passInfo.pAttachments = &attachment;
      passInfo.subpassCount = 1;
      passInfo.pSubpasses = &subpass;
      passInfo.dependencyCount = 2;
      passInfo.pDependencies = dependencies;
      if (!check(rd.fn.vkCreateRenderPass(rd.device, &passInfo, nullptr, &rd.renderPass), "vkCreateRenderPass")) return false;
      VkFramebufferCreateInfo fbInfo{};
      fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fbInfo.renderPass = rd.renderPass;
      fbInfo.attachmentCount = 1;
      fbInfo.pAttachments = &rd.view;
      fbInfo.width = kPatternSize;
      fbInfo.height = kPatternSize;
      fbInfo.layers = 1;
      if (!check(rd.fn.vkCreateFramebuffer(rd.device, &fbInfo, nullptr, &rd.framebuffer), "vkCreateFramebuffer")) return false;

      // Pipeline.
      VkShaderModuleCreateInfo moduleInfo{};
      moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
      moduleInfo.codeSize = sizeof(kPatternVert);
      moduleInfo.pCode = kPatternVert;
      if (!check(rd.fn.vkCreateShaderModule(rd.device, &moduleInfo, nullptr, &rd.vert), "vkCreateShaderModule(vert)")) return false;
      moduleInfo.codeSize = sizeof(kPatternFrag);
      moduleInfo.pCode = kPatternFrag;
      if (!check(rd.fn.vkCreateShaderModule(rd.device, &moduleInfo, nullptr, &rd.frag), "vkCreateShaderModule(frag)")) return false;
      VkPipelineLayoutCreateInfo layoutInfo{};
      layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      if (!check(rd.fn.vkCreatePipelineLayout(rd.device, &layoutInfo, nullptr, &rd.layout), "vkCreatePipelineLayout")) return false;
      VkPipelineShaderStageCreateInfo stages[2]{};
      stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
      stages[0].module = rd.vert;
      stages[0].pName = "main";
      stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
      stages[1].module = rd.frag;
      stages[1].pName = "main";
      VkPipelineVertexInputStateCreateInfo vertexInput{};
      vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
      VkPipelineInputAssemblyStateCreateInfo assembly{};
      assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
      assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      VkViewport viewport{0.0f, 0.0f, static_cast<float>(kPatternSize), static_cast<float>(kPatternSize), 0.0f, 1.0f};
      VkRect2D scissor{{0, 0}, {static_cast<uint32_t>(kPatternSize), static_cast<uint32_t>(kPatternSize)}};
      VkPipelineViewportStateCreateInfo viewportState{};
      viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
      viewportState.viewportCount = 1;
      viewportState.pViewports = &viewport;
      viewportState.scissorCount = 1;
      viewportState.pScissors = &scissor;
      VkPipelineRasterizationStateCreateInfo raster{};
      raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
      raster.polygonMode = VK_POLYGON_MODE_FILL;
      raster.cullMode = VK_CULL_MODE_NONE;
      raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
      raster.lineWidth = 1.0f;
      VkPipelineMultisampleStateCreateInfo multisample{};
      multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
      multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
      VkPipelineColorBlendAttachmentState blendAttachment{};
      blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                       VK_COLOR_COMPONENT_A_BIT;
      VkPipelineColorBlendStateCreateInfo blend{};
      blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
      blend.attachmentCount = 1;
      blend.pAttachments = &blendAttachment;
      VkGraphicsPipelineCreateInfo pipelineInfo{};
      pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
      pipelineInfo.stageCount = 2;
      pipelineInfo.pStages = stages;
      pipelineInfo.pVertexInputState = &vertexInput;
      pipelineInfo.pInputAssemblyState = &assembly;
      pipelineInfo.pViewportState = &viewportState;
      pipelineInfo.pRasterizationState = &raster;
      pipelineInfo.pMultisampleState = &multisample;
      pipelineInfo.pColorBlendState = &blend;
      pipelineInfo.layout = rd.layout;
      pipelineInfo.renderPass = rd.renderPass;
      VkPipelineCacheCreateInfo cacheInfo{};
      cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
      if (!check(rd.fn.vkCreatePipelineCache(rd.device, &cacheInfo, nullptr, &rd.cache), "vkCreatePipelineCache")) return false;
      t = NowMs();
      if (!check(rd.fn.vkCreateGraphicsPipelines(rd.device, rd.cache, 1, &pipelineInfo, nullptr, &rd.pipeline),
                 "vkCreateGraphicsPipelines")) return false;
      pipelineMs = NowMs() - t;
      // The same pipeline again through the now warm cache (what a persisted pipeline cache saves).
      VkPipeline again = VK_NULL_HANDLE;
      t = NowMs();
      if (rd.fn.vkCreateGraphicsPipelines(rd.device, rd.cache, 1, &pipelineInfo, nullptr, &again) == VK_SUCCESS) {
        pipelineCachedMs = NowMs() - t;
        rd.fn.vkDestroyPipeline(rd.device, again, nullptr);
      }

      // Readback buffer.
      const VkDeviceSize bytes = static_cast<VkDeviceSize>(kPatternSize) * kPatternSize * 4;
      VkBufferCreateInfo bufferInfo{};
      bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferInfo.size = bytes;
      bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      if (!check(rd.fn.vkCreateBuffer(rd.device, &bufferInfo, nullptr, &rd.buffer), "vkCreateBuffer")) return false;
      VkMemoryRequirements bufferReq{};
      rd.fn.vkGetBufferMemoryRequirements(rd.device, rd.buffer, &bufferReq);
      const int bufferType = MemoryType(memory, bufferReq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
      step = "buffer memory type";
      if (bufferType < 0) return fail("no host-visible memory type for the readback buffer");
      const bool coherent = (memory.memoryTypes[bufferType].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
      alloc.allocationSize = bufferReq.size;
      alloc.memoryTypeIndex = static_cast<uint32_t>(bufferType);
      if (!check(rd.fn.vkAllocateMemory(rd.device, &alloc, nullptr, &rd.bufferMemory), "vkAllocateMemory(buffer)")) return false;
      if (!check(rd.fn.vkBindBufferMemory(rd.device, rd.buffer, rd.bufferMemory, 0), "vkBindBufferMemory")) return false;

      // Commands.
      VkCommandPoolCreateInfo poolInfo{};
      poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
      poolInfo.queueFamilyIndex = family;
      if (!check(rd.fn.vkCreateCommandPool(rd.device, &poolInfo, nullptr, &rd.pool), "vkCreateCommandPool")) return false;
      VkCommandBufferAllocateInfo cbInfo{};
      cbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      cbInfo.commandPool = rd.pool;
      cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      cbInfo.commandBufferCount = 1;
      VkCommandBuffer cb = VK_NULL_HANDLE;
      if (!check(rd.fn.vkAllocateCommandBuffers(rd.device, &cbInfo, &cb), "vkAllocateCommandBuffers")) return false;
      const bool timestamps = timestampBits > 0 && props.limits.timestampComputeAndGraphics;
      if (timestamps) {
        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 2;
        if (rd.fn.vkCreateQueryPool(rd.device, &queryInfo, nullptr, &rd.queries) != VK_SUCCESS) {
          rd.queries = VK_NULL_HANDLE;
        }
      }
      VkCommandBufferBeginInfo begin{};
      begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      if (!check(rd.fn.vkBeginCommandBuffer(cb, &begin), "vkBeginCommandBuffer")) return false;
      if (rd.queries != VK_NULL_HANDLE) {
        rd.fn.vkCmdResetQueryPool(cb, rd.queries, 0, 2);
        rd.fn.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, rd.queries, 0);
      }
      VkClearValue clear{};
      clear.color.float32[3] = 1.0f;
      VkRenderPassBeginInfo rpBegin{};
      rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
      rpBegin.renderPass = rd.renderPass;
      rpBegin.framebuffer = rd.framebuffer;
      rpBegin.renderArea = scissor;
      rpBegin.clearValueCount = 1;
      rpBegin.pClearValues = &clear;
      rd.fn.vkCmdBeginRenderPass(cb, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
      rd.fn.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, rd.pipeline);
      rd.fn.vkCmdDraw(cb, 3, 1, 0, 0);
      rd.fn.vkCmdEndRenderPass(cb);
      if (rd.queries != VK_NULL_HANDLE) {
        rd.fn.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, rd.queries, 1);
      }
      VkBufferImageCopy region{};
      region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      region.imageExtent = {static_cast<uint32_t>(kPatternSize), static_cast<uint32_t>(kPatternSize), 1};
      rd.fn.vkCmdCopyImageToBuffer(cb, rd.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rd.buffer, 1, &region);
      VkBufferMemoryBarrier toHost{};
      toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      toHost.buffer = rd.buffer;
      toHost.size = VK_WHOLE_SIZE;
      rd.fn.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &toHost, 0,
                                 nullptr);
      if (!check(rd.fn.vkEndCommandBuffer(cb), "vkEndCommandBuffer")) return false;
      VkFenceCreateInfo fenceInfo{};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      if (!check(rd.fn.vkCreateFence(rd.device, &fenceInfo, nullptr, &rd.fence), "vkCreateFence")) return false;
      VkSubmitInfo submit{};
      submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit.commandBufferCount = 1;
      submit.pCommandBuffers = &cb;
      t = NowMs();
      if (!check(rd.fn.vkQueueSubmit(queue, 1, &submit, rd.fence), "vkQueueSubmit")) return false;
      if (!check(rd.fn.vkWaitForFences(rd.device, 1, &rd.fence, VK_TRUE, 10ull * 1000 * 1000 * 1000), "vkWaitForFences")) return false;
      submitMs = NowMs() - t;
      if (rd.queries != VK_NULL_HANDLE) {
        uint64_t stamps[2] = {};
        if (rd.fn.vkGetQueryPoolResults(rd.device, rd.queries, 0, 2, sizeof(stamps), stamps, sizeof(uint64_t),
                                        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
          const uint64_t mask = timestampBits >= 64 ? ~0ull : ((1ull << timestampBits) - 1ull);
          const uint64_t ticks = ((stamps[1] & mask) - (stamps[0] & mask)) & mask;
          gpuMs = static_cast<double>(ticks) * static_cast<double>(props.limits.timestampPeriod) / 1.0e6;
        }
      }

      void* mapped = nullptr;
      if (!check(rd.fn.vkMapMemory(rd.device, rd.bufferMemory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory")) return false;
      if (!coherent) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = rd.bufferMemory;
        range.size = VK_WHOLE_SIZE;
        rd.fn.vkInvalidateMappedMemoryRanges(rd.device, 1, &range);
      }
      const uint8_t* rows = static_cast<const uint8_t*>(mapped);
      const size_t pitch = static_cast<size_t>(kPatternSize) * 4;
      pattern = CheckPattern(rows, pitch);
      Json samples;
      const int points[][2] = {{0, 0}, {255, 0}, {0, 255}, {255, 255}, {128, 64}};
      for (const auto& pt : points) {
        const uint8_t* px = rows + static_cast<size_t>(pt[1]) * pitch + static_cast<size_t>(pt[0]) * 4;
        char key[16];
        snprintf(key, sizeof(key), "%d,%d", pt[0], pt[1]);
        char value[48];
        snprintf(value, sizeof(value), "[%u,%u,%u,%u]", px[0], px[1], px[2], px[3]);
        samples.Raw(key, value);
      }
      pixels = samples.Text();
      png = "deviceprobe-vulkan.png";
      if (!WritePng(options.outDir + "/" + png, kPatternSize, kPatternSize, rows, pitch, false, pngError)) {
        png.clear();
      }
      rd.fn.vkUnmapMemory(rd.device, rd.bufferMemory);
      step = "done";
      return true;
    }();

    j.Str("status", ok ? "ok" : "error");
    if (!ok) {
      j.Str("step", step);
      j.Str("error", error);
    }
    j.Str("device", deviceName);
    j.Bool("device_created", rd.device != VK_NULL_HANDLE);
    j.Real("create_instance_ms", vk.instanceMs, 2);
    j.Real("create_device_ms", createDeviceMs, 2);
    j.Real("create_pipeline_ms", pipelineMs, 2);
    j.Real("create_pipeline_cached_ms", pipelineCachedMs, 2);
    j.Real("submit_to_fence_ms", submitMs, 2);
    if (gpuMs >= 0.0) {
      j.Real("gpu_ms", gpuMs, 3);
    } else {
      j.Null("gpu_ms");
    }
    if (ok) {
      j.Str("pattern", pattern.verdict);
      j.Num("mismatched_pixels", pattern.mismatched);
      j.Num("max_channel_error", pattern.maxError);
      if (pattern.mismatched > 0) {
        j.Raw("first_mismatch", "[" + std::to_string(pattern.firstX) + "," + std::to_string(pattern.firstY) + "]");
      }
      j.Raw("pixels", pixels);
      if (!png.empty()) {
        j.Str("png", png);
      } else {
        j.Null("png");
        j.Str("png_error", pngError);
      }
    }
    j.Real("ms", NowMs() - t0, 1);
    result.json = j.Text();
    char line[512];
    if (ok) {
      snprintf(line, sizeof(line),
               "Vulkan render on %s: pattern %s (%d of %d pixels off), device %.1f ms, pipeline %.1f ms (cached %.1f ms), "
               "submit+wait %.1f ms%s%s",
               deviceName.c_str(), pattern.verdict.c_str(), pattern.mismatched, kPatternSize * kPatternSize, createDeviceMs,
               pipelineMs, pipelineCachedMs, submitMs, png.empty() ? ", no PNG: " : ", ", png.empty() ? pngError.c_str() : png.c_str());
    } else {
      snprintf(line, sizeof(line), "Vulkan render failed at %s: %s", step.c_str(), error.c_str());
    }
    result.summary = line;
    return result;
  }
} // namespace faf_probe
