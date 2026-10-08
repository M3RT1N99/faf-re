#include "DeviceDiligent.h"

#include <d3d9.h>
#include <d3dx9.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>

#include "D3D9Oracle.h"
#include "DeviceFactory.h"
#include "DiligentHost.h"
#include "EffectsDiligent.h"
#include "GalDiligent.h"
#include "ResourcesDiligent.h"
#include "ShaderCompileDiligent.h"
#include "Texture2DPortable.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/CubeRenderTargetContext.hpp"
#include "gpg/gal/CursorContext.hpp"
#include "gpg/gal/DepthStencilTargetContext.hpp"
#include "gpg/gal/DrawContext.hpp"
#include "gpg/gal/DrawStatistics.h"
#include "gpg/gal/DrawIndexedContext.hpp"
#include "gpg/gal/EffectContext.hpp"
#include "gpg/gal/Error.hpp"
#include "gpg/gal/IndexBufferContext.hpp"
#include "gpg/gal/RenderTargetContext.hpp"
#include "gpg/gal/TextureContext.hpp"
#include "gpg/gal/VertexBufferContext.hpp"
#include "moho/app/WinApp.h"
#include "moho/misc/StartupHelpers.h"

namespace gpg::gal::diligent
{
    namespace
    {
        // Device.hpp's slot names, in vtable order (0x00D42224).
        constexpr const char* kSlotNames[DeviceDiligent::kSlotCount] = {
            "~Device", "GetLog", "GetDeviceContext", "GetCurThreadId", "Func1", "GetModesForAdapter",
            "GetHeadOutputContext", "GetHeadOutputContext const", "GetPipelineState", "CreateEffect",
            "CreateTexture", "CreateRenderTarget", "CreateCubeRenderTarget", "CreateDepthStencilTarget",
            "CreateVertexFormat", "CreateVertexBuffer", "CreateIndexBuffer", "GetRenderTargetData", "StretchRect",
            "UpdateSurface", "SaveCubeRenderTarget", "SaveRenderTarget", "SaveTexture", "GetTexture2D", "Func7",
            "Reset(context)", "Reset()", "TestCooperativeLevel", "BeginScene", "EndScene", "Present", "SetCursor",
            "InitCursor", "ShowCursor", "SetViewport", "GetViewport", "ClearTarget", "GetContext", "Clear",
            "ClearTextures", "SetVertexDeclaration", "SetVertexBuffer", "SetBufferIndices", "SetFogState",
            "SetWireframeState", "SetColorWriteState", "DrawIndexedPrimitive", "DrawPrimitive", "BeginTechnique",
            "EndTechnique",
        };

        // kD3DXImageFileFormats, D3D9Interfaces.cpp:301-309 (binary 0x00D421E4).
        constexpr D3DXIMAGE_FILEFORMAT kD3DXImageFileFormats[5] = {
            D3DXIFF_BMP, D3DXIFF_JPG, D3DXIFF_TGA, D3DXIFF_PNG, D3DXIFF_DDS,
        };

        constexpr std::size_t kKeptDebugLayerMessages = 32;

