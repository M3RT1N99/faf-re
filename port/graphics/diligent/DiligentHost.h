#pragma once

// The Diligent side of the gal backend: device, immediate context, swap chain, the head render
// target, Present-by-draw, readbacks, and the D3D11 debug layer's message counts.
//
// This header carries no Diligent or engine types; Diligent interfaces appear only as forward
// declarations, so engine TUs that include it never meet Diligent's headers (m6u-PLAN.txt B).

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Diligent
{
    struct IBuffer;
    struct IRenderDevice;
    struct IDeviceContext;
    struct ITexture;
    struct ITextureView;
} // namespace Diligent

namespace gpg::gal::diligent
{
    class GpuBuffer;
    class GpuTexture;

    /**
     * The Diligent device type behind `/gal diligent:<api>` (M6c step 6 adds Vulkan and OpenGL on
     * Windows; the Android GLES path inherits the OpenGL conventions below).
     */
    enum class GraphicsApi : std::uint8_t
    {
        D3D11 = 0,
        Vulkan = 1,
        OpenGL = 2
    };

    /** "d3d11", "vk", "gl": the `/gal diligent:<api>` names. */
    [[nodiscard]] const char* GraphicsApiName(GraphicsApi api);
    /** Parses a `/gal diligent:<api>` name ("vulkan" and "opengl" are accepted too). */
    [[nodiscard]] bool ParseGraphicsApi(const char* name, GraphicsApi* api);

    /**
     * The OpenGL conventions (m6u-CRIT.txt R4), decided once here so the Android GLES path inherits
     * them. GL leaves the NDC z range (-1..1 without clip control) and the render-target row order
     * (window y up) to the application. The backend renders every target mirrored in GL terms, so
     * that texture memory keeps D3D's row order - row 0 at the top, as textures loaded from files have
     * it and as the harness reads frames back:
     *   - every vertex shader ends with y = -y and z = 2z - w (ShaderCompileDiligent.cpp);
     *   - viewports and scissor rectangles are handed to Diligent mirrored (top = height - bottom),
     *     because Diligent's GL backend converts D3D's top-left origin to GL's bottom-left itself;
     *   - the mirror turns the winding around, so front faces are counter-clockwise;
     *   - Present, which draws the head into the window (GL's own y-up framebuffer), reads the head's
     *     rows bottom-up.
     * Sampling, uploads, copies and readbacks then need no change: memory rows are D3D rows everywhere.
     */
    [[nodiscard]] inline bool& GlMirrorDisabledFlag()
    {
        static bool disabled = false;
        return disabled;
    }

    /** `/galglnomirror`: GL without the mirror convention (a diagnostic: GL's frames come back upside down). */
    inline void SetGlMirrorDisabled(const bool disabled)
    {
        GlMirrorDisabledFlag() = disabled;
    }

    [[nodiscard]] inline bool& SwapChainBgraFlag()
    {
        static bool bgra = false;
        return bgra;
    }

    /**
     * `/galswapchainbgra`: ask for a BGRA8 swap chain (a diagnostic for the BGRA path, m6u-CRIT.txt R11:
     * some Vulkan drivers, the phone's among them, offer only BGRA; this machine gives RGBA when asked).
     */
    inline void SetSwapChainBgraRequested(const bool bgra)
    {
        SwapChainBgraFlag() = bgra;
    }

    [[nodiscard]] inline bool FlipsRenderTargets(const GraphicsApi api)
    {
        return api == GraphicsApi::OpenGL && !GlMirrorDisabledFlag();
    }

    /** D3D11_MESSAGE_SEVERITY buckets, as the D3D11 debug layer stored them. */
    struct DebugLayerCounts
    {
        std::uint64_t corruption = 0;
        std::uint64_t error = 0;
        std::uint64_t warning = 0;
        std::uint64_t info = 0;
        std::uint64_t message = 0;
    };

