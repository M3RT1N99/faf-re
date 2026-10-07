#include "DiligentHost.h"

#include <d3d11.h>
#include <d3d11sdklayers.h>

#include <float.h>

#include <algorithm>
#include <cstring>
#include <mutex>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"
#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"
#include "Graphics/GraphicsEngineD3D11/interface/EngineFactoryD3D11.h"
#include "Graphics/GraphicsEngineD3D11/interface/RenderDeviceD3D11.h"
#include "Primitives/interface/DebugOutput.h"

namespace dg = Diligent;

namespace gpg::gal::diligent
{
    namespace
    {
        // Diligent's message callback is a plain function pointer (DebugOutput.h:58), so its
        // counters are process-wide.
        std::mutex gMessageLock;
        DiligentMessageCounts gMessageCounts;
        std::vector<std::string> gMessages;
        constexpr std::size_t kKeptDiligentMessages = 32;

        void DILIGENT_CALL_TYPE OnDiligentMessage(
            const dg::DEBUG_MESSAGE_SEVERITY severity,
            const dg::Char* const message,
            const char* const function,
            const char* const file,
            const int line
        )
        {
            static_cast<void>(function);
            const char* tag = "info";
            std::lock_guard<std::mutex> lock(gMessageLock);
            switch (severity) {
            case dg::DEBUG_MESSAGE_SEVERITY_INFO:
                ++gMessageCounts.info;
                break;
            case dg::DEBUG_MESSAGE_SEVERITY_WARNING:
                ++gMessageCounts.warning;
                tag = "warning";
                break;
            case dg::DEBUG_MESSAGE_SEVERITY_ERROR:
                ++gMessageCounts.error;
                tag = "error";
                break;
            default:
                ++gMessageCounts.fatal;
                tag = "fatal";
                break;
            }
            ::OutputDebugStringA(message != nullptr ? message : "");
            ::OutputDebugStringA("\n");
            if (severity != dg::DEBUG_MESSAGE_SEVERITY_INFO && gMessages.size() < kKeptDiligentMessages) {
                std::string text = std::string(tag) + ": " + (message != nullptr ? message : "");
                if (file != nullptr) {
                    text += " (" + std::string(file) + ":" + std::to_string(line) + ")";
                }
                gMessages.push_back(std::move(text));
            }
        }

        // A triangle covering the target, generated from SV_VertexID: no vertex buffer.
        constexpr const char* kPresentVS = R"(
struct VSOutput { float4 position : SV_POSITION; };
VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}
)";

        // Texel for pixel: the head target and the back buffer have the same size, so Load at the
        // pixel position copies exactly, with no sampler and no filtering.
        constexpr const char* kPresentPS = R"(
