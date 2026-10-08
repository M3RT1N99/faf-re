#pragma once

// gpg::gal::Device on Diligent (docs/port/renderer.md; M6a step 1 made the device, M6b step 3 the draw
// path).
//
// All 50 Device slots (Device.hpp:115-588; vtable 0x00D42224) are overridden:
//   - the device, swap chain, head target and Present-by-draw (DiligentHost);
//   - resources with GPU objects behind D3D9's CPU contract (ResourcesDiligent.h): textures from the
//     D3DX texel oracle with their full mip chains, render/cube/depth targets, vertex and index
//     buffers mapped the way D3D9 locks them;
//   - the D3D9 render-state shadow and the draw path (PipelineDiligent.h): ClearTarget binds the output,
//     Clear honours the viewport, SetViewport, the stream/declaration/index slots, the technique-begin
//     defaults and the fog/wireframe/colour-write slots write the shadow, and the draws build hashed PSOs
//     from it and the effect layer's pass (PassBinding.h, PassSink);
//   - readbacks: GetRenderTargetData of any render target in D3D9's byte order, StretchRect,
//     UpdateSurface and the Save slots;
//   - oracle values for capabilities, adapter modes, effects and GetTexture2D (D3D9Oracle.h), with a
//     portable GetTexture2D (Texture2DPortable.h) selectable for the Android path;
//   - the Win32 cursor built from the cursor texture, as the D3D10 backend's CursorD3D10 does.
//
// Port-only command line (read in Setup, CScApp parses /gal):
//   /gal diligent:<api>        select this backend on d3d11, vk (Vulkan) or gl (OpenGL; M6c step 6)
//   /galreport <file.json>     write the report at exit or after /galexitframes
//   /galexitframes <N>         after N presents, write the report and ask the app to exit
//   /galreportframe <N>        write the report after N presents and keep running (for runs under the
//                              frame harness, port/graphics/capture, which ends the process itself)
//   /galdumpfx <dir>           write every effect source buffer and its macros to <dir>
//   /galdumptex <dir>          write every GetTexture2D and CreateTexture source to <dir> (game data:
//                              scratch only), for the GetTexture2D unit test
//   /galtex2d oracle|portable  GetTexture2D through D3DX (default) or the portable decoder/encoder
//   /galhalfpixel shader|viewport|none   how D3D9's pixel centres are reproduced (PipelineDiligent.h)
//   /galnovalidation           no Diligent validation, no D3D11 debug layer, no Vulkan validation layer,
//                              no GL debug context
//   /galdebuglayerselftest     at setup, provoke one D3D11 debug-layer error (GL: one GL debug-output
//                              error) on purpose (not counted) to show in the report that it is live
//   /galselftest               at setup, run the known-answer checks of RunSelfTest (report "selfTest")

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "DiligentHost.h"
#include "PassBinding.h"
#include "PipelineDiligent.h"
#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/OutputContext.hpp"
#include "platform/Platform.h"

namespace gpg::gal::diligent
{
    class D3D9Oracle;
    class DiligentHost;
    class GpuShared;

    class DeviceDiligent final : public Device, public PassSink
    {
    public:
        static constexpr int kSlotCount = 50;

        DeviceDiligent();
        ~DeviceDiligent() override; // slot 0

        void Setup(const DeviceContext* context);

        /** The API to use whatever `/gal` says (DeviceFactory.h CreateDeviceForApi); before Setup. */
        void SetApiOverride(const char* api);