    /** Diligent's own DEBUG_MESSAGE_SEVERITY buckets (its validation and log output). */
    struct DiligentMessageCounts
    {
        std::uint64_t info = 0;
        std::uint64_t warning = 0;
        std::uint64_t error = 0;
        std::uint64_t fatal = 0;
    };

    /**
     * How the backend moved data to the GPU, for the report and the Vulkan-readiness gate
     * (docs/port/renderer.md M6b: no UpdateBuffer in per-draw paths; dynamic buffers through
     * Map(DISCARD/NO_OVERWRITE) like D3D9's locks, m6u-CRIT.txt R2).
     */
    struct GpuUploadStats
    {
        std::atomic<std::uint64_t> mapDiscard{0};         // dynamic buffer: D3D9 DISCARD lock
        std::atomic<std::uint64_t> mapNoOverwrite{0};     // dynamic buffer: D3D9 NOOVERWRITE lock, same frame
        std::atomic<std::uint64_t> mapFrameRestore{0};    // dynamic buffer: first non-discard map of a frame, whole shadow
        std::atomic<std::uint64_t> mapBytes{0};
        std::atomic<std::uint64_t> updateBuffer{0};       // static buffers (UpdateBuffer)
        std::atomic<std::uint64_t> updateBufferInFrame{0};// ... of those, between the first and last draw of a frame
        std::atomic<std::uint64_t> updateTexture{0};
        std::atomic<std::uint64_t> updateTextureBytes{0};
        std::atomic<std::uint64_t> texturesCreated{0};
        std::atomic<std::uint64_t> buffersCreated{0};
        std::atomic<std::uint64_t> createdOffRenderThread{0}; // GPU objects made on another thread (must stay 0 for GLES)
        std::atomic<std::uint64_t> releasedOffRenderThread{0};// ... dropped on another thread (deferred to the render thread on GL)
    };

    /**
     * The OpenGL debug output (GL_KHR_debug), counted per message type, when validation is on and the
     * context is a debug context. Filled by the backend's own glDebugMessageCallback (synchronous).
     */
    struct GlDebugCounts
    {
        bool active = false;
        std::uint64_t error = 0;              // GL_DEBUG_TYPE_ERROR
        std::uint64_t undefinedBehavior = 0;  // GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR
        std::uint64_t deprecated = 0;
        std::uint64_t portability = 0;
        std::uint64_t performance = 0;
        std::uint64_t other = 0;              // GL_DEBUG_TYPE_OTHER, MARKER, PUSH/POP_GROUP
        std::uint64_t notification = 0;       // of all the above, severity NOTIFICATION
    };

    /**
     * Vulkan render-pass scopes, counted at vkCmdBegin/EndRenderPass and vkCmdBegin/EndRendering (the
     * command-buffer calls Diligent makes through volk), with the backend's phase at each end: inside a
     * draw slot (a primbatcher flush: its buffer maps, the constant maps, the draw), and of those the
     * ones a texture upload caused (a dirty texture written at bind time; Vulkan copies outside render
     * passes). m6u-CRIT.txt R2: dynamic buffers must not end the render pass.
     */
    struct RenderPassCounts
    {
        bool hooked = false;
        std::uint64_t begins = 0;
        std::uint64_t ends = 0;
        std::uint64_t endsInDraw = 0;             // inside a draw slot (DeviceDiligent::SubmitDraw), by the innermost phase:
        std::uint64_t endsInDrawByUpload = 0;     //   a texture created or written (and transitioned) at bind time
        std::uint64_t endsInDrawByTargets = 0;    //   the slot binding another render target (a new render pass)
        std::uint64_t endsInDrawByMap = 0;        //   the vertex/index buffer maps (the primbatcher's DISCARD locks)
        std::uint64_t endsInDrawByCommit = 0;     //   the effect's constants and resource bindings
        std::uint64_t endsInDrawOther = 0;        //   anything else in the slot (the draw call itself)
        std::uint64_t endsByUploadOutsideDraw = 0;
    };