        std::string JsonEscape(const std::string& text)
        {
            std::string out;
            for (const char c : text) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += c;
                } else if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out += c;
                }
            }
            return out;
        }

        std::string JsonStringList(const std::vector<std::string>& values)
        {
            std::string out = "[";
            for (std::size_t index = 0; index < values.size(); ++index) {
                out += (index == 0 ? "\"" : ", \"") + JsonEscape(values[index]) + "\"";
            }
            return out + "]";
        }

        template <class Vector>
        std::string JsonIntList(const Vector& values)
        {
            std::string out = "[";
            bool first = true;
            for (const auto value : values) {
                out += (first ? "" : ", ") + std::to_string(value);
                first = false;
            }
            return out + "]";
        }

        bool GetOption(const char* const name, const std::uint32_t count, msvc8::vector<msvc8::string>* const out)
        {
            return moho::CFG_GetArgOption(name, count, out);
        }

        /** D3D9 converts a draw's primitive count to vertices/indices the same way for every API. */
        std::uint32_t ElementsForPrimitives(const DrawContext::TOPOLOGY topology, const std::uint32_t primitives)
        {
            switch (topology) {
            case DrawContext::TOPOLOGY_POINTLIST:
                return primitives;
            case DrawContext::TOPOLOGY_LINELIST:
                return primitives * 2U;
            case DrawContext::TOPOLOGY_LINESTRIP:
                return primitives + 1U;
            case DrawContext::TOPOLOGY_TRIANGLESTRIP:
                return primitives + 2U;
            default:
                return primitives * 3U; // TRIANGLELIST
            }
        }

        /** FNV-1a, to name dumped texture sources by content. */
        std::uint64_t Fnv1a(const void* const data, const std::size_t size)
        {
            std::uint64_t hash = 0xCBF29CE484222325ULL;
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t index = 0; index < size; ++index) {
                hash ^= bytes[index];
                hash *= 0x100000001B3ULL;
            }
            return hash;
        }

        // Which backend device stands behind each device the factory handed out (the same pointer
        // unless a DeviceDecorator wrapped it, DeviceFactory.h). Render thread only.
        struct FactoryEntry
        {
            Device* installed = nullptr;
            DeviceDiligent* backend = nullptr;
        };
        std::vector<FactoryEntry> gFactoryEntries;

        Device* MakeDevice(const char* const apiOverride)
        {
            auto* const backend = new DeviceDiligent();
            if (apiOverride != nullptr) {
                backend->SetApiOverride(apiOverride);
            }
            Device* const installed = DecorateCreatedDevice(backend);
            gFactoryEntries.push_back(FactoryEntry{installed, backend});
            return installed;
        }

        DeviceDiligent* FindBackend(Device* const installed)
        {
            for (const FactoryEntry& entry : gFactoryEntries) {
                if (entry.installed == installed) {
                    return entry.backend;
                }
            }
            return static_cast<DeviceDiligent*>(installed);
        }
    } // namespace

    // ---------------------------------------------------------------------------------------------
    // GalDiligent.h, DeviceFactory.h

    bool IsRequestedOnCommandLine()
    {
        msvc8::vector<msvc8::string> options;
        if (!GetOption("/gal", 1, &options) || options.empty()) {
            return false;
        }
        return std::strncmp(options[0].c_str(), "diligent", 8) == 0;
    }

    Device* CreateDevice()
    {
        return MakeDevice(nullptr);
    }

    void SetupDevice(Device* const device, const DeviceContext* const context)
    {
        DeviceDiligent* const backend = FindBackend(device);
        backend->Setup(context);
        NotifyDeviceSetup(device, backend, *context);
    }

    bool IsDiligentApiSupported(const char* const api)
    {
        GraphicsApi parsed = GraphicsApi::D3D11;
        return ParseGraphicsApi(api, &parsed);
    }

    Device* CreateDeviceForApi(const char* const api)
    {
        return IsDiligentApiSupported(api) ? MakeDevice(api) : nullptr;
    }

    Device* GetBackendDevice(Device* const installed)
    {
        return FindBackend(installed);
    }

    // ---------------------------------------------------------------------------------------------

    DeviceDiligent::DeviceDiligent()
        : mDeviceContext(DeviceApi::Unset)
    {}

    DeviceDiligent::~DeviceDiligent()
    {
        Count(0, kSlotNames[0], false);
        if (!mReportWritten && !mReportPath.empty()) {
            WriteReport("shutdown");
        }
        Shutdown();
        gFactoryEntries.erase(std::remove_if(gFactoryEntries.begin(), gFactoryEntries.end(),
                                             [this](const FactoryEntry& entry) { return entry.backend == this; }),
                              gFactoryEntries.end());
    }

    void DeviceDiligent::SetApiOverride(const char* const api)
    {
        mApiOverride = api != nullptr ? api : "";
    }

    void DeviceDiligent::Count(const int slot, const char* const name, const bool noOp)
    {
        ++mSlotCalls[slot];
        if (noOp && !mSlotLogged[slot].exchange(true)) {
            gpg::Logf("[gal-diligent] slot %d %s: not implemented", slot, name);
        }
    }

    void DeviceDiligent::ReadOptions()
    {
        msvc8::vector<msvc8::string> options;
        if (!mApiOverride.empty()) {
            mApi = mApiOverride;
        } else if (GetOption("/gal", 1, &options) && !options.empty()) {
            const char* const value = options[0].c_str();
            const char* const colon = std::strchr(value, ':');
            mApi = (colon != nullptr) ? colon + 1 : "d3d11";
        }
        options.clear();
        if (GetOption("/galreport", 1, &options) && !options.empty()) {
            mReportPath = options[0].c_str();
        }
        options.clear();
        if (GetOption("/galexitframes", 1, &options) && !options.empty()) {
            mExitFrames = static_cast<std::uint64_t>(std::strtoull(options[0].c_str(), nullptr, 10));
        }
        options.clear();
        if (GetOption("/galreportframe", 1, &options) && !options.empty()) {
            mReportFrame = static_cast<std::uint64_t>(std::strtoull(options[0].c_str(), nullptr, 10));
        }
        options.clear();
        if (GetOption("/galdumpfx", 1, &options) && !options.empty()) {
            mDumpFxDir = options[0].c_str();
        }
        options.clear();
        if (GetOption("/galdumptex", 1, &options) && !options.empty()) {
            mDumpTexDir = options[0].c_str();
        }
        options.clear();
        if (GetOption("/galtex2d", 1, &options) && !options.empty()) {
            mPortableTexture2D = std::strcmp(options[0].c_str(), "portable") == 0;
        }
        options.clear();
        if (GetOption("/galhalfpixel", 1, &options) && !options.empty()) {
            const char* const mode = options[0].c_str();
            mHalfPixel = std::strcmp(mode, "viewport") == 0 ? HalfPixelMode::Viewport
                         : std::strcmp(mode, "none") == 0   ? HalfPixelMode::None
                                                            : HalfPixelMode::Shader;
        }
        mValidation = !GetOption("/galnovalidation", 0, nullptr);
        SetGlMirrorDisabled(GetOption("/galglnomirror", 0, nullptr));
        SetSwapChainBgraRequested(GetOption("/galswapchainbgra", 0, nullptr));
        mDebugLayerSelfTest = GetOption("/galdebuglayerselftest", 0, nullptr);
        mRunSelfTest = GetOption("/galselftest", 0, nullptr);
    }

    void DeviceDiligent::Shutdown()
    {
        if (mHost && mHost->IsCreated()) {
            // The effect layer drops every GPU object it made on this device (PassBinding.h).
            SetPassSink(nullptr);
            SetEffectRenderDevice(nullptr);
        }
        mPassBinding = nullptr;
        mBound = OutputContext{};
        mVertexFormat.reset();
        for (StreamBinding& stream : mStreams) {
            stream = StreamBinding{};
        }
        mIndexBuffer.reset();
        mHeads.clear();
        mPipelineState.reset();
        mDrawPath.reset();
        if (mCursorIcon != nullptr) {
            ::SetCursor(static_cast<HCURSOR>(mPreviousCursor));
            ::DestroyIcon(static_cast<HICON>(mCursorIcon));
            mCursorIcon = nullptr;
            mPreviousCursor = nullptr;
        }
        mGpu.reset();
        if (mHost) {
            mHost->DrainDebugLayer(&mDebugLayerMessages, kKeptDebugLayerMessages - std::min(mDebugLayerMessages.size(), kKeptDebugLayerMessages));
            mHost->Destroy();
        }
        if (mOracle) {
            mOracle->Shutdown();
        }
    }

    // DeviceD3D9::Setup's order (D3D9Interfaces.cpp:1745-1843): validate the context, bring the API
    // up, fill the capabilities, the pipeline state's InitState, then the heads.
    void DeviceDiligent::Setup(const DeviceContext* const context)
    {
        Shutdown();
        ReadOptions();
        mCurThreadId = static_cast<int>(::GetCurrentThreadId());

        const int headCount = context->GetHeadCount();
        if (headCount == 0) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid device context specified");
        }
        if (!ParseGraphicsApi(mApi.c_str(), &mGraphicsApi)) {
            const std::string message = "/gal diligent:" + mApi + " is not implemented (diligent:d3d11, diligent:vk, diligent:gl)";
            ThrowGalError("DeviceDiligent.cpp", __LINE__, message.c_str());
        }
        mApi = GraphicsApiName(mGraphicsApi);

        std::string error;
        mOracle = std::make_unique<D3D9Oracle>();
        if (!mOracle->Init(&error)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, error.c_str());
        }
        mOracle->FillCapabilities(*context, mDeviceContext);

        // DeviceD3D9::GetHeadParameters' device window (D3D9Interfaces.cpp:1921-1924): the frame for a
        // full-screen head 0 (mWindowed means full screen, D3D9Interfaces.cpp:1925), else the viewport.
        const Head& head = mDeviceContext.GetHead(0U);
        void* const window = head.mWindowed ? head.mHandle : head.mWindow;
        mHost = std::make_unique<DiligentHost>();
        if (!mHost->Create(window, head.mWidth, head.mHeight, mGraphicsApi, mValidation, &error)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, error.c_str());
        }
        mGpu = mHost->GetGpu();
        mDrawPath = std::make_unique<DrawPath>(mGpu);
        mDrawPath->SetHalfPixelMode(mHalfPixel);

        if (mDebugLayerSelfTest) {
            mSelfTestErrors = mGraphicsApi == GraphicsApi::OpenGL ? mHost->SelfTestGlDebugOutput(&mSelfTestSample)
                                                                  : mHost->SelfTestDebugLayer(&mSelfTestSample);
            gpg::Logf("[gal-diligent] debug layer self test: %u error(s) provoked on purpose, not counted: %s", mSelfTestErrors,
                      mSelfTestSample.c_str());
        }
        mPipelineState.reset(new PipelineStateDiligent());
        mShadow.ResetToSetupState();
        // The effect layer creates its shaders, buffers and signatures on this device, and calls back
        // into OnBeginPass/OnEndPass (PassBinding.h). Its shaders go through the API's route
        // (ShaderCompileDiligent.h).
        SetEffectShaderCompiler(&CompileHlslShader);
        {
            msvc8::vector<msvc8::string> trilinear;
            int mode = 0;
            if (GetOption("/galtrilinear", 1, &trilinear) && !trilinear.empty()) {
                mode = std::strcmp(trilinear[0].c_str(), "aniso1") == 0 ? 1 : std::strcmp(trilinear[0].c_str(), "mippoint") == 0 ? 2
                       : std::strcmp(trilinear[0].c_str(), "level1") == 0   ? 3
                       : std::strcmp(trilinear[0].c_str(), "aniso2") == 0   ? 4
                       : std::strcmp(trilinear[0].c_str(), "aniso16") == 0  ? 5
                       : std::strcmp(trilinear[0].c_str(), "bias64") == 0   ? 6
                                                                            : 0;
            }
            SetEffectTrilinearMode(mode);
        }
        SetEffectRenderDevice(mGpu->Device());
        SetPassSink(this);
        CreateHeads();
        if (mRunSelfTest) {
            RunSelfTest();
        }

        gpg::Logf(
            "[gal-diligent] %s device on \"%s\", head %ux%u (%s), swap chain %s, validation %s, D3D11 debug layer %s, GL debug output %s, "
            "Vulkan layers %s, oracle %s, half pixel %s, GetTexture2D %s",
            mApi.c_str(), mHost->GetAdapterDescription().c_str(), head.mWidth, head.mHeight, head.mWindowed ? "full screen" : "windowed",
            mHost->GetSwapChainFormat().c_str(), mValidation ? "on" : "off", mHost->IsDebugLayerActive() ? "active" : "absent",
            mHost->GetGlDebugCounts().active ? "active" : "absent", mHost->GetVulkanLayerReport().c_str(),
            mOracle->GetDeviceDescription().c_str(),
            mHalfPixel == HalfPixelMode::Shader ? "shader" : mHalfPixel == HalfPixelMode::Viewport ? "viewport" : "none",
            mPortableTexture2D ? "portable" : "oracle"
        );
        if (headCount > 1) {
            gpg::Warnf("[gal-diligent] %d heads requested; only head 0 is presented", headCount);
        }
    }

    // DeviceD3D9::CreateHeads (D3D9Interfaces.cpp:1960-2006): per head the back buffer and a D24S8
    // depth target (token 3) of the back buffer's size; the back buffer starts cleared to black.
    void DeviceDiligent::CreateHeads()
    {
        mHeads.clear();
        const int headCount = mDeviceContext.GetHeadCount();
        mHeads.resize(static_cast<std::size_t>(headCount));
        for (int headIndex = 0; headIndex < headCount; ++headIndex) {
            const Head& head = mDeviceContext.GetHead(static_cast<std::uint32_t>(headIndex));
            const std::uint32_t width = headIndex == 0 ? mHost->GetHeadWidth() : head.mWidth;
            const std::uint32_t height = headIndex == 0 ? mHost->GetHeadHeight() : head.mHeight;
            // RenderTargetD3D9::SetSurface records only the size (D3D9Interfaces.cpp:5160-5169).
            RenderTargetContext surfaceContext{};
            surfaceContext.width_ = width;
            surfaceContext.height_ = height;
            surfaceContext.format_ = 2U; // A8R8G8B8, the back buffer's format (D3D9Interfaces.cpp:1918)
            if (headIndex == 0) {
                mHeads[headIndex].surface.reset(new RenderTargetDiligent(surfaceContext, mGpu, mHost->GetHeadTexture(), 0));
            } else {
                mHeads[headIndex].surface.reset(new RenderTargetDiligent(surfaceContext, mGpu));
            }
            const DepthStencilTargetContext depthContext(width, height, 3U, false);
            mHeads[headIndex].depthStencil.reset(new DepthStencilTargetDiligent(depthContext, mGpu));
        }
        mViewport = D3DVIEWPORT9{0U, 0U, mHost->GetHeadWidth(), mHost->GetHeadHeight(), 0.0f, 1.0f};
        mDrawPath->SetViewport(ViewportDesc{0.0F, 0.0F, static_cast<float>(mViewport.Width), static_cast<float>(mViewport.Height), 0.0F, 1.0F});
        // Clear(true, false, false, 0, ...) per head, as CreateHeads does before its first Present.
        mBound = mHeads[0];
        BindOutput();
        mDrawPath->Clear(true, false, false, 0U, 1.0F, 0U, mShadow);
    }

    // --- slots 1-8 ---------------------------------------------------------------------------------

    void* DeviceDiligent::GetLog()
    {
        Count(1, kSlotNames[1], false);
        return &mLog;
    }

    DeviceContext* DeviceDiligent::GetDeviceContext()
    {
        Count(2, kSlotNames[2], false);
        Func1();
        return &mDeviceContext;
    }

    int DeviceDiligent::GetCurThreadId()
    {
        Count(3, kSlotNames[3], false);
        return mCurThreadId;
    }

    // Empty in both shipped backends (0x008E8200, 0x008F86E0). Not counted: GetDeviceContext and
    // the others call it on every use.
    void DeviceDiligent::Func1() const
    {}

    void DeviceDiligent::GetModesForAdapter(msvc8::vector<HeadAdapterMode>& outModes, const int adapterIndex)
    {
        Count(5, kSlotNames[5], false);
        mOracle->GetModesForAdapter(outModes, adapterIndex);
    }

    OutputContext* DeviceDiligent::GetHeadOutputContext(const unsigned int headIndex)
    {
        Count(6, kSlotNames[6], false);
        if (headIndex >= mHeads.size()) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid head index specified");
        }
        return &mHeads[headIndex];
    }

    const OutputContext* DeviceDiligent::GetHeadOutputContext(const unsigned int headIndex) const
    {
        const_cast<DeviceDiligent*>(this)->Count(7, kSlotNames[7], false);
        if (headIndex >= mHeads.size()) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid head index specified");
        }
        return &mHeads[headIndex];
    }

    boost::shared_ptr<PipelineState> DeviceDiligent::GetPipelineState()
    {
        Count(8, kSlotNames[8], false);
        return mPipelineState;
    }

    // --- slots 9-16: creation -----------------------------------------------------------------------

    boost::shared_ptr<Effect> DeviceDiligent::CreateEffect(const EffectContext& context)
    {
        Count(9, kSlotNames[9], false);
        return CreateEffectFromContext(*mOracle, context, mDumpFxDir);
    }

    boost::shared_ptr<Texture> DeviceDiligent::CreateTexture(const TextureContext* const context)
    {
        Count(10, kSlotNames[10], false);
        if (::GetCurrentThreadId() != static_cast<DWORD>(mCurThreadId)) {
            ++mCreateTextureOffThread;
        }
        if (!mDumpTexDir.empty() && context->source_ == 1U && context->dataEnd_ > context->dataBegin_) {
            DumpTextureSource("tex", TextureSourceBegin(*context), context->dataEnd_ - context->dataBegin_);
        }
        return CreateTextureFromContext(*mOracle, *context, mGpu);
    }

    boost::shared_ptr<RenderTarget> DeviceDiligent::CreateRenderTarget(const RenderTargetContext* const context)
    {
        Count(11, kSlotNames[11], false);
        return boost::shared_ptr<RenderTarget>(new RenderTargetDiligent(*context, mGpu));
    }

    boost::shared_ptr<CubeRenderTarget> DeviceDiligent::CreateCubeRenderTarget(const CubeRenderTargetContext* const context)
    {
        Count(12, kSlotNames[12], false);
        return boost::shared_ptr<CubeRenderTarget>(new CubeRenderTargetDiligent(*context, mGpu));
    }

    boost::shared_ptr<DepthStencilTarget> DeviceDiligent::CreateDepthStencilTarget(const DepthStencilTargetContext* const context)
    {
        Count(13, kSlotNames[13], false);
        return boost::shared_ptr<DepthStencilTarget>(new DepthStencilTargetDiligent(*context, mGpu));
    }

    boost::shared_ptr<VertexFormat> DeviceDiligent::CreateVertexFormat(const std::uint32_t formatCode)
    {
        Count(14, kSlotNames[14], false);
        return boost::shared_ptr<VertexFormat>(new VertexFormatDiligent(formatCode));
    }

    boost::shared_ptr<VertexBuffer> DeviceDiligent::CreateVertexBuffer(const VertexBufferContext* const context)
    {
        Count(15, kSlotNames[15], false);
        return boost::shared_ptr<VertexBuffer>(new VertexBufferDiligent(*context, mGpu));
    }

    boost::shared_ptr<IndexBuffer> DeviceDiligent::CreateIndexBuffer(const IndexBufferContext* const context)
    {
        Count(16, kSlotNames[16], false);
        if (context->format_ == 0U) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "undefined index buffer format"); // as D3D9Interfaces.cpp:2838
        }
        return boost::shared_ptr<IndexBuffer>(new IndexBufferDiligent(*context, mGpu));
    }

    // --- slots 17-24: transfers, saving, decoding ---------------------------------------------------

    // DeviceD3D9::GetRenderTargetData (D3D9Interfaces.cpp:2866-2898): the target's pixels into level 0
    // of a system-memory texture of its size and format. The frame harness reads frames this way
    // (GetHeadOutputContext -> GetRenderTargetData into an A8R8G8B8 texture -> Lock,
    // port/graphics/capture/GalCapture.cpp), so the destination receives D3D9's byte order (B, G, R, A).
    void DeviceDiligent::CopyRenderTargetToTexture(const boost::shared_ptr<RenderTarget>& source, const boost::shared_ptr<Texture>& destination,
                                                   const char* const slot)
    {
        auto* const target = dynamic_cast<RenderTargetDiligent*>(source.get());
        auto* const texture = dynamic_cast<TextureDiligent*>(destination.get());
        if (target == nullptr) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Missing source texture");
        }
        if (texture == nullptr) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Missing dest   texture");
        }
        GpuTexture* const gpuTexture = target->GetGpu();
        std::vector<std::uint8_t> bgra;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (gpuTexture == nullptr || !mHost->ReadTextureBgra8(gpuTexture->Texture(), &bgra, &width, &height)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, (std::string(slot) + ": the render target cannot be read back").c_str());
        }
        mDrawPath->InvalidateTargets(); // the readback unbound them
        const TextureContext* const context = texture->GetContext();
        if (context->width_ != width || context->height_ != height || (context->format_ != 2U && context->format_ != 3U)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, (std::string(slot) + ": destination is not an A8R8G8B8 texture of the target's size").c_str());
        }
        const TextureLockRect lock = texture->Lock(0, RECT{}, 0);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(lock.bits) + static_cast<std::size_t>(row) * static_cast<std::size_t>(lock.pitch),
                        bgra.data() + static_cast<std::size_t>(row) * width * 4U, static_cast<std::size_t>(width) * 4U);
        }
        texture->Unlock(lock);
    }

    void DeviceDiligent::GetRenderTargetData(const boost::shared_ptr<RenderTarget>& source, const boost::shared_ptr<Texture>& destination)
    {
        Count(17, kSlotNames[17], false);
        CopyRenderTargetToTexture(source, destination, "GetRenderTargetData");
    }

    // DeviceD3D9::StretchRect (D3D9Interfaces.cpp:2907-2931): IDirect3DDevice9::StretchRect with
    // D3DTEXF_LINEAR between two colour targets.
    void DeviceDiligent::StretchRect(
        const boost::shared_ptr<RenderTarget>& source,
        const boost::shared_ptr<RenderTarget>& destination,
        const RECT* const sourceRect,
        const RECT* const destinationRect
    )
    {
        Count(18, kSlotNames[18], false);
        auto* const from = dynamic_cast<RenderTargetDiligent*>(source.get());
        auto* const to = dynamic_cast<RenderTargetDiligent*>(destination.get());
        if (from == nullptr) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Missing source texture");
        }
        if (to == nullptr) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Missing dest   texture");
        }
        GpuTexture* const fromTexture = from->GetGpu();
        GpuTexture* const toTexture = to->GetGpu();
        if (fromTexture == nullptr || toTexture == nullptr) {
            return;
        }
        std::int32_t rects[2][4] = {};
        if (sourceRect != nullptr) {
            rects[0][0] = sourceRect->left;
            rects[0][1] = sourceRect->top;
            rects[0][2] = sourceRect->right;
            rects[0][3] = sourceRect->bottom;
        }
        if (destinationRect != nullptr) {
            rects[1][0] = destinationRect->left;
            rects[1][1] = destinationRect->top;
            rects[1][2] = destinationRect->right;
            rects[1][3] = destinationRect->bottom;
        }
        mDrawPath->StretchRect(fromTexture->Texture(), sourceRect != nullptr ? rects[0] : nullptr, toTexture->Texture(),
                               destinationRect != nullptr ? rects[1] : nullptr);
    }

    // DeviceD3D9::UpdateSurface (D3D9Interfaces.cpp:2941-2983): D3DXLoadSurfaceFromSurface between the
    // two textures' level 0 with D3DX_DEFAULT filtering - here on the D3DX images, after which the
    // destination's written rectangle goes to the GPU like any locked one.
    void DeviceDiligent::UpdateSurface(
        const boost::shared_ptr<Texture>& source,
        const boost::shared_ptr<Texture>& destination,
        const RECT* const sourceRect,
        const RECT* const destinationRect
    )
    {
        Count(19, kSlotNames[19], false);
        auto* const from = dynamic_cast<TextureDiligent*>(source.get());
        auto* const to = dynamic_cast<TextureDiligent*>(destination.get());
        if (from == nullptr) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Missing source texture");
        }
        if (to == nullptr) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Missing dest   texture");
        }
        if (from->GetScratch() == nullptr || from->GetScratch()->GetType() != D3DRTYPE_TEXTURE || to->GetScratch() == nullptr ||
            to->GetScratch()->GetType() != D3DRTYPE_TEXTURE) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "UpdateSurface: 2D textures only");
        }
        IDirect3DSurface9* fromSurface = nullptr;
        IDirect3DSurface9* toSurface = nullptr;
        static_cast<IDirect3DTexture9*>(from->GetScratch())->GetSurfaceLevel(0U, &fromSurface);
        static_cast<IDirect3DTexture9*>(to->GetScratch())->GetSurfaceLevel(0U, &toSurface);
        const HRESULT result =
            D3DXLoadSurfaceFromSurface(toSurface, nullptr, destinationRect, fromSurface, nullptr, sourceRect, D3DX_DEFAULT, 0U);
        if (fromSurface != nullptr) {
            fromSurface->Release();
        }
        if (toSurface != nullptr) {
            toSurface->Release();
        }
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
        }
        // Mark the destination's level 0 written: a lock/unlock over the rectangle (or the level).
        const RECT whole{};
        const TextureLockRect lock = to->Lock(0, destinationRect != nullptr ? *destinationRect : whole, 0);
        to->Unlock(lock);
    }

    void DeviceDiligent::SaveCubeRenderTarget(const boost::shared_ptr<CubeRenderTarget>& cubeTarget, const msvc8::string& filePath)
    {
        // No engine caller (m6u-GAL.txt section 1, slot 20).
        static_cast<void>(cubeTarget);
        static_cast<void>(filePath);
        Count(20, kSlotNames[20], true);
    }

    // DeviceD3D9::SaveRenderTarget (D3D9Interfaces.cpp:3034-3063): the target read back into an
    // A8R8G8B8 D3DX image and saved with D3DXSaveSurfaceToFileA in the requested format.
    void DeviceDiligent::SaveRenderTarget(const boost::shared_ptr<RenderTarget>& renderTarget, const msvc8::string& filePath, const int fileFormat)
    {
        Count(21, kSlotNames[21], false);
        if (filePath.mySize == 0U) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Missing file");
        }
        auto* const target = dynamic_cast<RenderTargetDiligent*>(renderTarget.get());
        GpuTexture* const texture = target != nullptr ? target->GetGpu() : nullptr;
        std::vector<std::uint8_t> bgra;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (texture == nullptr || !mHost->ReadTextureBgra8(texture->Texture(), &bgra, &width, &height)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "Unable to get back buffer surface");
        }
        mDrawPath->InvalidateTargets();
        IDirect3DSurface9* surface = nullptr;
        HRESULT result = mOracle->GetDevice()->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &surface, nullptr);
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
        }
        D3DLOCKED_RECT locked{};
        surface->LockRect(&locked, nullptr, 0);
        for (std::uint32_t row = 0; row < height; ++row) {
            std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.Pitch),
                        bgra.data() + static_cast<std::size_t>(row) * width * 4U, static_cast<std::size_t>(width) * 4U);
        }
        surface->UnlockRect();
        result = D3DXSaveSurfaceToFileA(filePath.c_str(), kD3DXImageFileFormats[std::clamp(fileFormat, 0, 4)], surface, nullptr, nullptr);
        surface->Release();
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
        }
    }

    // DeviceD3D9::SaveTexture (D3D9Interfaces.cpp:3073-3123) on the D3DX image.
    void DeviceDiligent::SaveTexture(
        const boost::shared_ptr<Texture>& texture,
        const msvc8::string& filePath,
        const int fileFormat,
        gpg::MemBuffer<char>* const outBuffer
    )
    {
        Count(22, kSlotNames[22], false);
        auto* const textureDiligent = dynamic_cast<TextureDiligent*>(texture.get());
        IDirect3DBaseTexture9* const scratch = textureDiligent != nullptr ? textureDiligent->GetScratch() : nullptr;
        if (scratch == nullptr || scratch->GetType() != D3DRTYPE_TEXTURE || fileFormat < 0 || fileFormat > 4) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "SaveTexture: no 2D texture data");
        }
        IDirect3DSurface9* surface = nullptr;
        HRESULT result = static_cast<IDirect3DTexture9*>(scratch)->GetSurfaceLevel(0U, &surface);
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
        }
        const D3DXIMAGE_FILEFORMAT format = kD3DXImageFileFormats[fileFormat];
        if (outBuffer != nullptr) {
            ID3DXBuffer* fileBuffer = nullptr;
            result = D3DXSaveSurfaceToFileInMemory(&fileBuffer, format, surface, nullptr, nullptr);
            if (SUCCEEDED(result)) {
                if (outBuffer->Size() != fileBuffer->GetBufferSize()) {
                    *outBuffer = gpg::AllocMemBuffer(fileBuffer->GetBufferSize());
                }
                std::memcpy(outBuffer->GetPtr(0U, 0U), fileBuffer->GetBufferPointer(), fileBuffer->GetBufferSize());
                fileBuffer->Release();
            }
        } else {
            result = D3DXSaveSurfaceToFileA(filePath.c_str(), format, surface, nullptr, nullptr);
        }
        surface->Release();
        if (FAILED(result)) {
            ThrowGalErrorFromHresult("DeviceDiligent.cpp", __LINE__, result);
        }
    }

    std::string DeviceDiligent::DumpTextureSource(const char* const kind, const void* const data, const std::uint32_t bytes)
    {
        // Named by call order and content hash; tools/tex2d_test.py names them by VFS path through
        // the hash.
        char name[96];
        std::snprintf(name, sizeof(name), "\\%s_%04llu_%016llx", kind, static_cast<unsigned long long>(mTextureDumps++),
                      static_cast<unsigned long long>(Fnv1a(data, bytes)));
        const std::string stem = mDumpTexDir + name;
        std::ofstream out(stem + ".bin", std::ios::binary);
        out.write(static_cast<const char*>(data), bytes);
        return stem;
    }

    // DeviceD3D9::GetTexture2D (D3D9Interfaces.cpp:3134-3244): the image as DXT5 blocks. The D3DX oracle
    // reproduces D3D9 by construction; the portable path (Texture2DPortable.h) is what Android uses and
    // the GetTexture2D unit test (tools/tex2d_test.cpp) holds it to D3DX's bytes.
    void DeviceDiligent::GetTexture2D(
        const void* const sourceData,
        const std::uint32_t sourceBytes,
        gpg::MemBuffer<char>* const outTextureData,
        std::uint32_t* const outWidth,
        int* const outHeight
    )
    {
        Count(23, kSlotNames[23], false);
        ++mTexture2DCalls;
        if (::GetCurrentThreadId() != static_cast<DWORD>(mCurThreadId)) {
            ++mTexture2DOffThread;
        }
        std::string dumpStem;
        if (sourceData != nullptr && !mDumpTexDir.empty()) {
            dumpStem = DumpTextureSource("tex2d", sourceData, sourceBytes);
        }
        if (!mPortableTexture2D) {
            mOracle->GetTexture2D(sourceData, sourceBytes, outTextureData, outWidth, outHeight);
        } else if (sourceData != nullptr) {
            Texture2DBlocks blocks;
            std::string error;
            if (!DecodeTexture2DPortable(sourceData, sourceBytes, &blocks, &error)) {
                ThrowGalError("DeviceDiligent.cpp", __LINE__, ("GetTexture2D (portable): " + error).c_str());
            }
            *outWidth = blocks.width;
            *outHeight = static_cast<int>(blocks.height);
            if (outTextureData->Size() != blocks.data.size()) {
                *outTextureData = gpg::AllocMemBuffer(blocks.data.size());
            }
            if (!blocks.data.empty()) {
                std::memcpy(outTextureData->GetPtr(0U, 0U), blocks.data.data(), blocks.data.size());
            }
        }
        if (!dumpStem.empty()) {
            // What the engine received: u32 width, u32 height, the blocks (the unit test compares it with
            // the D3D9 HAL reference).
            std::ofstream out(dumpStem + ".out", std::ios::binary);
            const std::uint32_t height = static_cast<std::uint32_t>(*outHeight);
            out.write(reinterpret_cast<const char*>(outWidth), 4);
            out.write(reinterpret_cast<const char*>(&height), 4);
            out.write(outTextureData->GetPtr(0U, 0U), static_cast<std::streamsize>(outTextureData->Size()));
        }
    }

    // As both shipped backends (0x008E9B40): clears the handle, consumes the argument.
    boost::weak_ptr<void>* DeviceDiligent::Func7(boost::weak_ptr<void>* const outWeakHandle, boost::shared_ptr<void> temporarySharedHandle)
    {
        Count(24, kSlotNames[24], false);
        static_cast<void>(temporarySharedHandle);
        outWeakHandle->reset();
        return outWeakHandle;
    }

    // --- slots 25-33: lifetime, frame, cursor -------------------------------------------------------

    // DeviceD3D9::Reset(context) (D3D9Interfaces.cpp:2210-2244) resets the device for a new context,
    // which is how a resize arrives: the capabilities are refilled, the swap chain resized, the head
    // targets rebuilt, and the render states return to InitState (D3D9Interfaces.cpp:2240).
    void DeviceDiligent::Reset(DeviceContext* const context)
    {
        Count(25, kSlotNames[25], false);
        DeviceContext requested = *context;
        mOracle->FillCapabilities(requested, mDeviceContext);
        const Head& head = mDeviceContext.GetHead(0U);
        std::string error;
        mDrawPath->InvalidateTargets();
        mBound = OutputContext{};
        if (!mHost->Resize(head.mWidth, head.mHeight, &error)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, error.c_str());
        }
        mShadow.ResetToSetupState();
        CreateHeads();
        gpg::Logf("[gal-diligent] Reset: head %ux%u", head.mWidth, head.mHeight);
    }

    void DeviceDiligent::Reset()
    {
        Count(26, kSlotNames[26], false);
        DeviceContext current = mDeviceContext;
        Reset(&current);
    }

    // 0: never lost. D3D11 has no device loss in the D3D9 sense (a removed device shows up as Diligent
    // errors, which the report counts); a resize arrives through Reset(context).
    int DeviceDiligent::TestCooperativeLevel()
    {
        Count(27, kSlotNames[27], false);
        return 0;
    }

    void DeviceDiligent::BeginScene()
    {
        Count(28, kSlotNames[28], false);
        mInScene = true;
    }

    void DeviceDiligent::EndScene()
    {
        Count(29, kSlotNames[29], false);
        mInScene = false;
    }

    void DeviceDiligent::Present()
    {
        Count(30, kSlotNames[30], false);
        Func1();
        mHost->Present(mDeviceContext.mVSync ? 1U : 0U);
        mDrawPath->InvalidateTargets(); // Present leaves no target bound
        ++mPresents;
        const std::size_t kept = mDebugLayerMessages.size();
        mHost->DrainDebugLayer(&mDebugLayerMessages, kept < kKeptDebugLayerMessages ? kKeptDebugLayerMessages - kept : 0);
        for (std::size_t index = kept; index < mDebugLayerMessages.size(); ++index) {
            gpg::Warnf("[gal-diligent] D3D11 debug layer: %s", mDebugLayerMessages[index].c_str());
        }
        if (mPresents == 1 || mPresents % 300 == 0) {
            const DebugLayerCounts counts = mHost->GetDebugLayerCounts();
            const DrawPathStats& stats = mDrawPath->GetStats();
            gpg::Logf(
                "[gal-diligent] present %llu: draws %llu (skipped: no pass %llu, no program %llu, no pipeline %llu, commit %llu), "
                "PSOs %llu, debug layer errors %llu warnings %llu",
                static_cast<unsigned long long>(mPresents), static_cast<unsigned long long>(stats.draws),
                static_cast<unsigned long long>(stats.skippedNoBinding), static_cast<unsigned long long>(stats.skippedNoProgram),
                static_cast<unsigned long long>(stats.skippedNoPipeline), static_cast<unsigned long long>(stats.skippedCommit),
                static_cast<unsigned long long>(stats.psoCreated), static_cast<unsigned long long>(counts.error + counts.corruption),
                static_cast<unsigned long long>(counts.warning)
            );
        }
        if (mReportFrame != 0 && mPresents == mReportFrame) {
            WriteReport("reportframe");
        }
        if (mExitFrames != 0 && mPresents == mExitFrames && !mExitRequested) {
            mExitRequested = true;
            WriteReport("exitframes");
            gpg::Logf("[gal-diligent] %llu frames presented; requesting exit (/galexitframes)", static_cast<unsigned long long>(mPresents));
            moho::WIN_AppRequestExit();
        }
    }

    // The Win32 cursor, as CursorD3D10 builds it (0x008F8130, D3D10Interfaces.cpp:516-563): a 32x32
    // ARGB icon from level 0 of the cursor texture, rows bottom-up. CScApp selects the Windows cursor
    // for this backend (d3d_WindowsCursor, CScApp.cpp:1182-1183). The texture is a D3D9-layout
    // A8R8G8B8 image here (D3D10's is RGBA, hence its red/blue swap), so the DWORDs copy as they are.
    void DeviceDiligent::SetCursor(const CursorContext* const context)
    {
        Count(31, kSlotNames[31], false);
        if (mCursorIcon != nullptr) {
            ::SetCursor(static_cast<HCURSOR>(mPreviousCursor));
            ::DestroyIcon(static_cast<HICON>(mCursorIcon));
            mCursorIcon = nullptr;
        }
        auto* const texture = dynamic_cast<TextureDiligent*>(context->texture_.get());
        if (texture == nullptr || texture->GetContext()->width_ < 32U || texture->GetContext()->height_ < 32U) {
            return;
        }
        BITMAPV5HEADER bitmapInfo{};
        bitmapInfo.bV5Size = sizeof(BITMAPV5HEADER);
        bitmapInfo.bV5Width = 32L;
        bitmapInfo.bV5Height = 32L;
        bitmapInfo.bV5Planes = 1;
        bitmapInfo.bV5BitCount = 32;
        bitmapInfo.bV5Compression = BI_BITFIELDS;
        bitmapInfo.bV5RedMask = 0x00FF0000U;
        bitmapInfo.bV5GreenMask = 0x0000FF00U;
        bitmapInfo.bV5BlueMask = 0x000000FFU;
        bitmapInfo.bV5AlphaMask = 0xFF000000U;
        HDC const dc = ::GetDC(nullptr);
        void* pixels = nullptr;
        HBITMAP const colorBitmap = ::CreateDIBSection(dc, reinterpret_cast<const BITMAPINFO*>(&bitmapInfo), DIB_RGB_COLORS, &pixels, nullptr, 0U);
        ::ReleaseDC(nullptr, dc);
        if (colorBitmap == nullptr || pixels == nullptr) {
            return;
        }
        const TextureLockRect lock = texture->Lock(0, RECT{}, 2);
        auto* const destination = static_cast<std::uint32_t*>(pixels);
        for (std::uint32_t y = 0; y < 32U; ++y) {
            const auto* const row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(lock.bits) +
                                                                             static_cast<std::size_t>(lock.pitch) * (31U - y));
            std::memcpy(destination + y * 32U, row, 32U * 4U);
        }
        texture->Unlock(lock);
        HBITMAP const maskBitmap = ::CreateBitmap(32, 32, 1U, 1U, nullptr);
        ICONINFO iconInfo{};
        iconInfo.fIcon = FALSE;
        iconInfo.xHotspot = static_cast<DWORD>(context->hotspotX_);
        iconInfo.yHotspot = static_cast<DWORD>(context->hotspotY_);
        iconInfo.hbmMask = maskBitmap;
        iconInfo.hbmColor = colorBitmap;
        mCursorIcon = ::CreateIconIndirect(&iconInfo);
        ::DeleteObject(colorBitmap);
        ::DeleteObject(maskBitmap);
        mPreviousCursor = ::SetCursor(static_cast<HCURSOR>(mCursorIcon));
    }

    // CursorD3D10::InitCursor (0x008F8430): make the icon current again.
    void DeviceDiligent::InitCursor()
    {
        Count(32, kSlotNames[32], false);
        if (mCursorIcon != nullptr) {
            ::SetCursor(static_cast<HCURSOR>(mCursorIcon));
        }
    }

    // CursorD3D10::ShowCursor (0x008F84F0): drive the Win32 display count to shown or hidden. Returns
    // the previous state, as IDirect3DDevice9::ShowCursor does.
    int DeviceDiligent::ShowCursor(const bool show)
    {
        Count(33, kSlotNames[33], false);
        const bool previous = mCursorShown;
        mCursorShown = show;
        if (mCursorIcon != nullptr) {
            if (show) {
                while (::ShowCursor(TRUE) < 0) {
                }
            } else {
                while (::ShowCursor(FALSE) >= 0) {
                }
            }
        }
        return previous ? 1 : 0;
    }

    // --- slots 34-49: state and drawing -------------------------------------------------------------

    void DeviceDiligent::SetViewport(const D3DVIEWPORT9* const viewport)
    {
        Count(34, kSlotNames[34], false);
        mViewport = *viewport;
        mDrawPath->SetViewport(ViewportDesc{static_cast<float>(viewport->X), static_cast<float>(viewport->Y), static_cast<float>(viewport->Width),
                                            static_cast<float>(viewport->Height), viewport->MinZ, viewport->MaxZ});
    }

    void DeviceDiligent::GetViewport(D3DVIEWPORT9* const outViewport)
    {
        Count(35, kSlotNames[35], false);
        *outViewport = mViewport;
    }

    void DeviceDiligent::BindOutput()
    {
        OutputTargets targets;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (auto* const surface = dynamic_cast<RenderTargetDiligent*>(mBound.surface.get())) {
            if (GpuTexture* const texture = surface->GetGpu()) {
                targets.renderTarget = texture->RenderTargetView();
                width = texture->Width();
                height = texture->Height();
            }
        } else if (auto* const cube = dynamic_cast<CubeRenderTargetDiligent*>(mBound.cubeTarget.get())) {
            if (GpuTexture* const texture = cube->GetGpu()) {
                targets.renderTarget = texture->RenderTargetView(static_cast<std::uint32_t>(mBound.face));
                width = texture->Width();
                height = texture->Height();
            }
        }
        if (auto* const depth = dynamic_cast<DepthStencilTargetDiligent*>(mBound.depthStencil.get())) {
            if (GpuTexture* const texture = depth->GetGpu()) {
                targets.depthStencil = texture->DepthStencilView();
                if (width == 0U) {
                    width = texture->Width();
                    height = texture->Height();
                }
            }
        }
        targets.width = width;
        targets.height = height;
        mDrawPath->SetTargets(targets);
    }

    // Slot 36 binds the output context (m6u-GAL.txt section 1): the base copy, then the binding
    // (DeviceD3D9::ClearTarget, D3D9Interfaces.cpp:3461-3536). IDirect3DDevice9::SetRenderTarget resets
    // the viewport to the whole new target, and DeviceD3D9 calls it only when the colour target
    // changes, so the viewport resets exactly then.
    void DeviceDiligent::ClearTarget(const OutputContext* const context)
    {
        Count(36, kSlotNames[36], false);
        Device::ClearTarget(context);
        const bool colorChanged = context->surface.get() != mBound.surface.get() || context->cubeTarget.get() != mBound.cubeTarget.get() ||
                                  (context->cubeTarget.get() != nullptr && context->face != mBound.face);
        mBound = *context;
        BindOutput();
        if (colorChanged && (mBound.surface.get() != nullptr || mBound.cubeTarget.get() != nullptr)) {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            if (auto* const surface = dynamic_cast<RenderTargetDiligent*>(mBound.surface.get())) {
                width = surface->GetContext()->width_;
                height = surface->GetContext()->height_;
            } else if (auto* const cube = dynamic_cast<CubeRenderTargetDiligent*>(mBound.cubeTarget.get())) {
                width = height = cube->GetContext()->dimension_;
            }
            mViewport = D3DVIEWPORT9{0U, 0U, width, height, 0.0f, 1.0f};
            mDrawPath->SetViewport(ViewportDesc{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F});
        }
    }

    void DeviceDiligent::GetContext(OutputContext* const outContext)
    {
        Count(37, kSlotNames[37], false);
        Device::GetContext(outContext);
    }

    // DeviceD3D9::Clear (D3D9Interfaces.cpp:3546-3580): IDirect3DDevice9::Clear(0, NULL, ...), which
    // clears the viewport rectangle of the bound targets.
    void DeviceDiligent::Clear(const bool clearTarget, const bool clearZbuffer, const bool clearStencil, const std::uint32_t color,
                               const float depth, const int stencil)
    {
        Count(38, kSlotNames[38], false);
        if (!clearTarget && !clearZbuffer && !clearStencil) {
            return;
        }
        mDrawPath->Clear(clearTarget, clearZbuffer, clearStencil, color, depth, static_cast<std::uint32_t>(stencil), mShadow);
        if (clearTarget && dynamic_cast<RenderTargetDiligent*>(mBound.surface.get()) != nullptr &&
            static_cast<RenderTargetDiligent*>(mBound.surface.get())->GetHeadIndex() == 0) {
            ++mHeadClears;
        }
    }

    // PipelineStateD3D9::ClearTextures (0x00946240) unbinds the 16 texture stages. Textures here are
    // bound per pass by the effect layer's Commit, so there is nothing bound between passes to clear.
    void DeviceDiligent::ClearTextures()
    {
        Count(39, kSlotNames[39], false);
    }

    void DeviceDiligent::SetVertexDeclaration(const boost::shared_ptr<VertexFormat> vertexFormat)
    {
        Count(40, kSlotNames[40], false);
        if (vertexFormat.get() == nullptr) {
            ThrowGalError("VertexFormatDiligent.cpp", __LINE__, "invalid vertex format");
        }
        mVertexFormat = vertexFormat;
    }

    // DeviceD3D9::SetVertexBuffer (D3D9Interfaces.cpp:3612-3645): stream source at startVertex * stride
    // with the buffer's stride, and the frequency from the buffer's type: 2 (geometry) repeats every
    // `streamFrequencyToken` instances, 3 (per-instance data) advances once per instance.
    void DeviceDiligent::SetVertexBuffer(const std::uint32_t streamSlot, const boost::shared_ptr<VertexBuffer> vertexBuffer,
                                         const int streamFrequencyToken, const int startVertex)
    {
        Count(41, kSlotNames[41], false);
        if (streamSlot >= kMaxVertexStreams) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid vertex stream");
        }
        VertexBufferContext* const context = vertexBuffer->GetContext();
        StreamBinding& stream = mStreams[streamSlot];
        stream.buffer = vertexBuffer;
        stream.stride = context->stride_;
        stream.offset = static_cast<std::uint32_t>(startVertex) * context->stride_;
        stream.type = context->type_;
        stream.frequency = static_cast<std::uint32_t>(streamFrequencyToken);
    }

    void DeviceDiligent::SetBufferIndices(const boost::shared_ptr<IndexBuffer> indexBuffer)
    {
        Count(42, kSlotNames[42], false);
        if (indexBuffer.get() == nullptr) {
            ThrowGalError("IndexBufferDiligent.cpp", __LINE__, "invalid index buffer");
        }
        mIndexBuffer = indexBuffer;
    }

    void DeviceDiligent::SetFogState(const bool enable, const Matrix* const projection, const float fogStart, const float fogEnd, const int fogColor)
    {
        Count(43, kSlotNames[43], false);
        static_cast<void>(projection);
        mShadow.SetFogState(enable, fogStart, fogEnd, static_cast<std::uint32_t>(fogColor));
    }

    void DeviceDiligent::SetWireframeState(const bool enabled)
    {
        Count(44, kSlotNames[44], false);
        mShadow.SetWireframeState(enabled);
    }

    void DeviceDiligent::SetColorWriteState(const bool writeColor, const bool writeAlpha)
    {
        Count(45, kSlotNames[45], false);
        mShadow.SetColorWriteState(writeColor, writeAlpha);
    }

    void DeviceDiligent::SubmitDraw(DrawCall& call)
    {
        // The draw slot: the stream buffers' maps (GetBuffer), the effect's constants and textures
        // (Commit) and the draw. On Vulkan none of it may end the render pass but a texture upload
        // (DiligentHost.h RenderPassCounts).
        ScopedRenderPhase phase(ScopedRenderPhase::Kind::Draw);
        call.binding = mPassBinding;
        call.vertexFormatCode = mVertexFormat.get() != nullptr ? mVertexFormat->formatCode_ : 0x17U;
        std::uint32_t instances = 1U;
        for (std::uint32_t slot = 0; slot < kMaxVertexStreams; ++slot) {
            StreamBinding& stream = mStreams[slot];
            VertexStreamBinding& out = call.streams[slot];
            auto* const buffer = dynamic_cast<VertexBufferDiligent*>(stream.buffer.get());
            if (buffer == nullptr) {
                continue;
            }
            {
                ScopedRenderPhase map(ScopedRenderPhase::Kind::Map);
                out.buffer = buffer->GetBuffer();
            }
            out.offset = stream.offset;
            out.stride = stream.stride;
            out.perInstance = stream.type == 3U;
            out.frequency = stream.frequency;
            // D3D9 instancing: the geometry stream's INDEXEDDATA frequency is the instance count
            // (D3D10's emulation reads slot 0 the same way, D3D10Interfaces.cpp:4628-4680).
            if (stream.type == 2U && stream.frequency > 1U) {
                instances = std::max(instances, stream.frequency);
            }
        }
        call.instanceCount = instances;
        if (call.indexed) {
            if (auto* const indices = dynamic_cast<IndexBufferDiligent*>(mIndexBuffer.get())) {
                ScopedRenderPhase map(ScopedRenderPhase::Kind::Map);
                call.indexBuffer = indices->GetBuffer();
                call.index32 = indices->Is32Bit();
            }
        }
        mDrawPath->Draw(call, mShadow);
    }

    // DeviceD3D9::DrawIndexedPrimitive (D3D9Interfaces.cpp:3771-3797): the topology must be set, and
    // the primitive count comes from the index count (throwing as DrawContext.cpp does when it does
    // not fit); the draw uses the indices those primitives take.
    void DeviceDiligent::DrawIndexedPrimitive(const DrawIndexedContext* const context)
    {
        Count(46, kSlotNames[46], false);
        if (context->topology_ == 0) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid topology specified");
        }
        const std::uint32_t primitives = context->GetPrimitiveCount();
        DrawCall call;
        call.indexed = true;
        call.topology = static_cast<std::uint32_t>(context->topology_);
        call.count = ElementsForPrimitives(context->topology_, primitives);
        call.firstIndex = context->startIndex_;
        call.baseVertex = context->baseVertexIndex_;
        SubmitDraw(call);
        RecordDraw(primitives, context->vertexCount_);
    }

    // DeviceD3D9::DrawPrimitive (D3D9Interfaces.cpp:3743-3762).
    void DeviceDiligent::DrawPrimitive(const DrawContext* const context)
    {
        Count(47, kSlotNames[47], false);
        if (context->topology_ == 0) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid topology specified");
        }
        const std::uint32_t primitives = context->GetPrimitiveCount();
        DrawCall call;
        call.indexed = false;
        call.topology = static_cast<std::uint32_t>(context->topology_);
        call.count = ElementsForPrimitives(context->topology_, primitives);
        call.firstIndex = context->startVertex_;
        SubmitDraw(call);
        RecordDraw(primitives, context->vertexCount_);
    }

    void DeviceDiligent::BeginTechnique()
    {
        Count(48, kSlotNames[48], false);
        mShadow.BeginTechnique();
    }

    // PipelineStateD3D9::EndTechnique (0x00946300) is empty.
    void DeviceDiligent::EndTechnique()
    {
        Count(49, kSlotNames[49], false);
    }

    // ID3DXEffect::BeginPass sets the pass's states on the device right then (D3D9Interfaces.cpp:4739),
    // so a device slot called between BeginPass and the draw (SetColorWriteState) overrides them.
    void DeviceDiligent::OnBeginPass(PassBinding& binding)
    {
        std::size_t count = 0;
        const PassStateAssignment* const states = binding.GetStates(&count);
        mShadow.ApplyPassStates(states, count);
        mPassBinding = &binding;
    }

    void DeviceDiligent::OnEndPass(PassBinding& binding)
    {
        if (mPassBinding == &binding) {
            mPassBinding = nullptr;
        }
    }

    // --- report -------------------------------------------------------------------------------------

    void DeviceDiligent::WriteReport(const char* const reason)
    {
        mReportWritten = true;
        if (mReportPath.empty()) {
            return;
        }
        if (mHost && mHost->IsCreated()) {
            const std::size_t kept = mDebugLayerMessages.size();
            mHost->DrainDebugLayer(&mDebugLayerMessages, kept < kKeptDebugLayerMessages ? kKeptDebugLayerMessages - kept : 0);
        }
        const DebugLayerCounts layer = mHost ? mHost->GetDebugLayerCounts() : DebugLayerCounts{};
        const DiligentMessageCounts messages = DiligentHost::GetDiligentMessageCounts();

        std::string json = "{\n";
        json += "  \"backend\": \"diligent:" + JsonEscape(mApi) + "\",\n";
        json += "  \"api\": \"" + std::string(GraphicsApiName(mGraphicsApi)) + "\",\n";
        json += "  \"reason\": \"" + std::string(reason) + "\",\n";
        json += "  \"adapter\": \"" + JsonEscape(mHost ? mHost->GetAdapterDescription() : std::string()) + "\",\n";
        json += "  \"swapChainFormat\": \"" + JsonEscape(mHost ? mHost->GetSwapChainFormat() : std::string()) + "\",\n";
        json += "  \"oracleDevice\": \"" + JsonEscape(mOracle ? mOracle->GetDeviceDescription() : std::string()) + "\",\n";
        json += "  \"head\": [" + std::to_string(mHost ? mHost->GetHeadWidth() : 0) + ", " + std::to_string(mHost ? mHost->GetHeadHeight() : 0) + "],\n";
        json += "  \"presents\": " + std::to_string(mPresents) + ",\n";
        json += "  \"headClears\": " + std::to_string(mHeadClears) + ",\n";
        json += "  \"halfPixel\": \"" + std::string(mHalfPixel == HalfPixelMode::Shader ? "shader" : mHalfPixel == HalfPixelMode::Viewport ? "viewport" : "none") + "\",\n";
        json += "  \"texture2D\": {\"mode\": \"" + std::string(mPortableTexture2D ? "portable" : "oracle") + "\", \"calls\": " + std::to_string(mTexture2DCalls.load()) + ", \"offRenderThread\": " + std::to_string(mTexture2DOffThread.load()) + ", \"createTextureOffRenderThread\": " + std::to_string(mCreateTextureOffThread.load()) +
                ", \"dumped\": " + std::to_string(mTextureDumps) + "},\n";
        json += "  \"validation\": " + std::string(mValidation ? "true" : "false") + ",\n";
        json += "  \"debugLayer\": {\"active\": " + std::string(mHost && mHost->IsDebugLayerActive() ? "true" : "false") +
                ", \"corruption\": " + std::to_string(layer.corruption) + ", \"error\": " + std::to_string(layer.error) +
                ", \"warning\": " + std::to_string(layer.warning) + ", \"info\": " + std::to_string(layer.info) +
                ", \"message\": " + std::to_string(layer.message) + ", \"first\": " + JsonStringList(mDebugLayerMessages) +
                ", \"selfTest\": {\"run\": " + std::string(mDebugLayerSelfTest ? "true" : "false") + ", \"errorsProvoked\": " +
                std::to_string(mSelfTestErrors) + ", \"sample\": \"" + JsonEscape(mSelfTestSample) + "\"}},\n";
        json += "  \"diligentMessages\": {\"info\": " + std::to_string(messages.info) + ", \"warning\": " + std::to_string(messages.warning) +
                ", \"error\": " + std::to_string(messages.error) + ", \"fatal\": " + std::to_string(messages.fatal) +
                ", \"first\": " + JsonStringList(DiligentHost::GetDiligentMessages()) + "},\n";
        if (mHost) {
            // OpenGL: the debug output (a debug context's GL_KHR_debug messages, counted by type).
            std::vector<std::string> glMessages;
            const GlDebugCounts gl = mHost->GetGlDebugCounts(&glMessages);
            json += "  \"glDebugOutput\": {\"active\": " + std::string(gl.active ? "true" : "false") + ", \"error\": " + std::to_string(gl.error) +
                    ", \"undefinedBehavior\": " + std::to_string(gl.undefinedBehavior) + ", \"deprecated\": " + std::to_string(gl.deprecated) +
                    ", \"portability\": " + std::to_string(gl.portability) + ", \"performance\": " + std::to_string(gl.performance) +
                    ", \"other\": " + std::to_string(gl.other) + ", \"notification\": " + std::to_string(gl.notification) +
                    ", \"first\": " + JsonStringList(glMessages) + "},\n";
            const GlShaderRouteCounts route = GetGlShaderRouteCounts();
            json += "  \"glShaderRoute\": {\"compiled\": " + std::to_string(route.compiled) + ", \"failed\": " + std::to_string(route.failed) +
                    "},\n";
            // Vulkan: the layers the loader offers, and the render-pass scopes by phase.
            json += "  \"vulkanLayers\": " + mHost->GetVulkanLayerReport() + ",\n";
            const RenderPassCounts passes = GetRenderPassCounts();
            const std::uint64_t draws = mDrawPath ? mDrawPath->GetStats().draws : 0;
            json += "  \"renderPass\": {\"hooked\": " + std::string(passes.hooked ? "true" : "false") + ", \"begins\": " +
                    std::to_string(passes.begins) + ", \"ends\": " + std::to_string(passes.ends) + ", \"endsInDraw\": " +
                    std::to_string(passes.endsInDraw) + ", \"endsInDrawByUpload\": " + std::to_string(passes.endsInDrawByUpload) +
                    ", \"endsInDrawByTargets\": " + std::to_string(passes.endsInDrawByTargets) + ", \"endsInDrawByMap\": " +
                    std::to_string(passes.endsInDrawByMap) + ", \"endsInDrawByCommit\": " + std::to_string(passes.endsInDrawByCommit) +
                    ", \"endsInDrawOther\": " + std::to_string(passes.endsInDrawOther) +
                    ", \"endsByUploadOutsideDraw\": " + std::to_string(passes.endsByUploadOutsideDraw) + ", \"draws\": " +
                    std::to_string(draws) + "},\n";
            json += "  \"contextStats\": " + mHost->GetContextStatsJson() + ",\n";
        }

        if (mRunSelfTest) {
            std::size_t passed = 0;
            std::string list;
            for (const SelfTestResult& result : mSelfTestResults) {
                passed += result.pass ? 1U : 0U;
                list += std::string(list.empty() ? "" : ", ") + "{\"name\": \"" + JsonEscape(result.name) + "\", \"pass\": " +
                        (result.pass ? "true" : "false") + ", \"detail\": \"" + JsonEscape(result.detail) + "\"}";
            }
            json += "  \"selfTest\": {\"passed\": " + std::to_string(passed) + ", \"total\": " + std::to_string(mSelfTestResults.size()) +
                    ", \"debugLayerErrors\": " + std::to_string(mSelfTestDebugLayer.error + mSelfTestDebugLayer.corruption) +
                    ", \"debugLayerWarnings\": " + std::to_string(mSelfTestDebugLayer.warning) + ", \"results\": [" + list + "]},\n";
        }
        if (mDrawPath) {
            const DrawPathStats& stats = mDrawPath->GetStats();
            json += "  \"drawPath\": {\"draws\": " + std::to_string(stats.draws) + ", \"skippedNoBinding\": " + std::to_string(stats.skippedNoBinding) +
                    ", \"skippedNoProgram\": " + std::to_string(stats.skippedNoProgram) + ", \"skippedNoPipeline\": " +
                    std::to_string(stats.skippedNoPipeline) + ", \"skippedCommit\": " + std::to_string(stats.skippedCommit) +
                    ", \"skippedNoTarget\": " + std::to_string(stats.skippedNoTarget) + ", \"clearsFull\": " + std::to_string(stats.clearsFull) +
                    ", \"clearsQuad\": " + std::to_string(stats.clearsQuad) + ", \"blits\": " + std::to_string(stats.blits) +
                    ", \"copies\": " + std::to_string(stats.copies) + ", \"psoLookups\": " + std::to_string(stats.psoLookups) +
                    ", \"psoCreated\": " + std::to_string(stats.psoCreated) + ", \"psoFailed\": " + std::to_string(stats.psoFailed) +
                    ", \"messages\": " + JsonStringList(mDrawPath->GetMessages()) + ", \"pipelines\": " +
                    JsonStringList(mDrawPath->GetPipelineLog()) + "},\n";
        }
        if (mGpu) {
            GpuUploadStats& uploads = mGpu->Stats();
            json += "  \"uploads\": {\"mapDiscard\": " + std::to_string(uploads.mapDiscard.load()) + ", \"mapNoOverwrite\": " +
                    std::to_string(uploads.mapNoOverwrite.load()) + ", \"mapFrameRestore\": " + std::to_string(uploads.mapFrameRestore.load()) +
                    ", \"mapBytes\": " + std::to_string(uploads.mapBytes.load()) + ", \"updateBuffer\": " + std::to_string(uploads.updateBuffer.load()) +
                    ", \"updateBufferInFrame\": " + std::to_string(uploads.updateBufferInFrame.load()) + ", \"updateTexture\": " +
                    std::to_string(uploads.updateTexture.load()) + ", \"updateTextureBytes\": " + std::to_string(uploads.updateTextureBytes.load()) +
                    ", \"texturesCreated\": " + std::to_string(uploads.texturesCreated.load()) + ", \"buffersCreated\": " +
                    std::to_string(uploads.buffersCreated.load()) + ", \"createdOffRenderThread\": " +
                    std::to_string(uploads.createdOffRenderThread.load()) + ", \"releasedOffRenderThread\": " +
                    std::to_string(uploads.releasedOffRenderThread.load()) + "},\n";
        }
        {
            // The shaders are SM5 HLSL from the portable front end (FxHlslEmitter), compiled in this
            // process when a pass is first drawn; constants go up with Map(DISCARD) only (PassBinding.h).
            const EffectLayerCounts effectLayer = GetEffectLayerCounts();
            json += "  \"effectLayer\": {\"shaderSource\": \"FxHlslEmitter SM5\", \"effects\": " + std::to_string(effectLayer.effects) +
                    ", \"programsCompiled\": " + std::to_string(effectLayer.programsCompiled) + ", \"shadersCompiled\": " +
                    std::to_string(effectLayer.shadersCompiled) + ", \"generationFailures\": " + std::to_string(effectLayer.generationFailures) +
                    ", \"constantUploads\": " + std::to_string(effectLayer.constantUploads) + ", \"drawConstantUploads\": " +
                    std::to_string(effectLayer.drawConstantUploads) + ", \"commits\": " + std::to_string(effectLayer.commits) +
                    ", \"nullTextureBinds\": " + std::to_string(effectLayer.nullTextureBinds) + "},\n";
        }

        const DeviceContext& context = mDeviceContext;
        json += "  \"deviceContext\": {\"deviceType\": " + std::to_string(static_cast<int>(context.mDeviceType)) + ", \"validate\": " +
                std::to_string(context.mValidate ? 1 : 0) + ", \"adapter\": " + std::to_string(context.mAdapter) + ", \"vsync\": " +
                std::to_string(context.mVSync ? 1 : 0) + ", \"hwInstancing\": " + std::to_string(context.mHWBasedInstancing ? 1 : 0) +
                ", \"float16\": " + std::to_string(context.mSupportsFloat16 ? 1 : 0) + ", \"vsProfile\": " + std::to_string(context.mVertexShaderProfile) +
                ", \"psProfile\": " + std::to_string(context.mPixelShaderProfile) + ", \"maxPrimitives\": " + std::to_string(context.mMaxPrimitiveCount) +
                ", \"maxVertexIndex\": " + std::to_string(context.mMaxVertexCount) + ", \"heads\": [";
        for (int headIndex = 0; headIndex < context.GetHeadCount(); ++headIndex) {
            const Head& head = context.GetHead(static_cast<std::uint32_t>(headIndex));
            std::vector<std::string> labels;
            for (const HeadSampleOption& option : head.mStrs) {
                labels.emplace_back(option.label.c_str());
            }
            json += std::string(headIndex == 0 ? "" : ", ") + "{\"width\": " + std::to_string(head.mWidth) + ", \"height\": " +
                    std::to_string(head.mHeight) + ", \"fullScreen\": " + std::to_string(head.mWindowed ? 1 : 0) + ", \"modes\": " +
                    std::to_string(head.adapterModes.size()) + ", \"renderTargetFormats\": " + JsonIntList(head.validFormats1) +
                    ", \"textureFormats\": " + JsonIntList(head.validFormats2) + ", \"antialiasing\": " + JsonStringList(labels) + "}";
        }
        json += "]},\n";

        json += "  \"slots\": [\n";
        for (int slot = 0; slot < kSlotCount; ++slot) {
            json += "    {\"slot\": " + std::to_string(slot) + ", \"name\": \"" + kSlotNames[slot] + "\", \"calls\": " +
                    std::to_string(mSlotCalls[slot].load()) + "}" + (slot + 1 < kSlotCount ? ",\n" : "\n");
        }
        json += "  ],\n";

        const std::vector<EffectRecord> effects = GetEffectRecords();
        json += "  \"effects\": [\n";
        for (std::size_t index = 0; index < effects.size(); ++index) {
            const EffectRecord& effect = effects[index];
            json += "    {\"path\": \"" + JsonEscape(effect.sourcePath) + "\", \"fromCache\": " + std::string(effect.fromCache ? "true" : "false") +
                    ", \"techniques\": " + std::to_string(effect.techniqueCount) + ", \"parameters\": " + std::to_string(effect.parameterCount) +
                    ", \"validTechniques\": " + JsonStringList(effect.validTechniques) + "}" + (index + 1 < effects.size() ? ",\n" : "\n");
        }
        json += "  ]\n}\n";

        std::ofstream out(mReportPath, std::ios::binary | std::ios::trunc);
        out << json;
        gpg::Logf("[gal-diligent] report (%s) written to %s", reason, mReportPath.c_str());
    }
} // namespace gpg::gal::diligent
