#include "DeviceDiligent.h"

#include <d3d9.h>
#include <d3dx9.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "D3D9Oracle.h"
#include "DiligentHost.h"
#include "EffectsDiligent.h"
#include "GalDiligent.h"
#include "ResourcesDiligent.h"
#include "gpg/core/utils/Logging.h"
#include "gpg/gal/CubeRenderTargetContext.hpp"
#include "gpg/gal/CursorContext.hpp"
#include "gpg/gal/DepthStencilTargetContext.hpp"
#include "gpg/gal/DrawContext.hpp"
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
    } // namespace

    // ---------------------------------------------------------------------------------------------
    // GalDiligent.h

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
        return new DeviceDiligent();
    }

    void SetupDevice(Device* const device, const DeviceContext* const context)
    {
        static_cast<DeviceDiligent*>(device)->Setup(context);
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
    }

    void DeviceDiligent::Count(const int slot, const char* const name, const bool noOp)
    {
        ++mSlotCalls[slot];
        if (noOp && !mSlotLogged[slot].exchange(true)) {
            gpg::Logf("[gal-diligent] slot %d %s: no-op in the spike", slot, name);
        }
    }

    void DeviceDiligent::ReadOptions()
    {
        msvc8::vector<msvc8::string> options;
        if (GetOption("/gal", 1, &options) && !options.empty()) {
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
        mValidation = !GetOption("/galnovalidation", 0, nullptr);
        mDebugLayerSelfTest = GetOption("/galdebuglayerselftest", 0, nullptr);
    }

    void DeviceDiligent::Shutdown()
    {
        mHeads.clear();
        mPipelineState.reset();
        if (mHost) {
            mHost->DrainDebugLayer(&mDebugLayerMessages, kKeptDebugLayerMessages - std::min(mDebugLayerMessages.size(), kKeptDebugLayerMessages));
            mHost->Destroy();
        }
        if (mOracle) {
            mOracle->Shutdown();
        }
    }

    // DeviceD3D9::Setup's order (D3D9Interfaces.cpp:1745-1825): validate the context, bring the API
    // up, fill the capabilities, build the heads and the pipeline state.
    void DeviceDiligent::Setup(const DeviceContext* const context)
    {
        Shutdown();
        ReadOptions();
        mCurThreadId = static_cast<int>(::GetCurrentThreadId());

        const int headCount = context->GetHeadCount();
        if (headCount == 0) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid device context specified");
        }
        if (mApi != "d3d11") {
            const std::string message = "/gal diligent:" + mApi + " is not implemented (only diligent:d3d11)";
            ThrowGalError("DeviceDiligent.cpp", __LINE__, message.c_str());
        }

        std::string error;
        mOracle = std::make_unique<D3D9Oracle>();
        if (!mOracle->Init(&error)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, error.c_str());
        }
        mOracle->FillCapabilities(*context, mDeviceContext);

        // DeviceD3D9::GetHeadParameters' device window (D3D9Interfaces.cpp:1921-1922): the frame for a
        // full-screen head 0 (mWindowed means full screen, D3D9Interfaces.cpp:1925), else the viewport.
        const Head& head = mDeviceContext.GetHead(0U);
        void* const window = head.mWindowed ? head.mHandle : head.mWindow;
        mHost = std::make_unique<DiligentHost>();
        if (!mHost->Create(window, head.mWidth, head.mHeight, mValidation, &error)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, error.c_str());
        }

        if (mDebugLayerSelfTest) {
            mSelfTestErrors = mHost->SelfTestDebugLayer(&mSelfTestSample);
            gpg::Logf(
                "[gal-diligent] debug layer self test: %u error(s) provoked on purpose, not counted: %s", mSelfTestErrors,
                mSelfTestSample.c_str()
            );
        }
        mPipelineState.reset(new PipelineStateDiligent());
        CreateHeads();

        gpg::Logf(
            "[gal-diligent] D3D11 device on \"%s\", head %ux%u (%s), swap chain %s, validation %s, D3D11 debug layer %s, "
            "oracle %s",
            mHost->GetAdapterDescription().c_str(), head.mWidth, head.mHeight, head.mWindowed ? "full screen" : "windowed",
            mHost->GetSwapChainFormat().c_str(), mValidation ? "on" : "off", mHost->IsDebugLayerActive() ? "active" : "absent",
            mOracle->GetDeviceDescription().c_str()
        );
        if (headCount > 1) {
            gpg::Warnf("[gal-diligent] %d heads requested; the spike presents head 0 only", headCount);
        }
    }

    // DeviceD3D9::CreateHeads (D3D9Interfaces.cpp:1960-2013): per head a back-buffer target sized
    // like the head and a D24S8 depth target (token 3); the back buffer starts cleared to black.
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
            mHeads[headIndex].surface.reset(new RenderTargetDiligent(surfaceContext, headIndex == 0 ? 0 : -1));
            const DepthStencilTargetContext depthContext(width, height, 3U, false);
            mHeads[headIndex].depthStencil.reset(new DepthStencilTargetDiligent(depthContext));
        }
        mViewport = D3DVIEWPORT9{0U, 0U, mHost->GetHeadWidth(), mHost->GetHeadHeight(), 0.0f, 1.0f};
        const float black[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        mHost->ClearHead(black);
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
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid head index");
        }
        return &mHeads[headIndex];
    }

    const OutputContext* DeviceDiligent::GetHeadOutputContext(const unsigned int headIndex) const
    {
        const_cast<DeviceDiligent*>(this)->Count(7, kSlotNames[7], false);
        if (headIndex >= mHeads.size()) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "invalid head index");
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
        return CreateTextureFromContext(*mOracle, *context);
    }

    boost::shared_ptr<RenderTarget> DeviceDiligent::CreateRenderTarget(const RenderTargetContext* const context)
    {
        Count(11, kSlotNames[11], false);
        return boost::shared_ptr<RenderTarget>(new RenderTargetDiligent(*context, -1));
    }

    boost::shared_ptr<CubeRenderTarget> DeviceDiligent::CreateCubeRenderTarget(const CubeRenderTargetContext* const context)
    {
        Count(12, kSlotNames[12], false);
        return boost::shared_ptr<CubeRenderTarget>(new CubeRenderTargetDiligent(*context));
    }

    boost::shared_ptr<DepthStencilTarget> DeviceDiligent::CreateDepthStencilTarget(const DepthStencilTargetContext* const context)
    {
        Count(13, kSlotNames[13], false);
        return boost::shared_ptr<DepthStencilTarget>(new DepthStencilTargetDiligent(*context));
    }

    boost::shared_ptr<VertexFormat> DeviceDiligent::CreateVertexFormat(const std::uint32_t formatCode)
    {
        Count(14, kSlotNames[14], false);
        return boost::shared_ptr<VertexFormat>(new VertexFormatDiligent(formatCode));
    }

    boost::shared_ptr<VertexBuffer> DeviceDiligent::CreateVertexBuffer(const VertexBufferContext* const context)
    {
        Count(15, kSlotNames[15], false);
        return boost::shared_ptr<VertexBuffer>(new VertexBufferDiligent(*context));
    }

    boost::shared_ptr<IndexBuffer> DeviceDiligent::CreateIndexBuffer(const IndexBufferContext* const context)
    {
        Count(16, kSlotNames[16], false);
        if (context->format_ == 0U) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "undefined index buffer format"); // as DeviceD3D9.cpp:569
        }
        return boost::shared_ptr<IndexBuffer>(new IndexBufferDiligent(*context));
    }

    // --- slots 17-24: transfers, saving, decoding ---------------------------------------------------

    // Real for the head (the only render target on the GPU in this step): the frame harness reads
    // frames with GetHeadOutputContext -> GetRenderTargetData into a system-memory A8R8G8B8 texture
    // -> Lock (port/graphics/capture/GalCapture.cpp). As DeviceD3D9::GetRenderTargetData, the
    // destination must be a 2D texture of the target's size; its level 0 receives the pixels in
    // A8R8G8B8 memory order (B, G, R, A), so a capture reads the same bytes it would on D3D9.
    // Offscreen targets are placeholders, so they stay a logged no-op.
    void DeviceDiligent::GetRenderTargetData(const boost::shared_ptr<RenderTarget>& source, const boost::shared_ptr<Texture>& destination)
    {
        const auto* const target = static_cast<const RenderTargetDiligent*>(source.get());
        auto* const texture = static_cast<TextureDiligent*>(destination.get());
        if (target == nullptr || texture == nullptr || target->GetHeadIndex() != 0) {
            Count(17, kSlotNames[17], true);
            return;
        }
        Count(17, kSlotNames[17], false);

        IDirect3DTexture9* scratch = nullptr;
        if (texture->GetScratch() == nullptr ||
            FAILED(texture->GetScratch()->QueryInterface(__uuidof(IDirect3DTexture9), reinterpret_cast<void**>(&scratch)))) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "GetRenderTargetData: destination is not a 2D texture");
        }
        D3DSURFACE_DESC level{};
        const HRESULT descResult = scratch->GetLevelDesc(0, &level);
        if (FAILED(descResult) || (level.Format != D3DFMT_A8R8G8B8 && level.Format != D3DFMT_X8R8G8B8)) {
            scratch->Release();
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "GetRenderTargetData: destination is not A8R8G8B8");
        }

        std::vector<std::uint8_t> rgba;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (!mHost->ReadHead(&rgba, &width, &height) || width != level.Width || height != level.Height) {
            scratch->Release();
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "GetRenderTargetData: head readback failed or size mismatch");
        }

        D3DLOCKED_RECT locked{};
        const HRESULT lockResult = scratch->LockRect(0, &locked, nullptr, 0);
        if (FAILED(lockResult)) {
            scratch->Release();
            ThrowGalError("DeviceDiligent.cpp", __LINE__, "GetRenderTargetData: cannot lock the destination");
        }
        for (std::uint32_t row = 0; row < height; ++row) {
            const std::uint8_t* const from = rgba.data() + static_cast<std::size_t>(row) * width * 4u;
            std::uint8_t* const to = static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(row) * static_cast<std::size_t>(locked.Pitch);
            for (std::uint32_t x = 0; x < width; ++x) {
                to[x * 4u + 0u] = from[x * 4u + 2u]; // B
                to[x * 4u + 1u] = from[x * 4u + 1u]; // G
                to[x * 4u + 2u] = from[x * 4u + 0u]; // R
                to[x * 4u + 3u] = from[x * 4u + 3u]; // A
            }
        }
        scratch->UnlockRect(0);
        scratch->Release();
    }

    void DeviceDiligent::StretchRect(
        const boost::shared_ptr<RenderTarget>& source,
        const boost::shared_ptr<RenderTarget>& destination,
        const RECT* const sourceRect,
        const RECT* const destinationRect
    )
    {
        static_cast<void>(source);
        static_cast<void>(destination);
        static_cast<void>(sourceRect);
        static_cast<void>(destinationRect);
        Count(18, kSlotNames[18], true);
    }

    void DeviceDiligent::UpdateSurface(
        const boost::shared_ptr<Texture>& source,
        const boost::shared_ptr<Texture>& destination,
        const RECT* const sourceRect,
        const RECT* const destinationRect
    )
    {
        static_cast<void>(source);
        static_cast<void>(destination);
        static_cast<void>(sourceRect);
        static_cast<void>(destinationRect);
        Count(19, kSlotNames[19], true);
    }

    void DeviceDiligent::SaveCubeRenderTarget(const boost::shared_ptr<CubeRenderTarget>& cubeTarget, const msvc8::string& filePath)
    {
        static_cast<void>(cubeTarget);
        static_cast<void>(filePath);
        Count(20, kSlotNames[20], true);
    }

    void DeviceDiligent::SaveRenderTarget(const boost::shared_ptr<RenderTarget>& renderTarget, const msvc8::string& filePath, const int fileFormat)
    {
        static_cast<void>(renderTarget);
        static_cast<void>(filePath);
        static_cast<void>(fileFormat);
        Count(21, kSlotNames[21], true);
    }

    // DeviceD3D9::SaveTexture (D3D9Interfaces.cpp:3073-3127) on the scratch copy.
    void DeviceDiligent::SaveTexture(
        const boost::shared_ptr<Texture>& texture,
        const msvc8::string& filePath,
        const int fileFormat,
        gpg::MemBuffer<char>* const outBuffer
    )
    {
        Count(22, kSlotNames[22], false);
        auto* const textureDiligent = static_cast<TextureDiligent*>(texture.get());
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

    void DeviceDiligent::GetTexture2D(
        const void* const sourceData,
        const std::uint32_t sourceBytes,
        gpg::MemBuffer<char>* const outTextureData,
        std::uint32_t* const outWidth,
        int* const outHeight
    )
    {
        Count(23, kSlotNames[23], false);
        mOracle->GetTexture2D(sourceData, sourceBytes, outTextureData, outWidth, outHeight);
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
    // which is how a resize arrives: here the capabilities are refilled, the swap chain resized and
    // the head targets rebuilt.
    void DeviceDiligent::Reset(DeviceContext* const context)
    {
        Count(25, kSlotNames[25], false);
        DeviceContext requested = *context;
        mOracle->FillCapabilities(requested, mDeviceContext);
        const Head& head = mDeviceContext.GetHead(0U);
        std::string error;
        if (!mHost->Resize(head.mWidth, head.mHeight, &error)) {
            ThrowGalError("DeviceDiligent.cpp", __LINE__, error.c_str());
        }
        CreateHeads();
        gpg::Logf("[gal-diligent] Reset: head %ux%u", head.mWidth, head.mHeight);
    }

    void DeviceDiligent::Reset()
    {
        Count(26, kSlotNames[26], false);
        DeviceContext current = mDeviceContext;
        Reset(&current);
    }

    // 0: never lost. D3D11 has no device loss in the D3D9 sense, and Diligent reports a removed
    // device through its own errors.
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
        ++mPresents;
        const std::size_t kept = mDebugLayerMessages.size();
        mHost->DrainDebugLayer(&mDebugLayerMessages, kept < kKeptDebugLayerMessages ? kKeptDebugLayerMessages - kept : 0);
        for (std::size_t index = kept; index < mDebugLayerMessages.size(); ++index) {
            gpg::Warnf("[gal-diligent] D3D11 debug layer: %s", mDebugLayerMessages[index].c_str());
        }
        if (mPresents == 1 || mPresents % 300 == 0) {
            const DebugLayerCounts counts = mHost->GetDebugLayerCounts();
            gpg::Logf(
                "[gal-diligent] present %llu: head clears %llu, draws %llu+%llu (no-op), debug layer errors %llu warnings %llu",
                static_cast<unsigned long long>(mPresents), static_cast<unsigned long long>(mHeadClears),
                static_cast<unsigned long long>(mSlotCalls[46].load()), static_cast<unsigned long long>(mSlotCalls[47].load()),
                static_cast<unsigned long long>(counts.error + counts.corruption), static_cast<unsigned long long>(counts.warning)
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

    // The cursor is the Win32 one (CScApp sets d3d_WindowsCursor for this backend, as for /D3D10,
    // CScApp.cpp:1182-1183), so the device-cursor slots have nothing to do.
    void DeviceDiligent::SetCursor(const CursorContext* const context)
    {
        static_cast<void>(context);
        Count(31, kSlotNames[31], true);
    }

    void DeviceDiligent::InitCursor()
    {
        Count(32, kSlotNames[32], false); // empty in both shipped backends (0x008E8220)
    }

    // IDirect3DDevice9::ShowCursor's contract: returns whether the cursor was visible before.
    int DeviceDiligent::ShowCursor(const bool show)
    {
        Count(33, kSlotNames[33], false);
        const bool previous = mCursorShown;
        mCursorShown = show;
        return previous ? 1 : 0;
    }

    // --- slots 34-49: state and drawing -------------------------------------------------------------

    void DeviceDiligent::SetViewport(const D3DVIEWPORT9* const viewport)
    {
        Count(34, kSlotNames[34], false);
        mViewport = *viewport;
    }

    void DeviceDiligent::GetViewport(D3DVIEWPORT9* const outViewport)
    {
        Count(35, kSlotNames[35], false);
        *outViewport = mViewport;
    }

    // Slot 36 binds the output context (m6u-GAL.txt section 1): the base copy, then the binding. Only
    // the head target exists on the GPU, so "bound" means "the head is the colour target".
    void DeviceDiligent::ClearTarget(const OutputContext* const context)
    {
        Count(36, kSlotNames[36], false);
        Device::ClearTarget(context);
        const auto* const surface = static_cast<const RenderTargetDiligent*>(context->surface.get());
        mHeadBound = surface != nullptr && surface->GetHeadIndex() == 0;
    }

    void DeviceDiligent::GetContext(OutputContext* const outContext)
    {
        Count(37, kSlotNames[37], false);
        Device::GetContext(outContext);
    }

    // D3D9 clears the viewport rectangle (IDirect3DDevice9::Clear with no rects); the spike clears
    // the whole head, which is the same while the viewport covers it (the main menu's case).
    void DeviceDiligent::Clear(
        const bool clearTarget,
        const bool clearZbuffer,
        const bool clearStencil,
        const std::uint32_t color,
        const float depth,
        const int stencil
    )
    {
        Count(38, kSlotNames[38], false);
        static_cast<void>(clearZbuffer);
        static_cast<void>(clearStencil);
        static_cast<void>(depth);
        static_cast<void>(stencil);
        if (clearTarget && mHeadBound) {
            // D3DCOLOR is A8R8G8B8.
            const float rgba[4] = {
                static_cast<float>((color >> 16U) & 0xFFU) / 255.0f,
                static_cast<float>((color >> 8U) & 0xFFU) / 255.0f,
                static_cast<float>(color & 0xFFU) / 255.0f,
                static_cast<float>((color >> 24U) & 0xFFU) / 255.0f,
            };
            mHost->ClearHead(rgba);
            ++mHeadClears;
        }
    }

    void DeviceDiligent::ClearTextures()
    {
        Count(39, kSlotNames[39], true);
    }

    void DeviceDiligent::SetVertexDeclaration(const boost::shared_ptr<VertexFormat> vertexFormat)
    {
        static_cast<void>(vertexFormat);
        Count(40, kSlotNames[40], true);
    }

    void DeviceDiligent::SetVertexBuffer(
        const std::uint32_t streamSlot,
        const boost::shared_ptr<VertexBuffer> vertexBuffer,
        const int streamFrequencyToken,
        const int startVertex
    )
    {
        static_cast<void>(streamSlot);
        static_cast<void>(vertexBuffer);
        static_cast<void>(streamFrequencyToken);
        static_cast<void>(startVertex);
        Count(41, kSlotNames[41], true);
    }

    void DeviceDiligent::SetBufferIndices(const boost::shared_ptr<IndexBuffer> indexBuffer)
    {
        static_cast<void>(indexBuffer);
        Count(42, kSlotNames[42], true);
    }

    void DeviceDiligent::SetFogState(const bool enable, const Matrix* const projection, const float fogStart, const float fogEnd, const int fogColor)
    {
        static_cast<void>(enable);
        static_cast<void>(projection);
        static_cast<void>(fogStart);
        static_cast<void>(fogEnd);
        static_cast<void>(fogColor);
        Count(43, kSlotNames[43], true);
    }

    void DeviceDiligent::SetWireframeState(const bool enabled)
    {
        static_cast<void>(enabled);
        Count(44, kSlotNames[44], true);
    }

    void DeviceDiligent::SetColorWriteState(const bool writeColor, const bool writeAlpha)
    {
        static_cast<void>(writeColor);
        static_cast<void>(writeAlpha);
        Count(45, kSlotNames[45], true);
    }

    void DeviceDiligent::DrawIndexedPrimitive(const DrawIndexedContext* const context)
    {
        static_cast<void>(context);
        Count(46, kSlotNames[46], true);
    }

    void DeviceDiligent::DrawPrimitive(const DrawContext* const context)
    {
        static_cast<void>(context);
        Count(47, kSlotNames[47], true);
    }

    void DeviceDiligent::BeginTechnique()
    {
        Count(48, kSlotNames[48], false);
    }

    void DeviceDiligent::EndTechnique()
    {
        Count(49, kSlotNames[49], false);
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
        json += "  \"reason\": \"" + std::string(reason) + "\",\n";
        json += "  \"adapter\": \"" + JsonEscape(mHost ? mHost->GetAdapterDescription() : std::string()) + "\",\n";
        json += "  \"swapChainFormat\": \"" + JsonEscape(mHost ? mHost->GetSwapChainFormat() : std::string()) + "\",\n";
        json += "  \"oracleDevice\": \"" + JsonEscape(mOracle ? mOracle->GetDeviceDescription() : std::string()) + "\",\n";
        json += "  \"head\": [" + std::to_string(mHost ? mHost->GetHeadWidth() : 0) + ", " +
                std::to_string(mHost ? mHost->GetHeadHeight() : 0) + "],\n";
        json += "  \"presents\": " + std::to_string(mPresents) + ",\n";
        json += "  \"headClears\": " + std::to_string(mHeadClears) + ",\n";
        json += "  \"validation\": " + std::string(mValidation ? "true" : "false") + ",\n";
        json += "  \"debugLayer\": {\"active\": " + std::string(mHost && mHost->IsDebugLayerActive() ? "true" : "false") +
                ", \"corruption\": " + std::to_string(layer.corruption) + ", \"error\": " + std::to_string(layer.error) +
                ", \"warning\": " + std::to_string(layer.warning) + ", \"info\": " + std::to_string(layer.info) +
                ", \"message\": " + std::to_string(layer.message) + ", \"first\": " + JsonStringList(mDebugLayerMessages) +
                ", \"selfTest\": {\"run\": " + std::string(mDebugLayerSelfTest ? "true" : "false") + ", \"errorsProvoked\": " +
                std::to_string(mSelfTestErrors) + ", \"sample\": \"" + JsonEscape(mSelfTestSample) + "\"}},\n";
        json += "  \"diligentMessages\": {\"info\": " + std::to_string(messages.info) + ", \"warning\": " +
                std::to_string(messages.warning) + ", \"error\": " + std::to_string(messages.error) + ", \"fatal\": " +
                std::to_string(messages.fatal) + ", \"first\": " + JsonStringList(DiligentHost::GetDiligentMessages()) + "},\n";

        const DeviceContext& context = mDeviceContext;
        json += "  \"deviceContext\": {\"deviceType\": " + std::to_string(static_cast<int>(context.mDeviceType)) +
                ", \"validate\": " + std::to_string(context.mValidate ? 1 : 0) + ", \"adapter\": " +
                std::to_string(context.mAdapter) + ", \"vsync\": " + std::to_string(context.mVSync ? 1 : 0) +
                ", \"hwInstancing\": " + std::to_string(context.mHWBasedInstancing ? 1 : 0) + ", \"float16\": " +
                std::to_string(context.mSupportsFloat16 ? 1 : 0) + ", \"vsProfile\": " + std::to_string(context.mVertexShaderProfile) +
                ", \"psProfile\": " + std::to_string(context.mPixelShaderProfile) + ", \"maxPrimitives\": " +
                std::to_string(context.mMaxPrimitiveCount) + ", \"maxVertexIndex\": " + std::to_string(context.mMaxVertexCount) +
                ", \"heads\": [";
        for (int headIndex = 0; headIndex < context.GetHeadCount(); ++headIndex) {
            const Head& head = context.GetHead(static_cast<std::uint32_t>(headIndex));
            std::vector<std::string> labels;
            for (const HeadSampleOption& option : head.mStrs) {
                labels.emplace_back(option.label.c_str());
            }
            json += std::string(headIndex == 0 ? "" : ", ") + "{\"width\": " + std::to_string(head.mWidth) + ", \"height\": " +
                    std::to_string(head.mHeight) + ", \"fullScreen\": " + std::to_string(head.mWindowed ? 1 : 0) +
                    ", \"modes\": " + std::to_string(head.adapterModes.size()) + ", \"renderTargetFormats\": " +
                    JsonIntList(head.validFormats1) + ", \"textureFormats\": " + JsonIntList(head.validFormats2) +
                    ", \"antialiasing\": " + JsonStringList(labels) + "}";
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
            json += "    {\"path\": \"" + JsonEscape(effect.sourcePath) + "\", \"fromCache\": " +
                    std::string(effect.fromCache ? "true" : "false") + ", \"techniques\": " +
                    std::to_string(effect.techniqueCount) + ", \"parameters\": " + std::to_string(effect.parameterCount) +
                    ", \"validTechniques\": " + JsonStringList(effect.validTechniques) + "}" +
                    (index + 1 < effects.size() ? ",\n" : "\n");
        }
        json += "  ]\n}\n";

        std::ofstream out(mReportPath, std::ios::binary | std::ios::trunc);
        out << json;
        gpg::Logf("[gal-diligent] report (%s) written to %s", reason, mReportPath.c_str());
    }
} // namespace gpg::gal::diligent