    /** The Vulkan render-pass counters (process-wide: the hooks are plain function pointers). */
    [[nodiscard]] RenderPassCounts GetRenderPassCounts();

    /** Marks a phase of the render thread for the render-pass counters (Vulkan only; cheap elsewhere). */
    class ScopedRenderPhase
    {
    public:
        enum class Kind : std::uint8_t
        {
            Draw,
            Upload,
            Targets,
            Map,
            Commit
        };
        explicit ScopedRenderPhase(Kind kind);
        ~ScopedRenderPhase();
        ScopedRenderPhase(const ScopedRenderPhase&) = delete;
        ScopedRenderPhase& operator=(const ScopedRenderPhase&) = delete;

    private:
        Kind kind_;
    };

    /**
     * What every GPU-backed gal object shares: the Diligent device and immediate context (strong
     * references, released when the last holder goes, so resources the engine frees after the gal
     * Device are still safe), the lock that serialises immediate-context use (D3D9 runs
     * D3DCREATE_MULTITHREADED, flags 0x44 at D3D9Interfaces.cpp:1772, and the ResourceManager prefetch
     * thread creates textures), the frame counter dynamic buffers key their "first map of the frame"
     * on, and the upload statistics.
     */
    class GpuShared
    {
    public:
        GpuShared(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, std::uint32_t renderThreadId,
                  GraphicsApi api = GraphicsApi::D3D11);
        ~GpuShared();
        GpuShared(const GpuShared&) = delete;
        GpuShared& operator=(const GpuShared&) = delete;

        [[nodiscard]] Diligent::IRenderDevice* Device() const { return device_; }
        [[nodiscard]] Diligent::IDeviceContext* Context() const { return context_; }
        [[nodiscard]] std::recursive_mutex& Lock() { return lock_; }
        [[nodiscard]] std::uint64_t Frame() const { return frame_.load(); }
        void AdvanceFrame() { ++frame_; inFrame_ = false; }
        /** Set by the first draw of a frame, cleared by Present: "inside the per-draw path". */
        void MarkDraw() { inFrame_ = true; }
        [[nodiscard]] bool InFrame() const { return inFrame_; }
        [[nodiscard]] bool OnRenderThread() const;
        [[nodiscard]] GpuUploadStats& Stats() { return stats_; }
        [[nodiscard]] GraphicsApi Api() const { return api_; }
        /** GL: render targets are drawn mirrored so memory keeps D3D's row order (FlipsRenderTargets). */
        [[nodiscard]] bool FlipY() const { return FlipsRenderTargets(api_); }

        /**
         * Drops a GPU object. On GL a context exists only on the render thread (Diligent's GL device has
         * no multithreaded resource creation, RenderDeviceGLImpl.cpp:877, m6u-CRIT.txt R3), so an object
         * dropped elsewhere - the resource manager's prefetch thread releases textures - waits for
         * DrainRetired on the render thread. Counted on every API.
         */
        void Retire(std::unique_ptr<GpuTexture> texture);
        void Retire(std::unique_ptr<GpuBuffer> buffer);
        /** Render thread: releases what Retire deferred (Present, and the device's destruction). */
        void DrainRetired();

    private:
        Diligent::IRenderDevice* device_ = nullptr;
        Diligent::IDeviceContext* context_ = nullptr;
        std::recursive_mutex lock_;
        std::atomic<std::uint64_t> frame_{1};
        bool inFrame_ = false;
        std::uint32_t renderThreadId_ = 0;
        GraphicsApi api_ = GraphicsApi::D3D11;
        GpuUploadStats stats_;
        std::mutex retiredLock_;
        std::vector<std::unique_ptr<GpuTexture>> retiredTextures_;
        std::vector<std::unique_ptr<GpuBuffer>> retiredBuffers_;
    };