Texture2D g_Head;
struct VSOutput { float4 position : SV_POSITION; };
float4 main(VSOutput input) : SV_TARGET
{
    return g_Head.Load(int3(int2(input.position.xy), 0));
}
)";
    } // namespace

    // ---------------------------------------------------------------------------------------------

    ScopedDefaultFpu::ScopedDefaultFpu()
    {
        unsigned int unused = 0;
        ::_controlfp_s(&mSaved, 0, 0);
        ::_controlfp_s(&unused, _PC_53, _MCW_PC);
    }

    ScopedDefaultFpu::~ScopedDefaultFpu()
    {
        unsigned int unused = 0;
        ::_controlfp_s(&unused, mSaved & _MCW_PC, _MCW_PC);
    }

    GpuShared::GpuShared(dg::IRenderDevice* const device, dg::IDeviceContext* const context, const std::uint32_t renderThreadId)
        : device_(device),
          context_(context),
          renderThreadId_(renderThreadId)
    {
        if (device_ != nullptr) {
            device_->AddRef();
        }
        if (context_ != nullptr) {
            context_->AddRef();
        }
    }

    GpuShared::~GpuShared()
    {
        if (context_ != nullptr) {
            context_->Release();
        }
        if (device_ != nullptr) {
            device_->Release();
        }
    }

    bool GpuShared::OnRenderThread() const
    {
        return ::GetCurrentThreadId() == renderThreadId_;
    }

    // ---------------------------------------------------------------------------------------------

    struct DiligentHost::Impl
    {
        dg::RefCntAutoPtr<dg::IRenderDevice> device;
        dg::RefCntAutoPtr<dg::IDeviceContext> context;
        dg::RefCntAutoPtr<dg::ISwapChain> swapChain;
        dg::RefCntAutoPtr<dg::ITexture> head;
        dg::RefCntAutoPtr<dg::IPipelineState> presentPipeline;
        dg::RefCntAutoPtr<dg::IShaderResourceBinding> presentBinding;
        // Staging textures for ReadTexture, one per (format, size).
        std::vector<dg::RefCntAutoPtr<dg::ITexture>> readbacks;
        std::shared_ptr<GpuShared> gpu;
        ID3D11InfoQueue* infoQueue = nullptr;
        DebugLayerCounts debugCounts;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool CreateHead(std::string* error)
        {
            presentBinding.Release();
            head.Release();

            dg::TextureDesc desc;
            desc.Name = "gal head 0";
            desc.Type = dg::RESOURCE_DIM_TEX_2D;
            desc.Width = width;
            desc.Height = height;
            // The D3D9 back buffer is A8R8G8B8 (D3D9Interfaces.cpp:1918). RGBA8_UNORM holds the same
            // values; GetRenderTargetData swaps to D3D9's byte order. UNORM, not SRGB: FA renders in
            // gamma space (m6u-DIL.txt 9).
            desc.Format = dg::TEX_FORMAT_RGBA8_UNORM;
            desc.MipLevels = 1;
            desc.Usage = dg::USAGE_DEFAULT;
            desc.BindFlags = dg::BIND_RENDER_TARGET | dg::BIND_SHADER_RESOURCE;
            desc.ClearValue.Format = desc.Format;
            device->CreateTexture(desc, nullptr, &head);
            if (!head) {
                *error = "cannot create the head render target";
                return false;
            }

            presentPipeline->CreateShaderResourceBinding(&presentBinding, true);
            dg::IShaderResourceVariable* const variable =
                presentBinding->GetVariableByName(dg::SHADER_TYPE_PIXEL, "g_Head");
            if (variable == nullptr) {
                *error = "the present shader has no g_Head";
                return false;
            }
            variable->Set(head->GetDefaultView(dg::TEXTURE_VIEW_SHADER_RESOURCE));
            return true;
        }

        bool CreatePresentPipeline(std::string* error)
        {
            dg::ShaderCreateInfo shaderInfo;
            shaderInfo.SourceLanguage = dg::SHADER_SOURCE_LANGUAGE_HLSL;
            shaderInfo.EntryPoint = "main";

            dg::RefCntAutoPtr<dg::IShader> vs;
            shaderInfo.Desc.ShaderType = dg::SHADER_TYPE_VERTEX;
            shaderInfo.Desc.Name = "gal present VS";
            shaderInfo.Source = kPresentVS;
            device->CreateShader(shaderInfo, &vs);

            dg::RefCntAutoPtr<dg::IShader> ps;
            shaderInfo.Desc.ShaderType = dg::SHADER_TYPE_PIXEL;
            shaderInfo.Desc.Name = "gal present PS";
            shaderInfo.Source = kPresentPS;
            device->CreateShader(shaderInfo, &ps);
            if (!vs || !ps) {
                *error = "cannot compile the present shaders";
                return false;
            }

            dg::GraphicsPipelineStateCreateInfo info;
            info.PSODesc.Name = "gal present";
            info.PSODesc.PipelineType = dg::PIPELINE_TYPE_GRAPHICS;
            info.GraphicsPipeline.NumRenderTargets = 1;
            info.GraphicsPipeline.RTVFormats[0] = swapChain->GetDesc().ColorBufferFormat;
            info.GraphicsPipeline.DSVFormat = dg::TEX_FORMAT_UNKNOWN;
            info.GraphicsPipeline.PrimitiveTopology = dg::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            info.GraphicsPipeline.RasterizerDesc.CullMode = dg::CULL_MODE_NONE;
            info.GraphicsPipeline.DepthStencilDesc.DepthEnable = dg::False;
            info.pVS = vs;
            info.pPS = ps;
            info.PSODesc.ResourceLayout.DefaultVariableType = dg::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
            device->CreateGraphicsPipelineState(info, &presentPipeline);
            if (!presentPipeline) {
                *error = "cannot create the present pipeline";
                return false;
            }
            return true;
        }
    };

    DiligentHost::DiligentHost()
        : mImpl(std::make_unique<Impl>())
    {}

    DiligentHost::~DiligentHost()
    {
        Destroy();
    }

    bool DiligentHost::Create(
        void* const window,
        const std::uint32_t width,
        const std::uint32_t height,
        const bool validation,
        std::string* const error
    )
    {
        Destroy();
        ScopedDefaultFpu fpu;

        dg::IEngineFactoryD3D11* const factory = dg::GetEngineFactoryD3D11();
        if (factory == nullptr) {
            *error = "no Diligent D3D11 engine factory";
            return false;
        }
        // No MessageBoxA(MB_SETFOREGROUND) on a failed assertion (Win32Debug.cpp:84-89): the
        // message goes to the callback and the log, and the run continues.
        factory->SetBreakOnError(false);
        factory->SetMessageCallback(OnDiligentMessage);

        dg::EngineD3D11CreateInfo engineInfo;
        engineInfo.SetValidationLevel(validation ? dg::VALIDATION_LEVEL_1 : dg::VALIDATION_LEVEL_DISABLED);
        // Feature level 11.0: what every adapter the engine supports has; Diligent defaults to the
        // highest available otherwise.
        engineInfo.GraphicsAPIVersion = dg::Version{11, 0};

        dg::IRenderDevice* device = nullptr;
        dg::IDeviceContext* context = nullptr;
        factory->CreateDeviceAndContextsD3D11(engineInfo, &device, &context);
        mImpl->device.Attach(device);
        mImpl->context.Attach(context);
        if (!mImpl->device || !mImpl->context) {
            *error = "cannot create the D3D11 device";
            Destroy();
            return false;
        }

        // The debug layer exists only when the device was created with D3D11_CREATE_DEVICE_DEBUG.
        dg::RefCntAutoPtr<dg::IRenderDeviceD3D11> deviceD3D11(mImpl->device, dg::IID_RenderDeviceD3D11);
        if (deviceD3D11) {
            ID3D11Device* const d3dDevice = deviceD3D11->GetD3D11Device();
            if (d3dDevice != nullptr &&
                SUCCEEDED(d3dDevice->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&mImpl->infoQueue)))) {
                // Keep every message for DrainDebugLayer, and never break into a debugger.
                mImpl->infoQueue->SetMessageCountLimit(static_cast<UINT64>(-1));
                mImpl->infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, FALSE);
                mImpl->infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, FALSE);
                mImpl->infoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, FALSE);
            }
        }

        dg::SwapChainDesc swapChainDesc;
        swapChainDesc.Width = width;
        swapChainDesc.Height = height;
        // UNORM, not the SRGB default: FA renders in gamma space (m6u-DIL.txt 9).
        swapChainDesc.ColorBufferFormat = dg::TEX_FORMAT_RGBA8_UNORM;
        // The engine owns its depth targets (gal CreateDepthStencilTarget, the head's D24S8).
        swapChainDesc.DepthBufferFormat = dg::TEX_FORMAT_UNKNOWN;
        swapChainDesc.Usage = dg::SWAP_CHAIN_USAGE_RENDER_TARGET | dg::SWAP_CHAIN_USAGE_COPY_SOURCE;
        swapChainDesc.BufferCount = 2;
        dg::FullScreenModeDesc fullScreenDesc;
        dg::Win32NativeWindow nativeWindow{window};
        factory->CreateSwapChainD3D11(mImpl->device, mImpl->context, swapChainDesc, fullScreenDesc, nativeWindow, &mImpl->swapChain);
        if (!mImpl->swapChain) {
            *error = "cannot create the swap chain";
            Destroy();
            return false;
        }

        if (!mImpl->CreatePresentPipeline(error)) {
            Destroy();
            return false;
        }

        mImpl->width = width;
        mImpl->height = height;
        if (!mImpl->CreateHead(error)) {
            Destroy();
            return false;
        }
        mImpl->gpu = std::make_shared<GpuShared>(mImpl->device, mImpl->context, static_cast<std::uint32_t>(::GetCurrentThreadId()));
        return true;
    }

    void DiligentHost::Destroy()
    {
        if (!mImpl) {
            return;
        }
        if (mImpl->context) {
            mImpl->context->Flush();
        }
        if (mImpl->infoQueue != nullptr) {
            DrainDebugLayer(nullptr, 0);
            mImpl->infoQueue->Release();
            mImpl->infoQueue = nullptr;
        }
        mImpl->gpu.reset();
        mImpl->readbacks.clear();
        mImpl->presentBinding.Release();
        mImpl->presentPipeline.Release();
        mImpl->head.Release();
        mImpl->swapChain.Release();
        mImpl->context.Release();
        mImpl->device.Release();
    }

    bool DiligentHost::IsCreated() const
    {
        return mImpl->device && mImpl->swapChain;
    }

    const std::shared_ptr<GpuShared>& DiligentHost::GetGpu() const
    {
        return mImpl->gpu;
    }

    dg::ITexture* DiligentHost::GetHeadTexture() const
    {
        return mImpl->head;
    }

    bool DiligentHost::Resize(const std::uint32_t width, const std::uint32_t height, std::string* const error)
    {
        if (!IsCreated()) {
            *error = "no device";
            return false;
        }
        if (width == mImpl->width && height == mImpl->height) {
            return true;
        }
        ScopedDefaultFpu fpu;
        std::lock_guard<std::recursive_mutex> lock(mImpl->gpu->Lock());
        mImpl->context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        mImpl->swapChain->Resize(width, height);
        mImpl->width = width;
        mImpl->height = height;
        return mImpl->CreateHead(error);
    }

    void DiligentHost::Present(const std::uint32_t syncInterval)
    {
        if (!IsCreated()) {
            return;
        }
        std::lock_guard<std::recursive_mutex> lock(mImpl->gpu->Lock());
        dg::ITextureView* backBuffer = mImpl->swapChain->GetCurrentBackBufferRTV();
        mImpl->context->SetRenderTargets(1, &backBuffer, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const dg::SwapChainDesc& desc = mImpl->swapChain->GetDesc();
        dg::Viewport viewport;
        viewport.Width = static_cast<float>(desc.Width);
        viewport.Height = static_cast<float>(desc.Height);
        mImpl->context->SetViewports(1, &viewport, desc.Width, desc.Height);
        mImpl->context->SetPipelineState(mImpl->presentPipeline);
        mImpl->context->CommitShaderResources(mImpl->presentBinding, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        dg::DrawAttribs draw;
        draw.NumVertices = 3;
        draw.Flags = dg::DRAW_FLAG_VERIFY_ALL;
        mImpl->context->Draw(draw);
        // Unbind before presenting; the device rebinds its own targets at the next clear or draw.
        mImpl->context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        mImpl->swapChain->Present(syncInterval);
        mImpl->gpu->AdvanceFrame();
    }

    // The staging-texture readback of Diligent's own screen capture (DiligentTools ScreenCapture:
    // CopyTexture with both transitions, then a map for reading). This waits for the GPU instead of
    // a fence: the frame harness reads a few chosen frames, and D3D9's GetRenderTargetData stalls
    // the same way.
    bool DiligentHost::ReadTextureBgra8(
        dg::ITexture* const texture,
        std::vector<std::uint8_t>* const out,
        std::uint32_t* const width,
        std::uint32_t* const height
    )
    {
        if (!IsCreated() || texture == nullptr) {
            return false;
        }
        std::lock_guard<std::recursive_mutex> lock(mImpl->gpu->Lock());
        const dg::TextureDesc& sourceDesc = texture->GetDesc();
        bool swapRedBlue = false;
        switch (sourceDesc.Format) {
        case dg::TEX_FORMAT_RGBA8_UNORM:
        case dg::TEX_FORMAT_RGBA8_UNORM_SRGB:
            swapRedBlue = true;
            break;
        case dg::TEX_FORMAT_BGRA8_UNORM:
        case dg::TEX_FORMAT_BGRA8_UNORM_SRGB:
            break;
        default:
            return false;
        }

        dg::ITexture* staging = nullptr;
        for (const auto& candidate : mImpl->readbacks) {
            const dg::TextureDesc& desc = candidate->GetDesc();
            if (desc.Width == sourceDesc.Width && desc.Height == sourceDesc.Height && desc.Format == sourceDesc.Format) {
                staging = candidate;
                break;
            }
        }
        if (staging == nullptr) {
            dg::TextureDesc desc;
            desc.Name = "gal readback";
            desc.Type = dg::RESOURCE_DIM_TEX_2D;
            desc.Width = sourceDesc.Width;
            desc.Height = sourceDesc.Height;
            desc.Format = sourceDesc.Format;
            desc.MipLevels = 1;
            desc.Usage = dg::USAGE_STAGING;
            desc.CPUAccessFlags = dg::CPU_ACCESS_READ;
            dg::RefCntAutoPtr<dg::ITexture> created;
            mImpl->device->CreateTexture(desc, nullptr, &created);
            if (!created) {
                return false;
            }
            staging = created;
            mImpl->readbacks.push_back(std::move(created));
        }

        // The source may still be bound as the render target; a copy source must not be. The device
        // rebinds its targets at the next clear or draw (DrawPath::InvalidateTargets).
        mImpl->context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        dg::CopyTextureAttribs copy{texture, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging,
                                    dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
        mImpl->context->CopyTexture(copy);
        mImpl->context->WaitForIdle();

        dg::MappedTextureSubresource mapped;
        mImpl->context->MapTextureSubresource(staging, 0, 0, dg::MAP_READ, dg::MAP_FLAG_NONE, nullptr, mapped);
        if (mapped.pData == nullptr) {
            return false;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(sourceDesc.Width) * 4u;
        out->resize(rowBytes * sourceDesc.Height);
        for (std::uint32_t row = 0; row < sourceDesc.Height; ++row) {
            const std::uint8_t* const from = static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(mapped.Stride) * row;
            std::uint8_t* const to = out->data() + rowBytes * row;
            if (!swapRedBlue) {
                std::memcpy(to, from, rowBytes);
                continue;
            }
            for (std::uint32_t x = 0; x < sourceDesc.Width; ++x) {
                to[x * 4u + 0u] = from[x * 4u + 2u]; // B
                to[x * 4u + 1u] = from[x * 4u + 1u]; // G
                to[x * 4u + 2u] = from[x * 4u + 0u]; // R
                to[x * 4u + 3u] = from[x * 4u + 3u]; // A
            }
        }
        mImpl->context->UnmapTextureSubresource(staging, 0, 0);
        *width = sourceDesc.Width;
        *height = sourceDesc.Height;
        return true;
    }

    bool DiligentHost::ReadBuffer(dg::IBuffer* const buffer, const std::uint32_t size, std::vector<std::uint8_t>* const out)
    {
        if (!IsCreated() || buffer == nullptr || size == 0U) {
            return false;
        }
        std::lock_guard<std::recursive_mutex> lock(mImpl->gpu->Lock());
        dg::BufferDesc desc;
        desc.Name = "gal buffer readback";
        desc.Size = size;
        desc.Usage = dg::USAGE_STAGING;
        desc.CPUAccessFlags = dg::CPU_ACCESS_READ;
        dg::RefCntAutoPtr<dg::IBuffer> staging;
        mImpl->device->CreateBuffer(desc, nullptr, &staging);
        if (!staging) {
            return false;
        }
        mImpl->context->CopyBuffer(buffer, 0, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, size,
                                   dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        mImpl->context->WaitForIdle();
        void* mapped = nullptr;
        mImpl->context->MapBuffer(staging, dg::MAP_READ, dg::MAP_FLAG_NONE, mapped);
        if (mapped == nullptr) {
            return false;
        }
        out->assign(static_cast<const std::uint8_t*>(mapped), static_cast<const std::uint8_t*>(mapped) + size);
        mImpl->context->UnmapBuffer(staging, dg::MAP_READ);
        return true;
    }

    void DiligentHost::DrainDebugLayer(std::vector<std::string>* const out, const std::size_t keep)
    {
        ID3D11InfoQueue* const queue = mImpl->infoQueue;
        if (queue == nullptr) {
            return;
        }
        const UINT64 count = queue->GetNumStoredMessages();
        std::size_t kept = 0;
        for (UINT64 index = 0; index < count; ++index) {
            SIZE_T length = 0;
            if (FAILED(queue->GetMessage(index, nullptr, &length)) || length == 0) {
                continue;
            }
            std::vector<unsigned char> storage(length);
            D3D11_MESSAGE* const message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (FAILED(queue->GetMessage(index, message, &length))) {
                continue;
            }
            const char* tag = "message";
            switch (message->Severity) {
            case D3D11_MESSAGE_SEVERITY_CORRUPTION:
                ++mImpl->debugCounts.corruption;
                tag = "CORRUPTION";
                break;
            case D3D11_MESSAGE_SEVERITY_ERROR:
                ++mImpl->debugCounts.error;
                tag = "ERROR";
                break;
            case D3D11_MESSAGE_SEVERITY_WARNING:
                ++mImpl->debugCounts.warning;
                tag = "WARNING";
                break;
            case D3D11_MESSAGE_SEVERITY_INFO:
                ++mImpl->debugCounts.info;
                tag = "INFO";
                break;
            default:
                ++mImpl->debugCounts.message;
                break;
            }
            if (out != nullptr && kept < keep && message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
                std::string text(tag);
                text += " #" + std::to_string(static_cast<int>(message->ID)) + ": ";
                text.append(message->pDescription, message->DescriptionByteLength > 0 ? message->DescriptionByteLength - 1 : 0);
                out->push_back(std::move(text));
                ++kept;
            }
        }
        queue->ClearStoredMessages();
    }

    std::uint32_t DiligentHost::SelfTestDebugLayer(std::string* const sample)
    {
        ID3D11InfoQueue* const queue = mImpl->infoQueue;
        if (queue == nullptr || !mImpl->device) {
            return 0;
        }
        DrainDebugLayer(nullptr, 0);
        dg::RefCntAutoPtr<dg::IRenderDeviceD3D11> deviceD3D11(mImpl->device, dg::IID_RenderDeviceD3D11);
        if (!deviceD3D11) {
            return 0;
        }
        // A zero-sized buffer: the runtime rejects it (E_INVALIDARG) after the debug layer reports
        // it, so nothing reaches the driver. (An invalid draw would: on this machine one removed
        // the device, DXGI_ERROR_DRIVER_INTERNAL_ERROR, M6a step 1.)
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        ID3D11Buffer* buffer = nullptr;
        const HRESULT result = deviceD3D11->GetD3D11Device()->CreateBuffer(&desc, nullptr, &buffer);
        if (buffer != nullptr) {
            buffer->Release();
        }
        std::uint32_t errors = 0;
        const UINT64 count = queue->GetNumStoredMessages();
        for (UINT64 index = 0; index < count; ++index) {
            SIZE_T length = 0;
            if (FAILED(queue->GetMessage(index, nullptr, &length)) || length == 0) {
                continue;
            }
            std::vector<unsigned char> storage(length);
            D3D11_MESSAGE* const message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (SUCCEEDED(queue->GetMessage(index, message, &length)) &&
                message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) {
                if (errors == 0 && sample != nullptr) {
                    sample->assign(message->pDescription, message->DescriptionByteLength > 0 ? message->DescriptionByteLength - 1 : 0);
                }
                ++errors;
            }
        }
        queue->ClearStoredMessages();
        if (SUCCEEDED(result) && sample != nullptr) {
            *sample += " (CreateBuffer unexpectedly succeeded)";
        }
        return errors;
    }

    bool DiligentHost::IsDebugLayerActive() const
    {
        return mImpl->infoQueue != nullptr;
    }

    DebugLayerCounts DiligentHost::GetDebugLayerCounts() const
    {
        return mImpl->debugCounts;
    }

    DiligentMessageCounts DiligentHost::GetDiligentMessageCounts()
    {
        std::lock_guard<std::mutex> lock(gMessageLock);
        return gMessageCounts;
    }

    std::vector<std::string> DiligentHost::GetDiligentMessages()
    {
        std::lock_guard<std::mutex> lock(gMessageLock);
        return gMessages;
    }

    std::string DiligentHost::GetAdapterDescription() const
    {
        if (!mImpl->device) {
            return {};
        }
        return mImpl->device->GetAdapterInfo().Description;
    }

    std::string DiligentHost::GetSwapChainFormat() const
    {
        if (!mImpl->swapChain) {
            return {};
        }
        // GraphicsAccessories.hpp would name every format, but it includes the Archiver interface,
        // which a DILIGENT_NO_ARCHIVER install does not ship.
        switch (mImpl->swapChain->GetDesc().ColorBufferFormat) {
        case dg::TEX_FORMAT_RGBA8_UNORM:
            return "RGBA8_UNORM";
        case dg::TEX_FORMAT_RGBA8_UNORM_SRGB:
            return "RGBA8_UNORM_SRGB";
        case dg::TEX_FORMAT_BGRA8_UNORM:
            return "BGRA8_UNORM";
        case dg::TEX_FORMAT_BGRA8_UNORM_SRGB:
            return "BGRA8_UNORM_SRGB";
        default:
            return "format " + std::to_string(static_cast<int>(mImpl->swapChain->GetDesc().ColorBufferFormat));
        }
    }

    std::uint32_t DiligentHost::GetHeadWidth() const
    {
        return mImpl->width;
    }

    std::uint32_t DiligentHost::GetHeadHeight() const
    {
        return mImpl->height;
    }

    // ---------------------------------------------------------------------------------------------
    // Formats

    namespace
    {
        constexpr std::uint32_t FourCC(const char a, const char b, const char c, const char d)
        {
            return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) |
                   (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
        }
        // D3DFORMAT values (d3d9types.h).
        constexpr std::uint32_t kD3DFmtR8G8B8 = 20, kD3DFmtA8R8G8B8 = 21, kD3DFmtX8R8G8B8 = 22, kD3DFmtR5G6B5 = 23,
                                kD3DFmtX1R5G5B5 = 24, kD3DFmtA1R5G5B5 = 25, kD3DFmtA4R4G4B4 = 26, kD3DFmtA8 = 28,
                                kD3DFmtA2B10G10R10 = 31, kD3DFmtA8B8G8R8 = 32, kD3DFmtG16R16 = 34, kD3DFmtA2R10G10B10 = 35,
                                kD3DFmtA16B16G16R16 = 36, kD3DFmtL8 = 50, kD3DFmtA8L8 = 51, kD3DFmtR16F = 111, kD3DFmtG16R16F = 112,
                                kD3DFmtA16B16G16R16F = 113, kD3DFmtR32F = 114, kD3DFmtG32R32F = 115, kD3DFmtA32B32G32R32F = 116;
        constexpr std::uint32_t kD3DFmtDxt1 = FourCC('D', 'X', 'T', '1'), kD3DFmtDxt2 = FourCC('D', 'X', 'T', '2'),
                                kD3DFmtDxt3 = FourCC('D', 'X', 'T', '3'), kD3DFmtDxt4 = FourCC('D', 'X', 'T', '4'),
                                kD3DFmtDxt5 = FourCC('D', 'X', 'T', '5');
    } // namespace

    std::uint32_t TextureFormatForD3D9(const std::uint32_t d3dFormat, TexelConversion* const conversion)
    {
        *conversion = TexelConversion::None;
        switch (d3dFormat) {
        case kD3DFmtA8R8G8B8:
            return dg::TEX_FORMAT_BGRA8_UNORM; // same bytes: B, G, R, A
        case kD3DFmtX8R8G8B8:
            return dg::TEX_FORMAT_BGRX8_UNORM; // samples alpha 1, as D3D9
        case kD3DFmtA8B8G8R8:
            return dg::TEX_FORMAT_RGBA8_UNORM;
        case kD3DFmtR5G6B5:
            return dg::TEX_FORMAT_B5G6R5_UNORM;
        case kD3DFmtA1R5G5B5:
            return dg::TEX_FORMAT_B5G5R5A1_UNORM; // same bit layout
        case kD3DFmtX1R5G5B5:
            *conversion = TexelConversion::X1R5G5B5ToBgra8;
            return dg::TEX_FORMAT_BGRA8_UNORM;
        case kD3DFmtA4R4G4B4:
            *conversion = TexelConversion::A4R4G4B4ToBgra8;
            return dg::TEX_FORMAT_BGRA8_UNORM;
        case kD3DFmtR8G8B8:
            *conversion = TexelConversion::R8G8B8ToBgra8;
            return dg::TEX_FORMAT_BGRA8_UNORM;
        case kD3DFmtA8:
            return dg::TEX_FORMAT_A8_UNORM; // samples (0, 0, 0, A) on both
        case kD3DFmtL8:
            *conversion = TexelConversion::L8ToBgra8;
            return dg::TEX_FORMAT_BGRA8_UNORM;
        case kD3DFmtA8L8:
            *conversion = TexelConversion::A8L8ToBgra8;
            return dg::TEX_FORMAT_BGRA8_UNORM;
        case kD3DFmtA2B10G10R10:
            return dg::TEX_FORMAT_RGB10A2_UNORM;
        case kD3DFmtA2R10G10B10:
            *conversion = TexelConversion::A2R10G10B10ToRgb10A2;
            return dg::TEX_FORMAT_RGB10A2_UNORM;
        case kD3DFmtG16R16:
            return dg::TEX_FORMAT_RG16_UNORM;
        case kD3DFmtA16B16G16R16:
            return dg::TEX_FORMAT_RGBA16_UNORM;
        case kD3DFmtDxt1:
            return dg::TEX_FORMAT_BC1_UNORM;
        case kD3DFmtDxt2:
        case kD3DFmtDxt3:
            return dg::TEX_FORMAT_BC2_UNORM; // DXT2 is DXT3 with premultiplied colour: the same blocks
        case kD3DFmtDxt4:
        case kD3DFmtDxt5:
            return dg::TEX_FORMAT_BC3_UNORM;
        case kD3DFmtR16F:
            return dg::TEX_FORMAT_R16_FLOAT;
        case kD3DFmtG16R16F:
            return dg::TEX_FORMAT_RG16_FLOAT;
        case kD3DFmtA16B16G16R16F:
            return dg::TEX_FORMAT_RGBA16_FLOAT;
        case kD3DFmtR32F:
            return dg::TEX_FORMAT_R32_FLOAT;
        case kD3DFmtG32R32F:
            return dg::TEX_FORMAT_RG32_FLOAT;
        case kD3DFmtA32B32G32R32F:
            return dg::TEX_FORMAT_RGBA32_FLOAT;
        default:
            return 0U;
        }
    }

    std::uint32_t D3D9FormatBytes(const std::uint32_t d3dFormat, bool* const blockCompressed)
    {
        *blockCompressed = false;
        switch (d3dFormat) {
        case kD3DFmtDxt1:
            *blockCompressed = true;
            return 8U;
        case kD3DFmtDxt2:
        case kD3DFmtDxt3:
        case kD3DFmtDxt4:
        case kD3DFmtDxt5:
            *blockCompressed = true;
            return 16U;
        case kD3DFmtA8:
        case kD3DFmtL8:
            return 1U;
        case kD3DFmtR5G6B5:
        case kD3DFmtX1R5G5B5:
        case kD3DFmtA1R5G5B5:
        case kD3DFmtA4R4G4B4:
        case kD3DFmtA8L8:
        case kD3DFmtR16F:
            return 2U;
        case kD3DFmtR8G8B8:
            return 3U;
        case kD3DFmtA16B16G16R16:
        case kD3DFmtA16B16G16R16F:
        case kD3DFmtG32R32F:
            return 8U;
        case kD3DFmtA32B32G32R32F:
            return 16U;
        default:
            return 4U;
        }
    }

    std::uint32_t RenderTargetFormatForToken(const std::uint32_t token)
    {
        switch (token) {
        case 1U:
            return dg::TEX_FORMAT_RGB10A2_UNORM;
        case 4U:
        case 5U:
            return dg::TEX_FORMAT_B5G5R5A1_UNORM;
        case 6U:
            return dg::TEX_FORMAT_B5G6R5_UNORM;
        case 7U:
            return dg::TEX_FORMAT_RG16_UNORM;
        default:
            return dg::TEX_FORMAT_RGBA8_UNORM; // 2 A8R8G8B8, 3 X8R8G8B8
        }
    }

    std::uint32_t DepthStencilFormatForToken(const std::uint32_t token)
    {
        switch (token) {
        case 1U:
            return dg::TEX_FORMAT_D32_FLOAT;
        case 6U:
            return dg::TEX_FORMAT_D16_UNORM;
        default:
            return dg::TEX_FORMAT_D24_UNORM_S8_UINT; // 2 D15S1, 3 D24S8, 4 D24X8, 5 D24X4S4
        }
    }

    // ---------------------------------------------------------------------------------------------
    // GpuTexture

    struct GpuTexture::Impl
    {
        dg::RefCntAutoPtr<dg::ITexture> texture;
        std::vector<dg::RefCntAutoPtr<dg::ITextureView>> faceViews; // cube render-target faces
    };

    GpuTexture::GpuTexture()
        : impl_(std::make_unique<Impl>())
    {}

    GpuTexture::~GpuTexture() = default;

    std::unique_ptr<GpuTexture> GpuTexture::Create(
        GpuShared& gpu,
        const GpuTextureDesc& desc,
        const GpuSubresource* const initial,
        const std::uint32_t initialCount,
        std::string* const error
    )
    {
        std::lock_guard<std::recursive_mutex> lock(gpu.Lock());
        dg::TextureDesc textureDesc;
        textureDesc.Name = desc.name;
        switch (desc.kind) {
        case GpuTextureDesc::Kind::TextureCube:
            textureDesc.Type = dg::RESOURCE_DIM_TEX_CUBE;
            textureDesc.ArraySize = 6;
            break;
        case GpuTextureDesc::Kind::Texture3D:
            textureDesc.Type = dg::RESOURCE_DIM_TEX_3D;
            textureDesc.Depth = desc.depth;
            break;
        default:
            textureDesc.Type = dg::RESOURCE_DIM_TEX_2D;
            break;
        }
        textureDesc.Width = desc.width;
        textureDesc.Height = desc.height;
        textureDesc.Format = static_cast<dg::TEXTURE_FORMAT>(desc.format);
        textureDesc.MipLevels = desc.mipLevels;
        textureDesc.Usage = dg::USAGE_DEFAULT;
        textureDesc.BindFlags = dg::BIND_NONE;
        if (desc.shaderResource) {
            textureDesc.BindFlags |= dg::BIND_SHADER_RESOURCE;
        }
        if (desc.renderTarget || desc.generateMips) {
            textureDesc.BindFlags |= dg::BIND_RENDER_TARGET;
        }
        if (desc.depthStencil) {
            textureDesc.BindFlags |= dg::BIND_DEPTH_STENCIL;
            textureDesc.ClearValue.Format = textureDesc.Format;
            textureDesc.ClearValue.DepthStencil.Depth = 1.0F;
        } else if (desc.renderTarget) {
            textureDesc.ClearValue.Format = textureDesc.Format;
        }
        if (desc.generateMips) {
            textureDesc.MiscFlags = dg::MISC_TEXTURE_FLAG_GENERATE_MIPS;
        }

        std::vector<dg::TextureSubResData> subresources;
        dg::TextureData data;
        if (initial != nullptr && initialCount != 0U) {
            subresources.resize(initialCount);
            for (std::uint32_t index = 0; index < initialCount; ++index) {
                subresources[index].pData = initial[index].data;
                subresources[index].Stride = initial[index].stride;
                subresources[index].DepthStride = initial[index].depthStride;
            }
            data.pSubResources = subresources.data();
            data.NumSubresources = initialCount;
            data.pContext = gpu.Context();
        }
        if (!gpu.OnRenderThread()) {
            ++gpu.Stats().createdOffRenderThread;
        }
        ScopedDefaultFpu fpu;
        auto result = std::unique_ptr<GpuTexture>(new GpuTexture());
        gpu.Device()->CreateTexture(textureDesc, data.pSubResources != nullptr ? &data : nullptr, &result->impl_->texture);
        if (!result->impl_->texture) {
            if (error != nullptr) {
                *error = "CreateTexture failed for " + std::string(desc.name) + " (" + std::to_string(desc.width) + "x" +
                         std::to_string(desc.height) + ", format " + std::to_string(desc.format) + ")";
            }
            return nullptr;
        }
        ++gpu.Stats().texturesCreated;
        return result;
    }

    std::unique_ptr<GpuTexture> GpuTexture::Wrap(dg::ITexture* const texture)
    {
        auto result = std::unique_ptr<GpuTexture>(new GpuTexture());
        result->impl_->texture = texture;
        return result;
    }

    void GpuTexture::Update(
        GpuShared& gpu,
        const std::uint32_t level,
        const std::uint32_t face,
        const std::uint32_t x,
        const std::uint32_t y,
        const std::uint32_t width,
        const std::uint32_t height,
        const GpuSubresource& data
    )
    {
        if (!impl_->texture || width == 0U || height == 0U) {
            return;
        }
        std::lock_guard<std::recursive_mutex> lock(gpu.Lock());
        const dg::TextureDesc& desc = impl_->texture->GetDesc();
        const std::uint32_t depth = desc.Type == dg::RESOURCE_DIM_TEX_3D ? std::max<std::uint32_t>(desc.Depth >> level, 1U) : 1U;
        const dg::Box box(x, x + width, y, y + height, 0, depth);
        dg::TextureSubResData subresource;
        subresource.pData = data.data;
        subresource.Stride = data.stride;
        subresource.DepthStride = data.depthStride;
        gpu.Context()->UpdateTexture(impl_->texture, level, face, box, subresource, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                     dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ++gpu.Stats().updateTexture;
        // Rows of `stride` bytes: texel rows, or block rows for block-compressed formats.
        const std::uint32_t blockHeight = std::max<std::uint32_t>(gpu.Device()->GetTextureFormatInfo(desc.Format).BlockHeight, 1U);
        gpu.Stats().updateTextureBytes += static_cast<std::uint64_t>(data.stride) * ((height + blockHeight - 1U) / blockHeight);
    }

    void GpuTexture::GenerateMips(GpuShared& gpu)
    {
        if (!impl_->texture) {
            return;
        }
        std::lock_guard<std::recursive_mutex> lock(gpu.Lock());
        if (dg::ITextureView* const view = impl_->texture->GetDefaultView(dg::TEXTURE_VIEW_SHADER_RESOURCE)) {
            gpu.Context()->GenerateMips(view);
        }
    }

    dg::ITexture* GpuTexture::Texture() const
    {
        return impl_->texture;
    }

    dg::ITextureView* GpuTexture::ShaderResourceView() const
    {
        return impl_->texture ? impl_->texture->GetDefaultView(dg::TEXTURE_VIEW_SHADER_RESOURCE) : nullptr;
    }

    dg::ITextureView* GpuTexture::RenderTargetView(const std::uint32_t face)
    {
        if (!impl_->texture) {
            return nullptr;
        }
        const dg::TextureDesc& desc = impl_->texture->GetDesc();
        if (desc.Type != dg::RESOURCE_DIM_TEX_CUBE) {
            return impl_->texture->GetDefaultView(dg::TEXTURE_VIEW_RENDER_TARGET);
        }
        // One 2D-array view per cube face (D3DCUBEMAP_FACES order +X, -X, +Y, -Y, +Z, -Z = D3D11's slices).
        if (impl_->faceViews.empty()) {
            impl_->faceViews.resize(6);
        }
        if (face >= 6U) {
            return nullptr;
        }
        if (!impl_->faceViews[face]) {
            dg::TextureViewDesc view;
            view.Name = "gal cube face";
            view.ViewType = dg::TEXTURE_VIEW_RENDER_TARGET;
            view.TextureDim = dg::RESOURCE_DIM_TEX_2D_ARRAY;
            view.MostDetailedMip = 0;
            view.NumMipLevels = 1;
            view.FirstArraySlice = face;
            view.NumArraySlices = 1;
            impl_->texture->CreateView(view, &impl_->faceViews[face]);
        }
        return impl_->faceViews[face];
    }

    dg::ITextureView* GpuTexture::DepthStencilView() const
    {
        return impl_->texture ? impl_->texture->GetDefaultView(dg::TEXTURE_VIEW_DEPTH_STENCIL) : nullptr;
    }

    std::uint32_t GpuTexture::Width() const
    {
        return impl_->texture ? impl_->texture->GetDesc().Width : 0U;
    }

    std::uint32_t GpuTexture::Height() const
    {
        return impl_->texture ? impl_->texture->GetDesc().Height : 0U;
    }

    // ---------------------------------------------------------------------------------------------
    // GpuBuffer

    struct GpuBuffer::Impl
    {
        dg::RefCntAutoPtr<dg::IBuffer> buffer;
        bool dynamic = false;
    };

    GpuBuffer::GpuBuffer()
        : impl_(std::make_unique<Impl>())
    {}

    GpuBuffer::~GpuBuffer() = default;

    std::unique_ptr<GpuBuffer> GpuBuffer::Create(
        GpuShared& gpu,
        const std::uint32_t size,
        const bool indexBuffer,
        const bool dynamic,
        const void* const initial,
        std::string* const error
    )
    {
        std::lock_guard<std::recursive_mutex> lock(gpu.Lock());
        dg::BufferDesc desc;
        desc.Name = indexBuffer ? (dynamic ? "gal dynamic index buffer" : "gal index buffer")
                                : (dynamic ? "gal dynamic vertex buffer" : "gal vertex buffer");
        desc.Size = size;
        desc.BindFlags = indexBuffer ? dg::BIND_INDEX_BUFFER : dg::BIND_VERTEX_BUFFER;
        // D3D9's dynamic buffers (D3DUSAGE_DYNAMIC in the default pool, D3D9Interfaces.cpp:2811-2812,
        // 2843-2845) are written through Map(DISCARD/NO_OVERWRITE) only; Vulkan and D3D12 keep such
        // memory in a per-frame ring (DeviceContextVkImpl.cpp:2306-2329) and UpdateBuffer would end the
        // render pass (VulkanUtilities/CommandBuffer.hpp:526-547, m6u-CRIT.txt R2).
        desc.Usage = dynamic ? dg::USAGE_DYNAMIC : dg::USAGE_DEFAULT;
        desc.CPUAccessFlags = dynamic ? dg::CPU_ACCESS_WRITE : dg::CPU_ACCESS_NONE;
        dg::BufferData data;
        if (!dynamic && initial != nullptr) {
            data.pData = initial;
            data.DataSize = size;
            data.pContext = gpu.Context();
        }
        if (!gpu.OnRenderThread()) {
            ++gpu.Stats().createdOffRenderThread;
        }
        auto result = std::unique_ptr<GpuBuffer>(new GpuBuffer());
        result->impl_->dynamic = dynamic;
        gpu.Device()->CreateBuffer(desc, data.pData != nullptr ? &data : nullptr, &result->impl_->buffer);
        if (!result->impl_->buffer) {
            if (error != nullptr) {
                *error = std::string("CreateBuffer failed for ") + desc.Name + " of " + std::to_string(size) + " bytes";
            }
            return nullptr;
        }
        ++gpu.Stats().buffersCreated;
        return result;
    }

    void GpuBuffer::WriteDiscard(GpuShared& gpu, const std::uint32_t offset, const void* const data, const std::uint32_t size)
    {
        std::lock_guard<std::recursive_mutex> lock(gpu.Lock());
        void* mapped = nullptr;
        gpu.Context()->MapBuffer(impl_->buffer, dg::MAP_WRITE, dg::MAP_FLAG_DISCARD, mapped);
        if (mapped != nullptr) {
            std::memcpy(static_cast<std::uint8_t*>(mapped) + offset, data, size);
            gpu.Context()->UnmapBuffer(impl_->buffer, dg::MAP_WRITE);
        }
        gpu.Stats().mapBytes += size;
    }

    void GpuBuffer::WriteNoOverwrite(GpuShared& gpu, const std::uint32_t offset, const void* const data, const std::uint32_t size)
    {
        std::lock_guard<std::recursive_mutex> lock(gpu.Lock());
        void* mapped = nullptr;
        gpu.Context()->MapBuffer(impl_->buffer, dg::MAP_WRITE, dg::MAP_FLAG_NO_OVERWRITE, mapped);
        if (mapped != nullptr) {
            std::memcpy(static_cast<std::uint8_t*>(mapped) + offset, data, size);
            gpu.Context()->UnmapBuffer(impl_->buffer, dg::MAP_WRITE);
        }
        gpu.Stats().mapBytes += size;
    }

    void GpuBuffer::Update(GpuShared& gpu, const std::uint32_t offset, const void* const data, const std::uint32_t size)
    {
        std::lock_guard<std::recursive_mutex> lock(gpu.Lock());
        gpu.Context()->UpdateBuffer(impl_->buffer, offset, size, data, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ++gpu.Stats().updateBuffer;
        if (gpu.InFrame()) {
            ++gpu.Stats().updateBufferInFrame;
        }
    }

    dg::IBuffer* GpuBuffer::Buffer() const
    {
        return impl_->buffer;
    }
} // namespace gpg::gal::diligent
