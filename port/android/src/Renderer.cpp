#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <format>
#include <iterator>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "Buffer.h"
#include "DataBlob.h"
#include "DebugOutput.h"
#include "DeviceContext.h"
#include "EngineFactoryOpenGL.h"
#include "EngineFactoryVk.h"
#include "GraphicsTypes.h"
#include "PipelineState.h"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "RenderDeviceGLES.h"
#include "Shader.h"
#include "ShaderResourceBinding.h"
#include "SwapChain.h"
#include "Texture.h"

#include "Log.h"

namespace faf::android {

  namespace dg = Diligent;

  namespace {

    // --- Diligent messages ---------------------------------------------------------

    std::mutex& DiligentErrorMutex()
    {
      static std::mutex mutex;
      return mutex;
    }

    std::string& LastDiligentError()
    {
      static std::string error;
      return error;
    }

    /// Diligent reports why a device or swap chain could not be created only
    /// through its message callback; the last error is kept for the status.
    void DILIGENT_CALL_TYPE OnDiligentMessage(
      const dg::DEBUG_MESSAGE_SEVERITY severity,
      const dg::Char* message,
      const dg::Char* function,
      const dg::Char* /*file*/,
      const int /*line*/
    )
    {
      const std::string_view text = message != nullptr ? message : "";
      LogLevel level = LogLevel::Info;
      switch (severity) {
      case dg::DEBUG_MESSAGE_SEVERITY_INFO:
        level = LogLevel::Info;
        break;
      case dg::DEBUG_MESSAGE_SEVERITY_WARNING:
        level = LogLevel::Warning;
        break;
      case dg::DEBUG_MESSAGE_SEVERITY_ERROR:
      case dg::DEBUG_MESSAGE_SEVERITY_FATAL_ERROR:
        level = LogLevel::Error;
        break;
      }
      if (level == LogLevel::Error) {
        const std::lock_guard lock(DiligentErrorMutex());
        LastDiligentError().assign(text);
      }
      if (function != nullptr && level != LogLevel::Info) {
        Log::Get().Write(level, std::format("Diligent: {} ({})", text, function));
      } else {
        Log::Get().Write(level, std::format("Diligent: {}", text));
      }
    }

    void ClearDiligentError()
    {
      const std::lock_guard lock(DiligentErrorMutex());
      LastDiligentError().clear();
    }

    /// " (<last Diligent error>)" or "".
    std::string DiligentErrorSuffix()
    {
      const std::lock_guard lock(DiligentErrorMutex());
      const std::string& error = LastDiligentError();
      return error.empty() ? std::string() : " (" + error + ")";
    }

    // --- shaders -----------------------------------------------------------------------

    // One constant buffer for both stages; float4 members only, so the layout
    // is the same under HLSL packing, std140 (GLES) and SPIR-V.
    struct ShaderConstants
    {
      float rotation[4];   ///< Physical NDC -> logical NDC: x' = dot(p, xy), y' = dot(p, zw).
      float imageRect[4];  ///< xy: half extent of the image in logical NDC; z: 1 = draw the image.
      float background[4]; ///< Colour around (and without) the image.
      float barRect[4];    ///< Progress bar in logical NDC: left, bottom, right, top.
      float barFill[4];    ///< x..y: filled part of the bar, as fractions of its width.
      float barColor[4];
      float trackColor[4];
    };
    static_assert(sizeof(ShaderConstants) == 7 * 16, "ShaderConstants must be a sequence of float4");

    constexpr char kConstantsHlsl[] = R"(
cbuffer Constants
{
    float4 g_Rotation;
    float4 g_ImageRect;
    float4 g_Background;
    float4 g_BarRect;
    float4 g_BarFill;
    float4 g_BarColor;
    float4 g_TrackColor;
};

struct PSInput
{
    float4 Pos     : SV_POSITION;
    float2 Logical : TEX_COORD;
};
)";

    // A triangle that covers the viewport; its interpolated "Logical"
    // coordinate is the NDC position in the orientation the user sees.
    constexpr char kVertexShaderHlsl[] = R"(
void main(in uint VertId : SV_VertexID, out PSInput PSIn)
{
    float2 Corners[3];
    Corners[0] = float2(-1.0, -1.0);
    Corners[1] = float2(-1.0,  3.0);
    Corners[2] = float2( 3.0, -1.0);
    float2 Ndc = Corners[VertId];
    PSIn.Pos     = float4(Ndc, 0.0, 1.0);
    PSIn.Logical = float2(dot(Ndc, g_Rotation.xy), dot(Ndc, g_Rotation.zw));
}
)";

    // SampleLevel, not Sample: the lookup sits in non-uniform control flow
    // (inside the image rectangle only), where implicit derivatives are undefined.
    constexpr char kPixelShaderHlsl[] = R"(