    class DiligentHost
    {
    public:
        DiligentHost();
        ~DiligentHost();
        DiligentHost(const DiligentHost&) = delete;
        DiligentHost& operator=(const DiligentHost&) = delete;

        /**
         * Creates the device of `api` and its immediate context, a swap chain on `window` (an HWND) and
         * a head render target of `width` x `height`. `validation` turns on Diligent's validation and
         * the API's own: the D3D11 debug layer in a Debug build of Diligent (EngineFactoryD3D11.cpp:
         * 243-249), the Khronos validation layer for Vulkan when the machine has it, a debug context
         * and the GL debug output for OpenGL. Returns false with `error` set on failure.
         */
        bool Create(void* window, std::uint32_t width, std::uint32_t height, GraphicsApi api, bool validation, std::string* error);

        [[nodiscard]] GraphicsApi GetApi() const;

        /** Releases everything Create made; safe to call twice. */
        void Destroy();

        [[nodiscard]] bool IsCreated() const;

        /** Resizes the swap chain and recreates the head target. */
        bool Resize(std::uint32_t width, std::uint32_t height, std::string* error);

        /** The shared device/context/lock object; resources keep a reference. */
        [[nodiscard]] const std::shared_ptr<GpuShared>& GetGpu() const;

        /** The head render target (RGBA8_UNORM, render target + shader resource + copy source). */
        [[nodiscard]] Diligent::ITexture* GetHeadTexture() const;

        /**
         * Draws the head target into the swap chain's back buffer with a full-screen triangle and
         * presents. A draw rather than a copy: the swap chain may be BGRA where the head is RGBA,
         * which CopyResource cannot convert, and Android needs the same pass for pre-rotation
         * (m6u-CRIT.txt R11). Leaves no render target bound.
         */
        void Present(std::uint32_t syncInterval);

        /**
         * Reads a 2D render target's level 0 back in D3D9's A8R8G8B8 memory order (B, G, R, A per texel,
         * top row first, `width * 4` bytes per row): a CopyTexture into a CPU-readable staging texture of
         * the same format, a wait for the GPU, then a swizzle from RGBA8 (BGRA8 copies as is).
         * GetRenderTargetData uses it, so the frame harness (port/graphics/capture) reads this backend the
         * way it reads D3D9. Returns false for another format, without a device, or when the staging
         * texture cannot be made.
         */
        bool ReadTextureBgra8(Diligent::ITexture* texture, std::vector<std::uint8_t>* out, std::uint32_t* width, std::uint32_t* height);

        /**
         * Reads `size` bytes of a buffer back through a staging copy (CopyBuffer, wait, map). For the
         * self test (/galselftest) only: it stalls the GPU.
         */
        bool ReadBuffer(Diligent::IBuffer* buffer, std::uint32_t size, std::vector<std::uint8_t>* out);

        /**
         * Moves the debug layer's stored messages into the counters, appending up to `keep` of the
         * texts (severity first) to `out` when it is non-null.
         */
        void DrainDebugLayer(std::vector<std::string>* out, std::size_t keep);

        /**
         * Proves the debug layer is live: drains what is pending into the counters, asks the
         * native D3D11 device for a zero-sized buffer (which the runtime refuses and the layer
         * reports as an error before anything reaches the driver), and returns how many error
         * messages that produced, with the first one in `sample`. Those messages are cleared and
         * not counted.
         */
        std::uint32_t SelfTestDebugLayer(std::string* sample);

        /**
         * The GL counterpart of SelfTestDebugLayer: one invalid call (glBindTexture with an invalid
         * target: GL_INVALID_ENUM, no state change) must reach the debug output as an error. Returns
         * how many error messages it produced; they are not counted.
         */
        std::uint32_t SelfTestGlDebugOutput(std::string* sample);

        /** The GL debug output's counts; the first warnings and errors go to `out` when it is given. */
        [[nodiscard]] GlDebugCounts GetGlDebugCounts(std::vector<std::string>* out = nullptr) const;

