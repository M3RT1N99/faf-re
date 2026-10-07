#pragma once

// The Diligent side of the gal backend: device, immediate context, swap chain, the head render
// target, Clear and Present-by-draw, and the D3D11 debug layer's message counts.
//
// This header carries no Diligent or engine types, so DiligentHost.cpp is the only TU that includes
// Diligent's headers (they never meet boost 1.34, the msvc8 containers or wx; m6u-PLAN.txt B).

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gpg::gal::diligent
{
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

    class DiligentHost
    {
    public:
        DiligentHost();
        ~DiligentHost();
        DiligentHost(const DiligentHost&) = delete;
        DiligentHost& operator=(const DiligentHost&) = delete;

        /**
         * Creates the D3D11 device and immediate context, a swap chain on `window` (an HWND) and a
         * head render target of `width` x `height`. `validation` turns on Diligent's validation and,
         * in a Debug build of Diligent, the D3D11 debug layer (EngineFactoryD3D11.cpp:243-249).
         * Returns false with `error` set on failure.
         */
        bool Create(void* window, std::uint32_t width, std::uint32_t height, bool validation, std::string* error);

        /** Releases everything Create made; safe to call twice. */
        void Destroy();

        [[nodiscard]] bool IsCreated() const;

        /** Resizes the swap chain and recreates the head target. */
        bool Resize(std::uint32_t width, std::uint32_t height, std::string* error);

        /** Clears the head render target to `rgba` (0..1). */
        void ClearHead(const float rgba[4]);

        /**
         * Draws the head target into the swap chain's back buffer with a full-screen triangle and
         * presents. A draw rather than a copy: the swap chain may be BGRA where the head is RGBA,
         * which CopyResource cannot convert, and Android needs the same pass for pre-rotation
         * (m6u-CRIT.txt R11).
         */
        void Present(std::uint32_t syncInterval);

        /**
         * Reads the head render target back: a CopyTexture into a CPU-readable staging texture,
         * a wait for the GPU, then rows of R, G, B, A bytes into `out` (top row first, `width * 4`
         * bytes per row). The gal readback of the head (DeviceDiligent::GetRenderTargetData) uses
         * it, so the frame harness (port/graphics/capture) reads this backend the way it reads
         * D3D9. Returns false when there is no device or the staging texture cannot be made.
         */
        bool ReadHead(std::vector<std::uint8_t>* out, std::uint32_t* width, std::uint32_t* height);

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
} // namespace gpg::gal::diligent
