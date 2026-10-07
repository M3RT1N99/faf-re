#pragma once

// Windows "oracle mode" of the Diligent backend (m6u-CRIT.txt section 4, plan step 1): what the spike
// does not implement natively yet - effect metadata, texture decoding, GetTexture2D's DXT5 output,
// the capability lanes of the DeviceContext - it takes from the same D3D9/D3DX calls the D3D9 backend
// makes, so the engine above gal sees exactly what it sees on D3D9.
//
// Two D3D9 objects, and neither draws:
//   - IDirect3D9 alone answers the adapter, mode, format and caps queries for the HAL, with no
//     device (IDirect3D9::GetDeviceCaps/CheckDeviceFormat/EnumAdapterModes take an adapter index).
//   - A NULLREF device (D3DDEVTYPE_NULLREF) carries the D3DX objects that need a device: effects
//     (D3DXCreateEffect) and textures in the scratch/system-memory pools. It renders nothing and owns
//     no GPU memory. On this machine's d3d9.dll it creates effects and textures and answers
//     GetPassDesc, but ID3DXEffect::FindNextValidTechnique/ValidateTechnique crash in d3d9.dll
//     (an access violation reading 0; measured with the FAF effects, M6a step 1). Technique validity
//     is therefore decided from the HAL's caps instead (EffectsDiligent.cpp, IsTechniqueValid), and
//     gate 1 compares the resulting lists with a D3D9 HAL device's FindNextValidTechnique
//     (port/graphics/diligent/tools/fxtechlist.cpp).
// The device sits on a hidden 1x1 popup window of its own that is never shown, with
// D3DCREATE_NOWINDOWCHANGES, so it can neither take focus nor change the engine's window.

#include <d3d9.h>
#include <d3dx9.h>

#include <cstdint>
#include <string>

#include "gpg/core/streams/MemBufferStream.h"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/Head.hpp"
#include "legacy/containers/Vector.h"

namespace gpg::gal::diligent
{
    class D3D9Oracle
    {
    public:
        D3D9Oracle() = default;
        ~D3D9Oracle();
        D3D9Oracle(const D3D9Oracle&) = delete;
        D3D9Oracle& operator=(const D3D9Oracle&) = delete;

        /** Direct3DCreate9, the hidden window and the NULLREF device. */
        bool Init(std::string* error);
        void Shutdown();

        [[nodiscard]] IDirect3D9* GetDirect3D() const { return mDirect3D; }
        /** The HAL's caps for the default adapter (IDirect3D9::GetDeviceCaps, no device). */
        [[nodiscard]] const D3DCAPS9& GetHalCaps() const { return mHalCaps; }
        [[nodiscard]] IDirect3DDevice9* GetDevice() const { return mDevice; }
        /** "HAL+SWVP" style description of the NULLREF device's creation flags, for the report. */
        [[nodiscard]] const std::string& GetDeviceDescription() const { return mDeviceDescription; }

        /**
         * Copies `requested` into `out` and fills the capability lanes the way
         * DeviceD3D9::BuildDeviceCapabilities (D3D9Interfaces.cpp:2015-2165) does, from the HAL's
         * caps rather than a HAL device's: per head the adapter modes, render-target formats
         * (validFormats1), texture formats (validFormats2) and antialiasing options (mStrs); then
         * instancing, float16 vertex support, primitive and vertex limits, and the shader profile
         * tokens. Throws gal::Error where D3D9 throws (validation of shader model 2.0, instancing,
         * DXT support).
         */
        void FillCapabilities(const DeviceContext& requested, DeviceContext& out) const;

        /** DeviceD3D9::GetModesForAdapter (D3D9Interfaces.cpp:1493-1508) from IDirect3D9. */
        void GetModesForAdapter(msvc8::vector<HeadAdapterMode>& outModes, int adapterIndex) const;

        /**
         * DeviceD3D9::GetTexture2D (D3D9Interfaces.cpp:3134-3250) on the NULLREF device: the image
         * decoded with D3DX_DEFAULT_NONPOW2 and no filter, re-encoded to DXT5 when it is not DXT5,
         * and the blocks copied out. Bit-identical to D3D9 by construction.
         */
        void GetTexture2D(
            const void* sourceData,
            std::uint32_t sourceBytes,
            gpg::MemBuffer<char>* outTextureData,
            std::uint32_t* outWidth,
            int* outHeight
        ) const;

        // The D3D9 backend's format tables (D3D9Interfaces.cpp:685-771; binary 0x00D47FF0,
        // 0x00D42E84, 0x00D42194).
        [[nodiscard]] static std::uint32_t FormatGalToD3D(std::uint32_t mohoFormat);
        [[nodiscard]] static std::uint32_t FormatD3D9ToMoho(std::uint32_t d3dFormat);
        [[nodiscard]] static std::uint32_t RenderTargetFormatToD3D(std::uint32_t formatToken);
        [[nodiscard]] static std::uint32_t DepthStencilFormatToD3D(std::uint32_t formatToken);

    private:
        IDirect3D9* mDirect3D = nullptr;
        D3DCAPS9 mHalCaps{};
        IDirect3DDevice9* mDevice = nullptr;
        void* mWindow = nullptr; // HWND
        std::string mDeviceDescription;
    };

    /** gal::Error the way the D3D9/D3D10 backends throw it (D3D10Interfaces.cpp:456-464). */
    [[noreturn]] void ThrowGalError(const char* file, int line, const char* message);
    [[noreturn]] void ThrowGalErrorFromHresult(const char* file, int line, long hresult);
} // namespace gpg::gal::diligent
