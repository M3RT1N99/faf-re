#pragma once

// The few d3d9.h / d3d9types.h / d3d9caps.h declarations the Diligent backend uses as data, for builds
// without the DirectX SDK (Android, M7a1). The backend reproduces D3D9 and keeps D3D9's numbers in its
// tables (the vertex declaration table, the D3DFORMAT values of the texture path, the caps it hands the
// effect layer), so these are d3d9types.h's values verbatim. Nothing here talks to a D3D9 runtime.
//
// Windows builds include the SDK headers instead and never see this file. The Win32 basics (DWORD,
// LONG, RECT, D3DVIEWPORT9, HRESULT) come from the engine's Android shim (port/engine/shim).

#if defined(_WIN32)
#    error "D3D9Portable.h is for builds without the DirectX SDK; Windows includes <d3d9.h>."
#endif

#include <cstdint>

#include "platform/Platform.h" // the shim's Win32 types (DWORD, BYTE, WORD, LONG, HRESULT, RECT)

// ---- d3d9types.h: vertex declarations -------------------------------------------------------------

struct D3DVERTEXELEMENT9
{
    WORD Stream;
    WORD Offset;
    BYTE Type;
    BYTE Method;
    BYTE Usage;
    BYTE UsageIndex;
};
static_assert(sizeof(D3DVERTEXELEMENT9) == 8, "d3d9types.h D3DVERTEXELEMENT9 layout");

enum D3DDECLTYPE : BYTE
{
    D3DDECLTYPE_FLOAT1 = 0,
    D3DDECLTYPE_FLOAT2 = 1,
    D3DDECLTYPE_FLOAT3 = 2,
    D3DDECLTYPE_FLOAT4 = 3,
    D3DDECLTYPE_D3DCOLOR = 4,
    D3DDECLTYPE_UBYTE4 = 5,
    D3DDECLTYPE_SHORT2 = 6,
    D3DDECLTYPE_SHORT4 = 7,
    D3DDECLTYPE_UBYTE4N = 8,
    D3DDECLTYPE_SHORT2N = 9,
    D3DDECLTYPE_SHORT4N = 10,
    D3DDECLTYPE_USHORT2N = 11,
    D3DDECLTYPE_USHORT4N = 12,
    D3DDECLTYPE_UDEC3 = 13,
    D3DDECLTYPE_DEC3N = 14,
    D3DDECLTYPE_FLOAT16_2 = 15,
    D3DDECLTYPE_FLOAT16_4 = 16,
    D3DDECLTYPE_UNUSED = 17
};

enum D3DDECLMETHOD : BYTE
{
    D3DDECLMETHOD_DEFAULT = 0
};

enum D3DDECLUSAGE : BYTE
{
    D3DDECLUSAGE_POSITION = 0,
    D3DDECLUSAGE_BLENDWEIGHT = 1,
    D3DDECLUSAGE_BLENDINDICES = 2,
    D3DDECLUSAGE_NORMAL = 3,
    D3DDECLUSAGE_PSIZE = 4,
    D3DDECLUSAGE_TEXCOORD = 5,
    D3DDECLUSAGE_TANGENT = 6,
    D3DDECLUSAGE_BINORMAL = 7,
    D3DDECLUSAGE_TESSFACTOR = 8,
    D3DDECLUSAGE_POSITIONT = 9,
    D3DDECLUSAGE_COLOR = 10,
    D3DDECLUSAGE_FOG = 11,
    D3DDECLUSAGE_DEPTH = 12,
    D3DDECLUSAGE_SAMPLE = 13
};

// d3d9types.h: {0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0}
#define D3DDECL_END() {0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0}

// ---- d3d9caps.h: the two caps the effect layer reads ----------------------------------------------

/** D3DCAPS9 reduced to what the backend reads (technique validity follows the shader versions). */
struct D3DCAPS9
{
    DWORD VertexShaderVersion;
    DWORD PixelShaderVersion;
};

#define D3DVS_VERSION(major, minor) (0xFFFE0000u | ((major) << 8) | (minor))
#define D3DPS_VERSION(major, minor) (0xFFFF0000u | ((major) << 8) | (minor))

// ---- d3d9.h: the error the D3D9 backend reports for a failed effect call ---------------------------

#ifndef D3DERR_INVALIDCALL
#    define D3DERR_INVALIDCALL static_cast<HRESULT>(0x8876086CL)
#endif

// ---- d3d9types.h: the D3DFORMAT values of the texture path ------------------------------------------

namespace gpg::gal::diligent::d3dfmt
{
    constexpr std::uint32_t FourCC(const char a, const char b, const char c, const char d)
    {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8U) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16U) |
               (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24U);
    }
    constexpr std::uint32_t kUnknown = 0, kR8G8B8 = 20, kA8R8G8B8 = 21, kX8R8G8B8 = 22, kR5G6B5 = 23, kX1R5G5B5 = 24,
                            kA1R5G5B5 = 25, kA4R4G4B4 = 26, kA8 = 28, kA2B10G10R10 = 31, kA8B8G8R8 = 32, kX8B8G8R8 = 33,
                            kG16R16 = 34, kA2R10G10B10 = 35, kA16B16G16R16 = 36, kL8 = 50, kA8L8 = 51, kR16F = 111,
                            kG16R16F = 112, kA16B16G16R16F = 113, kR32F = 114, kG32R32F = 115, kA32B32G32R32F = 116;
    constexpr std::uint32_t kDxt1 = FourCC('D', 'X', 'T', '1'), kDxt2 = FourCC('D', 'X', 'T', '2'), kDxt3 = FourCC('D', 'X', 'T', '3'),
                            kDxt4 = FourCC('D', 'X', 'T', '4'), kDxt5 = FourCC('D', 'X', 'T', '5');
} // namespace gpg::gal::diligent::d3dfmt