        /**
         * Vulkan only: the instance layers the loader offers this process (its bitness), and whether
         * the Khronos validation layer was among them, as JSON object text.
         */
        [[nodiscard]] std::string GetVulkanLayerReport() const;

        /** Diligent's command counters of the immediate context (DeviceContextStats), as JSON object text. */
        [[nodiscard]] std::string GetContextStatsJson() const;

        [[nodiscard]] bool IsDebugLayerActive() const;
        [[nodiscard]] DebugLayerCounts GetDebugLayerCounts() const;
        [[nodiscard]] static DiligentMessageCounts GetDiligentMessageCounts();
        /** The first few Diligent warnings and errors, for the report. */
        [[nodiscard]] static std::vector<std::string> GetDiligentMessages();
        [[nodiscard]] std::string GetAdapterDescription() const;
        [[nodiscard]] std::string GetSwapChainFormat() const;
        [[nodiscard]] std::uint32_t GetHeadWidth() const;
        [[nodiscard]] std::uint32_t GetHeadHeight() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };

    // ---------------------------------------------------------------------------------------------
    // GPU objects behind the gal resources. Engine-side code (ResourcesDiligent.cpp) holds them through
    // these classes and never sees a Diligent header. Every method takes the GpuShared lock.

    /**
     * How CPU texels in D3D9's layout become the Diligent texture's texels (TextureFormatForD3D9).
     * D3D11 has no L8/A8L8/R8G8B8/X1R5G5B5/A4R4G4B4 and no view swizzles (m6u-DIL.txt 9), so those
     * formats are expanded on upload to BGRA8 with D3D9's sampling result: L8 -> (L, L, L, 1),
     * A8L8 -> (L, L, L, A), X8-less formats -> alpha 1 (the D3DFORMAT reference).
     */
    enum class TexelConversion : std::uint8_t
    {
        None = 0,
        L8ToBgra8,
        A8L8ToBgra8,
        R8G8B8ToBgra8,
        X1R5G5B5ToBgra8,
        A4R4G4B4ToBgra8,
        A2R10G10B10ToRgb10A2
    };

    /**
     * The Diligent TEXTURE_FORMAT (as an integer) for a D3DFORMAT texture, and the conversion its texels
     * need; 0 when there is none (D3DFMT_A8R8G8B8 -> BGRA8_UNORM, the same bytes; DXT1 -> BC1,
     * DXT2/3 -> BC2, DXT4/5 -> BC3; the float formats one to one).
     */
    std::uint32_t TextureFormatForD3D9(std::uint32_t d3dFormat, TexelConversion* conversion);

    /** Bytes per D3D9 texel (or per 4x4 block for DXT), and whether the format is block compressed. */
    std::uint32_t D3D9FormatBytes(std::uint32_t d3dFormat, bool* blockCompressed);

    /**
     * gal render-target format tokens 1..7 (A2R10G10B10, A8R8G8B8, X8R8G8B8, A1R5G5B5, X1R5G5B5, R5G6B5,
     * G16R16; D3D9Interfaces.cpp:747-760) as Diligent formats: RGBA8_UNORM for the 8-bit ones (the
     * head's format; the channels sample the same), the others by value.
     */
    std::uint32_t RenderTargetFormatForToken(std::uint32_t token);

    /** gal depth-stencil tokens 1..6 (D32, D15S1, D24S8, D24X8, D24X4S4, D16; D3D9Interfaces.cpp:763-771). */
    std::uint32_t DepthStencilFormatForToken(std::uint32_t token);

    struct GpuTextureDesc
    {
        enum class Kind : std::uint8_t
        {
            Texture2D,
            TextureCube,
            Texture3D
        };
        Kind kind = Kind::Texture2D;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t depth = 1;       // Texture3D
        std::uint32_t mipLevels = 1;
        std::uint32_t format = 0;      // Diligent TEXTURE_FORMAT
        bool renderTarget = false;
        bool depthStencil = false;
        bool shaderResource = true;
        bool generateMips = false;     // D3DUSAGE_AUTOGENMIPMAP
        const char* name = "gal texture";
    };

