#pragma once

// gpg::gal::Device on Diligent (M6 step 1, the integration spike; docs/port/renderer.md).
//
// All 50 Device slots (Device.hpp:115-588; vtable 0x00D42224) are overridden. In this step:
//   - real: device and swap chain creation on the engine window (Setup), the head render target,
//     ClearTarget/Clear on it, and Present, which draws the head into the swap chain;
//   - oracle (D3D9Oracle.h): the DeviceContext capability lanes, adapter modes, effects
//     (metadata from D3DX reflection), textures (D3DX scratch textures) and GetTexture2D;
//   - placeholders (ResourcesDiligent.h): render, cube and depth targets, vertex and index buffers,
//     vertex formats, the pipeline state;
//   - real readback of the head: GetRenderTargetData copies head 0 through a staging texture
//     into a system-memory texture, so the frame harness (port/graphics/capture) can read frames;
//   - logged no-ops: every draw, state, other readback, save and cursor slot. The first call of each is
//     logged as "[gal-diligent] slot N <name>: no-op in the spike" so the census of what the main
//     menu uses is in the log, and every slot's call count is in the report.
//
// Port-only command line (read in Setup, CScApp parses /gal):
//   /gal diligent:d3d11        select this backend (only d3d11 so far)
//   /galreport <file.json>     write the report at exit or after /galexitframes
//   /galexitframes <N>         after N presents, write the report and ask the app to exit
//   /galreportframe <N>        write the report after N presents and keep running (for runs under
//                              the frame harness, port/graphics/capture, which ends the process itself)
//   /galdumpfx <dir>           write every effect source buffer and its macros to <dir>
//   /galnovalidation           no Diligent validation and no D3D11 debug layer
//   /galdebuglayerselftest     at setup, provoke one D3D11 debug-layer error on purpose (not
//                              counted) to show in the report that the layer is live

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/OutputContext.hpp"
#include "platform/Platform.h"

namespace gpg::gal::diligent
{
    class D3D9Oracle;
    class DiligentHost;

    class DeviceDiligent final : public Device
    {
    public:
        static constexpr int kSlotCount = 50;

        DeviceDiligent();
        ~DeviceDiligent() override; // slot 0

        void Setup(const DeviceContext* context);

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

        /** Writes the JSON report (/galreport) now. */
        void WriteReport(const char* reason);

    private:
        void Count(int slot, const char* name, bool noOp);
        void Shutdown();
        void ReadOptions();
        void CreateHeads();

        DeviceContext mDeviceContext;
        std::unique_ptr<D3D9Oracle> mOracle;
        std::unique_ptr<DiligentHost> mHost;
        std::vector<OutputContext> mHeads;
        boost::shared_ptr<PipelineState> mPipelineState;
        msvc8::vector<msvc8::string> mLog;
        int mCurThreadId = 0;
        D3DVIEWPORT9 mViewport{};
        bool mHeadBound = false;
        bool mCursorShown = true;
        bool mInScene = false;
        std::atomic<std::uint64_t> mSlotCalls[kSlotCount]{};
        std::atomic<bool> mSlotLogged[kSlotCount]{};
        std::uint64_t mPresents = 0;
        std::uint64_t mHeadClears = 0;
        std::vector<std::string> mDebugLayerMessages;
        bool mReportWritten = false;
        bool mExitRequested = false;

        // options
        std::string mApi = "d3d11";
        std::string mReportPath;
        std::string mDumpFxDir;
        std::uint64_t mExitFrames = 0;
        std::uint64_t mReportFrame = 0;
        bool mValidation = true;
        bool mDebugLayerSelfTest = false;
        std::uint32_t mSelfTestErrors = 0;
        std::string mSelfTestSample;
    };
} // namespace gpg::gal::diligent