Texture2D    g_Texture;
SamplerState g_Texture_sampler;

struct PSOutput
{
    float4 Color : SV_TARGET;
};

void main(in PSInput PSIn, out PSOutput PSOut)
{
    float2 L = PSIn.Logical;
    float4 Color = g_Background;
    if (g_ImageRect.z > 0.5)
    {
        float2 Rel = L / g_ImageRect.xy;
        if (abs(Rel.x) <= 1.0 && abs(Rel.y) <= 1.0)
        {
            float2 UV = float2(Rel.x * 0.5 + 0.5, 0.5 - Rel.y * 0.5);
            Color = g_Texture.SampleLevel(g_Texture_sampler, UV, 0.0);
        }
    }
    if (L.x >= g_BarRect.x && L.x <= g_BarRect.z && L.y >= g_BarRect.y && L.y <= g_BarRect.w)
    {
        float T = (L.x - g_BarRect.x) / (g_BarRect.z - g_BarRect.x);
        Color = (T >= g_BarFill.x && T <= g_BarFill.y) ? g_BarColor : g_TrackColor;
    }
    PSOut.Color = float4(Color.rgb, 1.0);
}
)";

    std::string BlobText(dg::IDataBlob* blob)
    {
      if (blob == nullptr || blob->GetSize() == 0) {
        return {};
      }
      std::string text(static_cast<const char*>(blob->GetConstDataPtr()), blob->GetSize());
      while (!text.empty() && (text.back() == '\0' || text.back() == '\n')) {
        text.pop_back();
      }
      return text;
    }

    // --- frame layout ----------------------------------------------------------------

    struct Transform
    {
      float rotation[4];
      bool swapsAxes;
    };

    /// The inverse of the swap chain's pre-transform: maps a position in the
    /// presented (physical) image to the orientation the user sees. The
    /// transform describes how the content was rotated clockwise before
    /// presentation; NDC here has +y up (Diligent flips Vulkan's y).
    Transform InversePreTransform(const dg::SURFACE_TRANSFORM transform)
    {
      switch (transform) {
      case dg::SURFACE_TRANSFORM_ROTATE_90:
        return {{0.0f, -1.0f, 1.0f, 0.0f}, true};
      case dg::SURFACE_TRANSFORM_ROTATE_180:
        return {{-1.0f, 0.0f, 0.0f, -1.0f}, false};
      case dg::SURFACE_TRANSFORM_ROTATE_270:
        return {{0.0f, 1.0f, -1.0f, 0.0f}, true};
      case dg::SURFACE_TRANSFORM_HORIZONTAL_MIRROR:
        return {{-1.0f, 0.0f, 0.0f, 1.0f}, false};
      case dg::SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90:
        return {{0.0f, 1.0f, 1.0f, 0.0f}, true};
      case dg::SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180:
        return {{1.0f, 0.0f, 0.0f, -1.0f}, false};
      case dg::SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270:
        return {{0.0f, -1.0f, -1.0f, 0.0f}, true};
      case dg::SURFACE_TRANSFORM_OPTIMAL:
      case dg::SURFACE_TRANSFORM_IDENTITY:
        break;
      }
      return {{1.0f, 0.0f, 0.0f, 1.0f}, false};
    }

    const char* TransformName(const dg::SURFACE_TRANSFORM transform)
    {
      switch (transform) {
      case dg::SURFACE_TRANSFORM_OPTIMAL:
        return "optimal";
      case dg::SURFACE_TRANSFORM_IDENTITY:
        return "identity";
      case dg::SURFACE_TRANSFORM_ROTATE_90:
        return "rotate 90";
      case dg::SURFACE_TRANSFORM_ROTATE_180:
        return "rotate 180";
      case dg::SURFACE_TRANSFORM_ROTATE_270:
        return "rotate 270";
      case dg::SURFACE_TRANSFORM_HORIZONTAL_MIRROR:
        return "mirror";
      case dg::SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90:
        return "mirror + rotate 90";
      case dg::SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180:
        return "mirror + rotate 180";
      case dg::SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270:
        return "mirror + rotate 270";
      }
      return "?";
    }

    void SetColor(float (&out)[4], const float r, const float g, const float b)
    {
      out[0] = r;
      out[1] = g;
      out[2] = b;
      out[3] = 1.0f;
    }

    /// Colours are written as they should appear: the swap chain and the
    /// texture are UNORM (not sRGB), so sRGB values pass through unchanged.
    ShaderConstants ComputeConstants(
      const FrameParams& params,
      const dg::SwapChainDesc& desc,
      const bool hasImage,
      const int imageWidth,
      const int imageHeight
    )
    {
      ShaderConstants c{};
      const Transform transform = InversePreTransform(desc.PreTransform);
      std::copy(std::begin(transform.rotation), std::end(transform.rotation), std::begin(c.rotation));
      const float width = static_cast<float>(transform.swapsAxes ? desc.Height : desc.Width);
      const float height = static_cast<float>(transform.swapsAxes ? desc.Width : desc.Height);

      if (hasImage && imageWidth > 0 && imageHeight > 0) {
        // Fit the whole image (letterbox or pillarbox), keep its aspect ratio.
        const float screenAspect = width / height;
        const float imageAspect = static_cast<float>(imageWidth) / static_cast<float>(imageHeight);
        c.imageRect[0] = screenAspect > imageAspect ? imageAspect / screenAspect : 1.0f;
        c.imageRect[1] = screenAspect > imageAspect ? 1.0f : screenAspect / imageAspect;
        c.imageRect[2] = 1.0f;
        SetColor(c.background, 0.0f, 0.0f, 0.0f);
      } else {
        // Slowly breathing dark blue: shows at a glance that the app is alive.
        const float pulse = 0.5f + 0.5f * std::sin(params.seconds * 1.6f);
        SetColor(c.background, 0.035f + 0.015f * pulse, 0.060f + 0.025f * pulse, 0.110f + 0.045f * pulse);
      }

      // An empty rectangle (right < left) hides the bar.
      c.barRect[0] = 1.0f;
      c.barRect[2] = -1.0f;
      if (params.progress >= 0.0f) {
        const float barWidth = std::round(width * 0.45f);
        const float barHeight = std::max(4.0f, std::round(height * 0.008f));
        const float centerY = 1.0f - 2.0f * 0.82f; // 82 % down the screen
        c.barRect[0] = -barWidth / width;
        c.barRect[1] = centerY - barHeight / height;
        c.barRect[2] = barWidth / width;
        c.barRect[3] = centerY + barHeight / height;
        c.barFill[0] = 0.0f;
        c.barFill[1] = std::clamp(params.progress, 0.0f, 1.0f);
        SetColor(c.barColor, 0.36f, 0.64f, 0.96f);
        SetColor(c.trackColor, 0.15f, 0.19f, 0.27f);
      }
      return c;
    }

    /// Halves an RGBA8 image (2x2 box filter; an odd last row/column is kept).
    faf::port::Image Halve(const faf::port::Image& in)
    {
      faf::port::Image out;
      out.format = in.format;
      out.width = std::max(1, in.width / 2);
      out.height = std::max(1, in.height / 2);
      out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4);
      const auto pixel = [&](const int x, const int y, const int channel) {
        const int cx = std::min(x, in.width - 1);
        const int cy = std::min(y, in.height - 1);
        return static_cast<unsigned>(
          in.rgba[(static_cast<std::size_t>(cy) * static_cast<std::size_t>(in.width) + static_cast<std::size_t>(cx)) *
                    4 +
                  static_cast<std::size_t>(channel)]
        );
      };
      for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
          for (int channel = 0; channel < 4; ++channel) {
            const unsigned sum = pixel(2 * x, 2 * y, channel) + pixel(2 * x + 1, 2 * y, channel) +
              pixel(2 * x, 2 * y + 1, channel) + pixel(2 * x + 1, 2 * y + 1, channel);
            out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)) *
                       4 +
                     static_cast<std::size_t>(channel)] = static_cast<std::uint8_t>((sum + 2) / 4);
          }
        }
      }
      return out;
    }

    dg::SwapChainDesc MakeSwapChainDesc()
    {
      dg::SwapChainDesc desc;
      // UNORM, not the default sRGB: the picture is copied, never blended or
      // lit, and EGL window surfaces are not sRGB-capable everywhere, so both
      // backends pass the image's sRGB values through unchanged.
      desc.ColorBufferFormat = dg::TEX_FORMAT_RGBA8_UNORM;
      desc.DepthBufferFormat = dg::TEX_FORMAT_UNKNOWN;
      desc.PreTransform = dg::SURFACE_TRANSFORM_OPTIMAL;
      return desc;
    }

  } // namespace

  struct Renderer::Impl
  {
    Backend backend = Backend::Vulkan;
    dg::IEngineFactoryVk* factoryVk = nullptr;
    dg::RefCntAutoPtr<dg::IRenderDevice> device;
    dg::RefCntAutoPtr<dg::IDeviceContext> context;
    dg::RefCntAutoPtr<dg::ISwapChain> swapChain;
    dg::RefCntAutoPtr<dg::IPipelineState> pipeline;
    dg::TEXTURE_FORMAT pipelineFormat = dg::TEX_FORMAT_UNKNOWN;
    dg::RefCntAutoPtr<dg::IShaderResourceBinding> binding;
    dg::RefCntAutoPtr<dg::IBuffer> constants;
    dg::RefCntAutoPtr<dg::ITexture> placeholder;
    dg::RefCntAutoPtr<dg::ITexture> image;
    int imageWidth = 0;
    int imageHeight = 0;
    /// The window the surface was made for; null while there is none (the
    /// OpenGL ES swap chain object outlives its surface).
    ANativeWindow* window = nullptr;

    void ReleaseAll()
    {
      binding.Release();
      pipeline.Release();
      pipelineFormat = dg::TEX_FORMAT_UNKNOWN;
      constants.Release();
      image.Release();
      placeholder.Release();
      imageWidth = 0;
      imageHeight = 0;
      if (context) {
        context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        context->Flush();
        context->WaitForIdle();
      }
      swapChain.Release();
      context.Release();
      device.Release();
      window = nullptr;
    }

    bool CreateVulkan(ANativeWindow* nativeWindow, std::string& error)
    {
      factoryVk = dg::GetEngineFactoryVk();
      if (factoryVk == nullptr) {
        error = "Diligent's Vulkan backend is not available";
        return false;
      }
      factoryVk->SetMessageCallback(OnDiligentMessage);
      dg::EngineVkCreateInfo createInfo;
      factoryVk->CreateDeviceAndContextsVk(createInfo, &device, &context);
      if (!device || !context) {
        error = "no usable Vulkan device" + DiligentErrorSuffix();
        return false;
      }
      return CreateSwapChainVk(nativeWindow, error);
    }

    bool CreateSwapChainVk(ANativeWindow* nativeWindow, std::string& error)
    {
      dg::NativeWindow handle;
      handle.pAWindow = nativeWindow;
      factoryVk->CreateSwapChainVk(device, context, MakeSwapChainDesc(), handle, &swapChain);
      if (!swapChain) {
        error = "cannot create a Vulkan swap chain" + DiligentErrorSuffix();
        return false;
      }
      window = nativeWindow;
      return true;
    }

    bool CreateGles(ANativeWindow* nativeWindow, std::string& error)
    {
      dg::IEngineFactoryOpenGL* factory = dg::GetEngineFactoryOpenGL();
      if (factory == nullptr) {
        error = "Diligent's OpenGL ES backend is not available";
        return false;
      }
      factory->SetMessageCallback(OnDiligentMessage);
      dg::EngineGLCreateInfo createInfo;
      createInfo.Window.pAWindow = nativeWindow;
      factory->CreateDeviceAndSwapChainGL(createInfo, &device, &context, MakeSwapChainDesc(), &swapChain);
      if (!device || !context || !swapChain) {
        error = "cannot create an OpenGL ES 3 context" + DiligentErrorSuffix();
        return false;
      }
      window = nativeWindow;
      return true;
    }

    [[nodiscard]] dg::RefCntAutoPtr<dg::IRenderDeviceGLES> Gles() const
    {
      return dg::RefCntAutoPtr<dg::IRenderDeviceGLES>{device, dg::IID_RenderDeviceGLES};
    }

    dg::RefCntAutoPtr<dg::ITexture> CreateRgbaTexture(
      const char* name,
      const std::uint8_t* pixels,
      const int width,
      const int height
    ) const
    {
      dg::TextureDesc desc;
      desc.Name = name;
      desc.Type = dg::RESOURCE_DIM_TEX_2D;
      desc.Width = static_cast<dg::Uint32>(width);
      desc.Height = static_cast<dg::Uint32>(height);
      desc.Format = dg::TEX_FORMAT_RGBA8_UNORM;
      desc.MipLevels = 1;
      desc.Usage = dg::USAGE_IMMUTABLE;
      desc.BindFlags = dg::BIND_SHADER_RESOURCE;

      dg::TextureSubResData level;
      level.pData = pixels;
      level.Stride = static_cast<dg::Uint64>(width) * 4;
      dg::TextureData data;
      data.pSubResources = &level;
      data.NumSubresources = 1;

      dg::RefCntAutoPtr<dg::ITexture> texture;
      device->CreateTexture(desc, &data, &texture);
      return texture;
    }

    /// A new binding (SRB) for `texture`. The texture variable is MUTABLE, so
    /// a different texture gets a new binding instead of rebinding the old one.
    bool BindTexture(dg::ITexture* texture, std::string& error)
    {
      dg::RefCntAutoPtr<dg::IShaderResourceBinding> next;
      pipeline->CreateShaderResourceBinding(&next, true);
      if (!next) {
        error = "cannot create a shader resource binding" + DiligentErrorSuffix();
        return false;
      }
      dg::IShaderResourceVariable* variable = next->GetVariableByName(dg::SHADER_TYPE_PIXEL, "g_Texture");
      if (variable == nullptr) {
        error = "the pixel shader has no g_Texture";
        return false;
      }
      variable->Set(texture->GetDefaultView(dg::TEXTURE_VIEW_SHADER_RESOURCE));
      binding = std::move(next);
      return true;
    }

    dg::RefCntAutoPtr<dg::IShader> CompileShader(
      const dg::SHADER_TYPE type,
      const char* name,
      const std::string& source,
      std::string& error
    ) const
    {
      dg::ShaderCreateInfo createInfo;
      createInfo.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_HLSL;
      createInfo.Desc.ShaderType = type;
      createInfo.Desc.Name = name;
      // Required by OpenGL ES, harmless on Vulkan: texture and sampler pair up
      // by the "_sampler" suffix.
      createInfo.Desc.UseCombinedTextureSamplers = true;
      createInfo.EntryPoint = "main";
      createInfo.Source = source.c_str();
      createInfo.SourceLength = source.size();

      dg::RefCntAutoPtr<dg::IShader> shader;
      dg::RefCntAutoPtr<dg::IDataBlob> output;
      device->CreateShader(createInfo, &shader, &output);
      if (!shader) {
        const std::string log = BlobText(output);
        error = std::string("cannot compile ") + name + (log.empty() ? DiligentErrorSuffix() : ": " + log);
      }
      return shader;
    }

    /// The pipeline, its constant buffer and a black placeholder texture (every
    /// variable must be bound before a draw, also before the image is there).
    bool CreatePipeline(std::string& error)
    {
      const dg::TEXTURE_FORMAT format = swapChain->GetDesc().ColorBufferFormat;
      const std::string vsSource = std::string(kConstantsHlsl) + kVertexShaderHlsl;
      const std::string psSource = std::string(kConstantsHlsl) + kPixelShaderHlsl;
      dg::RefCntAutoPtr<dg::IShader> vs = CompileShader(dg::SHADER_TYPE_VERTEX, "faf splash VS", vsSource, error);
      if (!vs) {
        return false;
      }
      dg::RefCntAutoPtr<dg::IShader> ps = CompileShader(dg::SHADER_TYPE_PIXEL, "faf splash PS", psSource, error);
      if (!ps) {
        return false;
      }

      dg::GraphicsPipelineStateCreateInfo createInfo;
      createInfo.PSODesc.Name = "faf splash";
      createInfo.PSODesc.PipelineType = dg::PIPELINE_TYPE_GRAPHICS;
      createInfo.GraphicsPipeline.NumRenderTargets = 1;
      createInfo.GraphicsPipeline.RTVFormats[0] = format;
      createInfo.GraphicsPipeline.DSVFormat = dg::TEX_FORMAT_UNKNOWN;
      createInfo.GraphicsPipeline.PrimitiveTopology = dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      // No winding to get right across backends and pre-rotation.
      createInfo.GraphicsPipeline.RasterizerDesc.CullMode = dg::CULL_MODE_NONE;
      createInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = dg::False;
      createInfo.pVS = vs;
      createInfo.pPS = ps;

      const dg::ShaderResourceVariableDesc variables[] = {
        {dg::SHADER_TYPE_PIXEL, "g_Texture", dg::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE},
      };
      dg::SamplerDesc linearClamp;
      linearClamp.MinFilter = dg::FILTER_TYPE_LINEAR;
      linearClamp.MagFilter = dg::FILTER_TYPE_LINEAR;
      linearClamp.MipFilter = dg::FILTER_TYPE_LINEAR;
      linearClamp.AddressU = dg::TEXTURE_ADDRESS_CLAMP;
      linearClamp.AddressV = dg::TEXTURE_ADDRESS_CLAMP;
      linearClamp.AddressW = dg::TEXTURE_ADDRESS_CLAMP;
      const dg::ImmutableSamplerDesc samplers[] = {
        {dg::SHADER_TYPE_PIXEL, "g_Texture", linearClamp},
      };
      createInfo.PSODesc.ResourceLayout.DefaultVariableType = dg::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
      createInfo.PSODesc.ResourceLayout.Variables = variables;
      createInfo.PSODesc.ResourceLayout.NumVariables = static_cast<dg::Uint32>(std::size(variables));
      createInfo.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
      createInfo.PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<dg::Uint32>(std::size(samplers));

      dg::RefCntAutoPtr<dg::IPipelineState> nextPipeline;
      device->CreateGraphicsPipelineState(createInfo, &nextPipeline);
      if (!nextPipeline) {
        error = "cannot create the pipeline state" + DiligentErrorSuffix();
        return false;
      }

      if (!constants) {
        dg::BufferDesc bufferDesc;
        bufferDesc.Name = "faf splash constants";
        bufferDesc.Size = sizeof(ShaderConstants);
        bufferDesc.Usage = dg::USAGE_DYNAMIC;
        bufferDesc.BindFlags = dg::BIND_UNIFORM_BUFFER;
        bufferDesc.CPUAccessFlags = dg::CPU_ACCESS_WRITE;
        device->CreateBuffer(bufferDesc, nullptr, &constants);
        if (!constants) {
          error = "cannot create the constant buffer" + DiligentErrorSuffix();
          return false;
        }
      }
      for (const dg::SHADER_TYPE stage : {dg::SHADER_TYPE_VERTEX, dg::SHADER_TYPE_PIXEL}) {
        if (dg::IShaderResourceVariable* variable = nextPipeline->GetStaticVariableByName(stage, "Constants")) {
          variable->Set(constants);
        }
      }

      if (!placeholder) {
        const std::uint8_t black[4] = {0, 0, 0, 255};
        placeholder = CreateRgbaTexture("faf placeholder", black, 1, 1);
        if (!placeholder) {
          error = "cannot create a texture" + DiligentErrorSuffix();
          return false;
        }
      }

      pipeline = std::move(nextPipeline);
      pipelineFormat = format;
      binding.Release();
      return BindTexture(image ? image.RawPtr() : placeholder.RawPtr(), error);
    }

    /// A new swap chain may come with a different format (Vulkan picks BGRA
    /// where RGBA is not offered); the pipeline must match it.
    bool EnsurePipelineMatchesSwapChain(std::string& error)
    {
      if (pipeline && swapChain && swapChain->GetDesc().ColorBufferFormat == pipelineFormat) {
        return true;
      }
      return CreatePipeline(error);
    }

    bool TryCreate(const Backend which, ANativeWindow* nativeWindow, std::string& error)
    {
      ClearDiligentError();
      backend = which;
      const bool created = which == Backend::Vulkan ? CreateVulkan(nativeWindow, error) : CreateGles(nativeWindow, error);
      if (!created || !CreatePipeline(error)) {
        ReleaseAll();
        return false;
      }
      return true;
    }
  };

  Renderer::Renderer()
    : mImpl(std::make_unique<Impl>())
  {}

  Renderer::~Renderer()
  {
    Destroy();
  }

  bool Renderer::Create(ANativeWindow* window, const Backend preferred, const bool allowFallback, std::string& error)
  {
    Impl& impl = *mImpl;
    impl.ReleaseAll();
    std::string vulkanError;
    try {
      if (preferred == Backend::Vulkan) {
        if (impl.TryCreate(Backend::Vulkan, window, vulkanError)) {
          return true;
        }
        if (!allowFallback) {
          error = "Vulkan: " + vulkanError;
          return false;
        }
        LogWarning("renderer: Vulkan is not usable ({}); falling back to OpenGL ES", vulkanError);
      }
      std::string glesError;
      if (impl.TryCreate(Backend::Gles, window, glesError)) {
        return true;
      }
      error = vulkanError.empty() ? "OpenGL ES: " + glesError : "Vulkan: " + vulkanError + "; OpenGL ES: " + glesError;
    } catch (const std::exception& e) {
      impl.ReleaseAll();
      error = std::string("graphics initialisation failed: ") + e.what();
    }
    return false;
  }

  bool Renderer::HasDevice() const
  {
    return mImpl->device != nullptr;
  }

  bool Renderer::HasSurface() const
  {
    return mImpl->swapChain != nullptr && mImpl->window != nullptr;
  }

  Backend Renderer::ActiveBackend() const
  {
    return mImpl->backend;
  }

  std::string Renderer::Description() const
  {
    const Impl& impl = *mImpl;
    if (!impl.device) {
      return "no device";
    }
    const dg::RenderDeviceInfo& info = impl.device->GetDeviceInfo();
    const dg::GraphicsAdapterInfo& adapter = impl.device->GetAdapterInfo();
    // Description is a fixed char array; formatting the array itself would
    // include its trailing NULs (and cut the log line at the first one).
    const std::string_view adapterName(adapter.Description, strnlen(adapter.Description, std::size(adapter.Description)));
    std::string text = std::format(
      "{} {}.{}, {}",
      impl.backend == Backend::Vulkan ? "Vulkan" : "OpenGL ES",
      info.APIVersion.Major,
      info.APIVersion.Minor,
      adapterName
    );
    if (impl.swapChain) {
      const dg::SwapChainDesc& desc = impl.swapChain->GetDesc();
      text += std::format(
        ", swap chain {}x{} {} buffers {}, pre-transform {}",
        desc.Width,
        desc.Height,
        desc.BufferCount,
        desc.ColorBufferFormat == dg::TEX_FORMAT_BGRA8_UNORM ? "BGRA8" : "RGBA8",
        TransformName(desc.PreTransform)
      );
    }
    return text;
  }

  bool Renderer::AttachWindow(ANativeWindow* window, std::string& error)
  {
    Impl& impl = *mImpl;
    if (!impl.device) {
      error = "no device";
      return false;
    }
    try {
      if (impl.backend == Backend::Vulkan) {
        impl.swapChain.Release();
        if (!impl.CreateSwapChainVk(window, error)) {
          return false;
        }
      } else {
        dg::RefCntAutoPtr<dg::IRenderDeviceGLES> gles = impl.Gles();
        if (!gles) {
          error = "the OpenGL ES device does not expose IRenderDeviceGLES";
          return false;
        }
        // Resume makes a new EGL surface for the window. Anything but success
        // means Diligent had to recreate the context, which loses every GL
        // object; this bring-up does not rebuild them.
        const EGLint result = gles->Resume(window);
        if (result != EGL_SUCCESS) {
          error = std::format("the OpenGL ES context was lost (EGL error 0x{:04x})", static_cast<unsigned>(result));
          return false;
        }
        impl.window = window;
        impl.swapChain->Resize(0, 0);
      }
      return impl.EnsurePipelineMatchesSwapChain(error);
    } catch (const std::exception& e) {
      error = std::string("cannot attach the window: ") + e.what();
      return false;
    }
  }

  void Renderer::DetachWindow()
  {
    Impl& impl = *mImpl;
    if (!impl.device || impl.window == nullptr) {
      return;
    }
    try {
      impl.context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
      impl.context->Flush();
      if (impl.backend == Backend::Vulkan) {
        // Destroys the VkSwapchainKHR and the VkSurfaceKHR; the window is
        // about to be destroyed by the system.
        impl.swapChain.Release();
      } else if (dg::RefCntAutoPtr<dg::IRenderDeviceGLES> gles = impl.Gles()) {
        gles->Suspend();
      }
    } catch (const std::exception& e) {
      LogWarning("renderer: detaching the window: {}", e.what());
    }
    impl.window = nullptr;
  }

  bool Renderer::UpdateSurface(std::string& error)
  {
    Impl& impl = *mImpl;
    if (!HasSurface()) {
      return true;
    }
    try {
      if (impl.backend == Backend::Vulkan) {
        // A new swap chain rather than ISwapChain::Resize: Diligent keeps the
        // surface extent it saw first on Android, which is stale after a real
        // resize (multi-window, foldables). Creation also picks up the
        // current transform. Resizes are rare, so the cost does not matter.
        ANativeWindow* window = impl.window;
        impl.context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        impl.context->Flush();
        impl.swapChain.Release();
        impl.window = nullptr;
        if (!impl.CreateSwapChainVk(window, error)) {
          return false;
        }
      } else {
        // The EGL surface follows the window; Resize(0, 0) reads its size.
        impl.swapChain->Resize(0, 0);
      }
      return impl.EnsurePipelineMatchesSwapChain(error);
    } catch (const std::exception& e) {
      error = std::string("cannot resize the swap chain: ") + e.what();
      return false;
    }
  }

  bool Renderer::SetImage(const faf::port::Image& source, std::string& error)
  {
    Impl& impl = *mImpl;
    if (!impl.device || !impl.pipeline) {
      error = "no device";
      return false;
    }
    if (source.width <= 0 || source.height <= 0 ||
        source.rgba.size() != static_cast<std::size_t>(source.width) * static_cast<std::size_t>(source.height) * 4) {
      error = "the background image is empty or inconsistent";
      return false;
    }
    try {
      const auto limit = static_cast<int>(impl.device->GetAdapterInfo().Texture.MaxTexture2DDimension);
      const faf::port::Image* upload = &source;
      faf::port::Image reduced;
      while (limit > 0 && (upload->width > limit || upload->height > limit)) {
        reduced = Halve(*upload);
        upload = &reduced;
      }
      if (upload != &source) {
        LogInfo(
          "renderer: background {}x{} exceeds the texture limit {}; using {}x{}",
          source.width,
          source.height,
          limit,
          upload->width,
          upload->height
        );
      }
      dg::RefCntAutoPtr<dg::ITexture> texture =
        impl.CreateRgbaTexture("faf main menu background", upload->rgba.data(), upload->width, upload->height);
      if (!texture) {
        error = "cannot create the background texture" + DiligentErrorSuffix();
        return false;
      }
      if (!impl.BindTexture(texture, error)) {
        return false;
      }
      impl.image = std::move(texture);
      // The aspect ratio is the source's; halving keeps it within a pixel.
      impl.imageWidth = source.width;
      impl.imageHeight = source.height;
      return true;
    } catch (const std::exception& e) {
      error = std::string("cannot upload the background: ") + e.what();
      return false;
    }
  }

  bool Renderer::HasImage() const
  {
    return mImpl->image != nullptr;
  }

  FrameResult Renderer::Render(const FrameParams& params, std::string& error)
  {
    Impl& impl = *mImpl;
    if (!HasSurface() || !impl.pipeline || !impl.binding) {
      return FrameResult::Skipped;
    }
    try {
      const dg::SwapChainDesc& desc = impl.swapChain->GetDesc();
      if (desc.Width == 0 || desc.Height == 0) {
        return FrameResult::Skipped;
      }
      dg::ITextureView* target = impl.swapChain->GetCurrentBackBufferRTV();
      if (target == nullptr) {
        return FrameResult::Skipped; // no image acquired (window changing)
      }
      const ShaderConstants constants =
        ComputeConstants(params, desc, impl.image != nullptr, impl.imageWidth, impl.imageHeight);

      impl.context->SetRenderTargets(1, &target, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      impl.context->ClearRenderTarget(target, constants.background, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

      void* mapped = nullptr;
      impl.context->MapBuffer(impl.constants, dg::MAP_WRITE, dg::MAP_FLAG_DISCARD, mapped);
      if (mapped == nullptr) {
        error = "cannot map the constant buffer" + DiligentErrorSuffix();
        return FrameResult::Failed;
      }
      std::memcpy(mapped, &constants, sizeof(constants));
      impl.context->UnmapBuffer(impl.constants, dg::MAP_WRITE);

      impl.context->SetPipelineState(impl.pipeline);
      impl.context->CommitShaderResources(impl.binding, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      dg::DrawAttribs draw;
      draw.NumVertices = 3;
      draw.Flags = dg::DRAW_FLAG_VERIFY_ALL;
      impl.context->Draw(draw);
      impl.swapChain->Present(1);
      return FrameResult::Presented;
    } catch (const std::exception& e) {
      error = std::string("rendering failed: ") + e.what();
      return FrameResult::Failed;
    }
  }

  void Renderer::Destroy()
  {
    try {
      mImpl->ReleaseAll();
    } catch (const std::exception& e) {
      LogWarning("renderer: shutdown: {}", e.what());
    }
  }

} // namespace faf::android
