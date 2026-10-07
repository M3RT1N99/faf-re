#include "DiligentHost.h"

#include <d3d11.h>
#include <d3d11sdklayers.h>

#include <float.h>

#include <algorithm>
#include <cstring>
#include <mutex>

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/SwapChain.h"
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

        /**
         * The engine runs the main thread under 24-bit x87 precision (CScApp::Init; the headless
         * runner does the same, m3a-result.txt). The shader compiler and the driver are not written
         * for that, so device and shader creation run at the CRT default and the engine's control
         * word is put back afterwards (m6u-PLAN.txt B, "save and restore the x87 control word").
         */
        class ScopedDefaultFpu
        {
        public:
            ScopedDefaultFpu()
            {
                unsigned int unused = 0;
                ::_controlfp_s(&mSaved, 0, 0);
                ::_controlfp_s(&unused, _PC_53, _MCW_PC);
            }
            ~ScopedDefaultFpu()
            {
                unsigned int unused = 0;
                ::_controlfp_s(&unused, mSaved & _MCW_PC, _MCW_PC);
            }
            ScopedDefaultFpu(const ScopedDefaultFpu&) = delete;
            ScopedDefaultFpu& operator=(const ScopedDefaultFpu&) = delete;

        private:
            unsigned int mSaved = 0;
        };

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

    struct DiligentHost::Impl
    {
        dg::RefCntAutoPtr<dg::IRenderDevice> device;
        dg::RefCntAutoPtr<dg::IDeviceContext> context;
        dg::RefCntAutoPtr<dg::ISwapChain> swapChain;
        dg::RefCntAutoPtr<dg::ITexture> head;
        dg::RefCntAutoPtr<dg::ITexture> headReadback; // staging copy of the head, made on first ReadHead
        dg::RefCntAutoPtr<dg::IPipelineState> presentPipeline;
        dg::RefCntAutoPtr<dg::IShaderResourceBinding> presentBinding;
        ID3D11InfoQueue* infoQueue = nullptr;
        DebugLayerCounts debugCounts;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool CreateHead(std::string* error)
        {
            presentBinding.Release();
            headReadback.Release();
            head.Release();

            dg::TextureDesc desc;
            desc.Name = "gal head 0";
            desc.Type = dg::RESOURCE_DIM_TEX_2D;
            desc.Width = width;
            desc.Height = height;
            desc.Format = dg::TEX_FORMAT_RGBA8_UNORM;
            desc.MipLevels = 1;
            desc.Usage = dg::USAGE_DEFAULT;
            desc.BindFlags = dg::BIND_RENDER_TARGET | dg::BIND_SHADER_RESOURCE;
            // Black, as D3D9's CreateHeads leaves a fresh back buffer (Clear(true, ..., 0)).
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
        mImpl->presentBinding.Release();
        mImpl->presentPipeline.Release();
        mImpl->headReadback.Release();
        mImpl->head.Release();
        mImpl->swapChain.Release();
        mImpl->context.Release();
        mImpl->device.Release();
    }

    bool DiligentHost::IsCreated() const
    {
        return mImpl->device && mImpl->swapChain;
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
        mImpl->context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        mImpl->swapChain->Resize(width, height);
        mImpl->width = width;
        mImpl->height = height;
        return mImpl->CreateHead(error);
    }

    void DiligentHost::ClearHead(const float rgba[4])
    {
        if (!IsCreated()) {
            return;
        }
        dg::ITextureView* target = mImpl->head->GetDefaultView(dg::TEXTURE_VIEW_RENDER_TARGET);
        mImpl->context->SetRenderTargets(1, &target, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        mImpl->context->ClearRenderTarget(target, rgba, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void DiligentHost::Present(const std::uint32_t syncInterval)
    {
        if (!IsCreated()) {
            return;
        }
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
        // Unbind before presenting so the next frame's clear rebinds the head explicitly.
        mImpl->context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        mImpl->swapChain->Present(syncInterval);
    }

    // The staging-texture readback of Diligent's own screen capture (DiligentTools ScreenCapture:
    // CopyTexture with both transitions, then a map for reading). This waits for the GPU instead of
    // a fence: the frame harness reads a few chosen frames, and D3D9's GetRenderTargetData stalls
    // the same way.
    bool DiligentHost::ReadHead(std::vector<std::uint8_t>* const out, std::uint32_t* const width, std::uint32_t* const height)
    {
        if (!IsCreated() || !mImpl->head) {
            return false;
        }
        if (!mImpl->headReadback) {
            dg::TextureDesc desc;
            desc.Name = "gal head 0 readback";
            desc.Type = dg::RESOURCE_DIM_TEX_2D;
            desc.Width = mImpl->width;
            desc.Height = mImpl->height;
            desc.Format = dg::TEX_FORMAT_RGBA8_UNORM;
            desc.MipLevels = 1;
            desc.Usage = dg::USAGE_STAGING;
            desc.CPUAccessFlags = dg::CPU_ACCESS_READ;
            mImpl->device->CreateTexture(desc, nullptr, &mImpl->headReadback);
            if (!mImpl->headReadback) {
                return false;
            }
        }

        // The head may still be bound as the render target (Clear binds it); a copy source must not be.
        mImpl->context->SetRenderTargets(0, nullptr, nullptr, dg::RESOURCE_STATE_TRANSITION_MODE_NONE);
        const dg::CopyTextureAttribs copy{
            mImpl->head, dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, mImpl->headReadback,
            dg::RESOURCE_STATE_TRANSITION_MODE_TRANSITION};
        mImpl->context->CopyTexture(copy);
        mImpl->context->WaitForIdle();

        dg::MappedTextureSubresource mapped;
        mImpl->context->MapTextureSubresource(mImpl->headReadback, 0, 0, dg::MAP_READ, dg::MAP_FLAG_NONE, nullptr, mapped);
        if (mapped.pData == nullptr) {
            return false;
        }
        const std::size_t rowBytes = static_cast<std::size_t>(mImpl->width) * 4u;
        out->resize(rowBytes * mImpl->height);
        for (std::uint32_t row = 0; row < mImpl->height; ++row) {
            std::memcpy(
                out->data() + rowBytes * row,
                static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(mapped.Stride) * row,
                rowBytes
            );
        }
        mImpl->context->UnmapTextureSubresource(mImpl->headReadback, 0, 0);
        *width = mImpl->width;
        *height = mImpl->height;
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
} // namespace gpg::gal::diligent