    /** One subresource's data: rows of `stride` bytes (block rows for BC), `depthStride` per slice. */
    struct GpuSubresource
    {
        const void* data = nullptr;
        std::uint32_t stride = 0;
        std::uint32_t depthStride = 0;
    };

    class GpuTexture
    {
    public:
        /**
         * Creates the texture with `initial` holding mipLevels x faces subresources in Diligent's order
         * (face-major), or none. Null with `error` set on failure.
         */
        static std::unique_ptr<GpuTexture> Create(GpuShared& gpu, const GpuTextureDesc& desc, const GpuSubresource* initial,
                                                  std::uint32_t initialCount, std::string* error);
        /** Wraps a texture made elsewhere (the head); keeps a reference. */
        static std::unique_ptr<GpuTexture> Wrap(Diligent::ITexture* texture);
        ~GpuTexture();

        /** UpdateTexture of a region of one subresource (x, y, width, height in texels, BC-aligned). */
        void Update(GpuShared& gpu, std::uint32_t level, std::uint32_t face, std::uint32_t x, std::uint32_t y, std::uint32_t width,
                    std::uint32_t height, const GpuSubresource& data);
        /** GenerateMips (the D3DUSAGE_AUTOGENMIPMAP textures). */
        void GenerateMips(GpuShared& gpu);

        [[nodiscard]] Diligent::ITexture* Texture() const;
        [[nodiscard]] Diligent::ITextureView* ShaderResourceView() const;
        [[nodiscard]] Diligent::ITextureView* RenderTargetView(std::uint32_t face = 0);
        [[nodiscard]] Diligent::ITextureView* DepthStencilView() const;
        [[nodiscard]] std::uint32_t Width() const;
        [[nodiscard]] std::uint32_t Height() const;

    private:
        GpuTexture();
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class GpuBuffer
    {
    public:
        /**
         * A vertex or index buffer of `size` bytes. Dynamic buffers are USAGE_DYNAMIC and written only
         * through Map (WriteDiscard/WriteNoOverwrite); the others are USAGE_DEFAULT, created with
         * `initial` and changed with UpdateBuffer.
         */
        static std::unique_ptr<GpuBuffer> Create(GpuShared& gpu, std::uint32_t size, bool indexBuffer, bool dynamic, const void* initial,
                                                 std::string* error);
        ~GpuBuffer();

        /** Map(MAP_WRITE, MAP_FLAG_DISCARD) and copy `size` bytes to `offset`. */
        void WriteDiscard(GpuShared& gpu, std::uint32_t offset, const void* data, std::uint32_t size);
        /** Map(MAP_WRITE, MAP_FLAG_NO_OVERWRITE) and copy `size` bytes to `offset`. */
        void WriteNoOverwrite(GpuShared& gpu, std::uint32_t offset, const void* data, std::uint32_t size);
        /** UpdateBuffer (static buffers only). */
        void Update(GpuShared& gpu, std::uint32_t offset, const void* data, std::uint32_t size);

        [[nodiscard]] Diligent::IBuffer* Buffer() const;

    private:
        GpuBuffer();
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    /**
     * Runs the scope at the CRT default x87 precision and restores the engine's control word after.
     * The engine runs the main thread under 24-bit precision (CScApp::Init); the shader compiler and the
     * driver are not written for that (m6u-PLAN.txt B, "save and restore the x87 control word").
     */
    class ScopedDefaultFpu
    {
    public:
        ScopedDefaultFpu();
        ~ScopedDefaultFpu();
        ScopedDefaultFpu(const ScopedDefaultFpu&) = delete;
        ScopedDefaultFpu& operator=(const ScopedDefaultFpu&) = delete;

    private:
        unsigned int mSaved = 0;
    };
} // namespace gpg::gal::diligent