        void* GetLog() override;                                                     // 1
        DeviceContext* GetDeviceContext() override;                                  // 2
        int GetCurThreadId() override;                                               // 3
        void Func1() const override;                                                 // 4
        void GetModesForAdapter(msvc8::vector<HeadAdapterMode>& outModes, int adapterIndex) override; // 5
        OutputContext* GetHeadOutputContext(unsigned int headIndex) override;        // 6/7
        const OutputContext* GetHeadOutputContext(unsigned int headIndex) const override;
        boost::shared_ptr<PipelineState> GetPipelineState() override;                // 8
        boost::shared_ptr<Effect> CreateEffect(const EffectContext& context) override; // 9
        boost::shared_ptr<Texture> CreateTexture(const TextureContext* context) override; // 10
        boost::shared_ptr<RenderTarget> CreateRenderTarget(const RenderTargetContext* context) override; // 11
        boost::shared_ptr<CubeRenderTarget> CreateCubeRenderTarget(const CubeRenderTargetContext* context) override; // 12
        boost::shared_ptr<DepthStencilTarget> CreateDepthStencilTarget(const DepthStencilTargetContext* context) override; // 13
        boost::shared_ptr<VertexFormat> CreateVertexFormat(std::uint32_t formatCode) override; // 14
        boost::shared_ptr<VertexBuffer> CreateVertexBuffer(const VertexBufferContext* context) override; // 15
        boost::shared_ptr<IndexBuffer> CreateIndexBuffer(const IndexBufferContext* context) override; // 16
        void GetRenderTargetData(
            const boost::shared_ptr<RenderTarget>& source,
            const boost::shared_ptr<Texture>& destination
        ) override; // 17
        void StretchRect(
            const boost::shared_ptr<RenderTarget>& source,
            const boost::shared_ptr<RenderTarget>& destination,
            const RECT* sourceRect,
            const RECT* destinationRect
        ) override; // 18
        void UpdateSurface(
            const boost::shared_ptr<Texture>& source,
            const boost::shared_ptr<Texture>& destination,
            const RECT* sourceRect,
            const RECT* destinationRect
        ) override; // 19
        void SaveCubeRenderTarget(const boost::shared_ptr<CubeRenderTarget>& cubeTarget, const msvc8::string& filePath) override; // 20
        void SaveRenderTarget(const boost::shared_ptr<RenderTarget>& renderTarget, const msvc8::string& filePath, int fileFormat) override; // 21
        void SaveTexture(
            const boost::shared_ptr<Texture>& texture,
            const msvc8::string& filePath,
            int fileFormat,
            gpg::MemBuffer<char>* outBuffer
        ) override; // 22
        void GetTexture2D(
            const void* sourceData,
            std::uint32_t sourceBytes,
            gpg::MemBuffer<char>* outTextureData,
            std::uint32_t* outWidth,
            int* outHeight
        ) override; // 23
        boost::weak_ptr<void>* Func7(boost::weak_ptr<void>* outWeakHandle, boost::shared_ptr<void> temporarySharedHandle) override; // 24
        void Reset(DeviceContext* context) override;   // 25
        void Reset() override;                         // 26
        int TestCooperativeLevel() override;           // 27
        void BeginScene() override;                    // 28
        void EndScene() override;                      // 29
        void Present() override;                       // 30
        void SetCursor(const CursorContext* context) override; // 31
        void InitCursor() override;                    // 32
        int ShowCursor(bool show) override;            // 33
        void SetViewport(const D3DVIEWPORT9* viewport) override; // 34
        void GetViewport(D3DVIEWPORT9* outViewport) override;    // 35
        void ClearTarget(const OutputContext* context) override; // 36
        void GetContext(OutputContext* outContext) override;     // 37
        void Clear(bool clearTarget, bool clearZbuffer, bool clearStencil, std::uint32_t color, float depth, int stencil) override; // 38
        void ClearTextures() override;                 // 39
        void SetVertexDeclaration(boost::shared_ptr<VertexFormat> vertexFormat) override; // 40
        void SetVertexBuffer(std::uint32_t streamSlot, boost::shared_ptr<VertexBuffer> vertexBuffer, int streamFrequencyToken, int startVertex) override; // 41
        void SetBufferIndices(boost::shared_ptr<IndexBuffer> indexBuffer) override; // 42
        void SetFogState(bool enable, const Matrix* projection, float fogStart, float fogEnd, int fogColor) override; // 43
        void SetWireframeState(bool enabled) override; // 44
        void SetColorWriteState(bool writeColor, bool writeAlpha) override; // 45
        void DrawIndexedPrimitive(const DrawIndexedContext* context) override; // 46
        void DrawPrimitive(const DrawContext* context) override; // 47
        void BeginTechnique() override;                // 48
        void EndTechnique() override;                  // 49

        // PassSink (PassBinding.h): the effect layer's BeginPass/EndPass.
        void OnBeginPass(PassBinding& binding) override;
        void OnEndPass(PassBinding& binding) override;

        /** Writes the JSON report (/galreport) now. */
        void WriteReport(const char* reason);

        /**
         * /galselftest: known-answer checks of the paths the main menu does not reach (partial-viewport
         * Clear, StretchRect copy/scale/sub-rectangle, offscreen GetRenderTargetData, texture Lock/Unlock
         * uploads with and without format conversion, dynamic-buffer DISCARD/NOOVERWRITE/plain locks and
         * the first-map-of-a-frame rewrite, static-buffer updates, cube faces, shader-readable depth,
         * resize). Runs once at the end of Setup, through the Device slots themselves; the results go to
         * the log and the report. SelfTestDiligent.cpp.
         */
        void RunSelfTest();

    private:
        struct StreamBinding
        {
            boost::shared_ptr<VertexBuffer> buffer;
            std::uint32_t offset = 0;
            std::uint32_t stride = 0;
            std::uint32_t type = 0;      // VertexBufferContext::type_
            std::uint32_t frequency = 1; // the stream-frequency token SetVertexBuffer got
        };

        void Count(int slot, const char* name, bool noOp);
        void Shutdown();
        void ReadOptions();
        void CreateHeads();
        void BindOutput();
        void SubmitDraw(DrawCall& call);
        void CopyRenderTargetToTexture(const boost::shared_ptr<RenderTarget>& source, const boost::shared_ptr<Texture>& destination,
                                       const char* slot);
        /** Writes <dir>\<kind>_<n>_<fnv>.bin and returns the path without ".bin". */
        std::string DumpTextureSource(const char* kind, const void* data, std::uint32_t bytes);

        DeviceContext mDeviceContext;
        std::unique_ptr<D3D9Oracle> mOracle;
        std::unique_ptr<DiligentHost> mHost;
        std::shared_ptr<GpuShared> mGpu;
        std::unique_ptr<DrawPath> mDrawPath;
        D3D9StateShadow mShadow;
        std::vector<OutputContext> mHeads;
        boost::shared_ptr<PipelineState> mPipelineState;
        msvc8::vector<msvc8::string> mLog;
        int mCurThreadId = 0;
        D3DVIEWPORT9 mViewport{};
        bool mCursorShown = true;
        void* mCursorIcon = nullptr;     // HICON
        void* mPreviousCursor = nullptr; // HCURSOR
        bool mInScene = false;

        // What ClearTarget bound (kept alive while bound) and the input assembler state.
        OutputContext mBound;
        boost::shared_ptr<VertexFormat> mVertexFormat;
        StreamBinding mStreams[kMaxVertexStreams];
        boost::shared_ptr<IndexBuffer> mIndexBuffer;
        PassBinding* mPassBinding = nullptr;

        std::atomic<std::uint64_t> mSlotCalls[kSlotCount]{};
        std::atomic<bool> mSlotLogged[kSlotCount]{};
        std::uint64_t mPresents = 0;
        std::uint64_t mHeadClears = 0;
        std::atomic<std::uint64_t> mTexture2DCalls{0};
        std::atomic<std::uint64_t> mTexture2DOffThread{0}; // calls from another thread than Setup's (the prefetch thread)
        std::atomic<std::uint64_t> mCreateTextureOffThread{0};
        std::uint64_t mTextureDumps = 0;
        std::vector<std::string> mDebugLayerMessages;
        bool mReportWritten = false;
        bool mExitRequested = false;

        // options
        std::string mApi = "d3d11";
        std::string mApiOverride;
        GraphicsApi mGraphicsApi = GraphicsApi::D3D11;
        std::string mReportPath;
        std::string mDumpFxDir;
        std::string mDumpTexDir;
        bool mPortableTexture2D = false;
        HalfPixelMode mHalfPixel = HalfPixelMode::Shader;
        std::uint64_t mExitFrames = 0;
        std::uint64_t mReportFrame = 0;
        bool mValidation = true;
        bool mDebugLayerSelfTest = false;
        std::uint32_t mSelfTestErrors = 0;
        std::string mSelfTestSample;
        bool mRunSelfTest = false;
        struct SelfTestResult
        {
            std::string name;
            bool pass = false;
            std::string detail;
        };
        std::vector<SelfTestResult> mSelfTestResults;
        DebugLayerCounts mSelfTestDebugLayer{}; // what the self test itself made the debug layer report
    };
} // namespace gpg::gal::diligent
