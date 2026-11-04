// The Direct3D 10 hardware-vertex formatters, for the Android client build. Every API but
// Direct3D 9 packs its mesh vertices with them, the Vulkan backend too (gpg/gal/MeshVertex.cpp).
// The engine defines them in backends/d3d10/HardwareVertexFormatterD3D10.cpp and
// Float16HardwareVertexFormatterD3D10.cpp, which this build leaves out with the rest of that
// backend; the bodies here are those files', except that the float16 formatter's halves come from
// ConvertFloatToHalf instead of D3DXFloat32To16Array.
//
// The Direct3D 9 formatters, which only a Direct3D 9 device chooses, are in D3D9Unavailable.cpp.

#include <cstdint>
#include <cstring>

#include "gpg/gal/Device.hpp"
#include "gpg/gal/DeviceContext.hpp"
#include "gpg/gal/MeshVertex.h"
#include "gpg/gal/backends/d3d10/Float16HardwareVertexFormatterD3D10.hpp"
#include "gpg/gal/backends/d3d10/HardwareVertexFormatterD3D10.hpp"

namespace gpg::gal
{
  namespace
  {
    constexpr std::uint32_t kHardwareVertexFormatToken = 14U;
    constexpr std::uint32_t kFloat16VertexFormatToken = 15U;
    constexpr std::uint32_t kHardwareVertexStrideBase = 0x48U;
    constexpr std::uint32_t kFloat16VertexStrideStream0 = 0x2CU;
    constexpr std::uint32_t kFloat16VertexStrideStream1 = 0x44U;

    /**
     * D3DXFloat32To16Array for one value, as the x64 D3DX9_43.dll of the June 2010 SDK, which the
     * Windows build links, computes it; checked against that DLL for all 2^32 patterns.
     *
     * It is D3DX's C routine: XNA Math's XMConvertFloatToHalf of 2010, which rounds to nearest
     * even, with D3DX's guard that a denormal's shift above 31 gives 0. Below 2^-14, where the
     * halves are denormal, the bits that shift drops do not count in the rounding. D3DX's halves
     * have no infinity or NaN: from 131040 up, and for infinities and NaNs, the result is 0x7FFF
     * (131008) with the sign.
     *
     * D3DX picks its routine by CPU. A GenuineIntel CPU runs this one for a single value, and so
     * does every CPU when the registry sets DisableD3DXPSGP. Other CPUs with SSE2 run an SSE
     * routine that differs in 2.3 % of the patterns: below 2^-14, at the top of each binade and
     * for NaNs.
     */
    [[nodiscard]] std::uint16_t ConvertFloatToHalf(const float value)
    {
      std::uint32_t bits;
      std::memcpy(&bits, &value, sizeof(bits));
      const std::uint32_t sign = (bits & 0x80000000U) >> 16U;
      bits &= 0x7FFFFFFFU;

      std::uint32_t result;
      if (bits > 0x47FFEFFFU) {
        result = 0x7FFFU;
      } else {
        if (bits < 0x38800000U) {
          const std::uint32_t shift = 113U - (bits >> 23U);
          bits = (shift > 31U) ? 0U : (0x800000U | (bits & 0x7FFFFFU)) >> shift;
        } else {
          bits += 0xC8000000U;
        }
        result = (bits + 0x0FFFU + ((bits >> 13U) & 1U)) >> 13U;
      }
      return static_cast<std::uint16_t>(result | sign);
    }

    /**
     * Address: 0x0094D490 (FUN_0094D490)
     *
     * What it does:
     * Copies rows `(0,1,2)`, `(4,5,6)`, `(8,9,10)`, `(12,13,14)` from one
     * source 4x4 matrix into four contiguous 3-float destination rows.
     */
    const Matrix* CopyMatrix4x3Rows(
      float* const outRow0,
      float* const outRow1,
      float* const outRow2,
      float* const outRow3,
      const Matrix& matrix
    )
    {
      float* const outRows[4] = {outRow0, outRow1, outRow2, outRow3};
      for (int row = 0; row < 4; ++row) {
        outRows[row][0] = matrix.r[row].x;
        outRows[row][1] = matrix.r[row].y;
        outRows[row][2] = matrix.r[row].z;
      }

      return &matrix;
    }
  } // namespace

  /**
   * Address: 0x0094D4F0 (FUN_0094D4F0, ??0HardwareVertexFormatterD3D10@gal@gpg@@QAE@@Z)
   *
   * What it does:
   * Initializes one D3D10 hardware-vertex formatter wrapper.
   */
  HardwareVertexFormatterD3D10::HardwareVertexFormatterD3D10() = default;

  /**
   * Address: 0x0094D500 (FUN_0094D500)
   * Address: 0x0094D8F0 (FUN_0094D8F0, slot 0: the scalar deleting destructor)
   *
   * What it does:
   * Nothing of its own; reinstalls the base `MeshFormatter` vtable.
   */
  HardwareVertexFormatterD3D10::~HardwareVertexFormatterD3D10() = default;

  /**
   * Address: 0x0094D510 (FUN_0094D510)
   *
   * What it does:
   * Returns the active device context's hardware-instancing flag. Unlike the
   * D3D9 formatters it ignores the `mesh_Rebatch` switches.
   */
  bool HardwareVertexFormatterD3D10::AllowMeshInstancing()
  {
    return Device::GetInstance()->GetDeviceContext()->mHWBasedInstancing;
  }

  /**
   * Address: 0x0094D960 (FUN_0094D960)
   *
   * What it does:
   * Creates vertex format 14 on the active device (slot 14, `[vtbl+0x38]`).
   */
  boost::shared_ptr<VertexFormat> HardwareVertexFormatterD3D10::CreateVertexFormat(const std::int32_t layoutVariant)
  {
    static_cast<void>(layoutVariant);
    return Device::GetInstance()->CreateVertexFormat(kHardwareVertexFormatToken);
  }

  /**
   * Address: 0x0094D530 (FUN_0094D530)
   *
   * What it does:
   * Returns packed hardware-vertex stride for the requested stream class.
   */
  std::uint32_t HardwareVertexFormatterD3D10::GetVertexStride(
    const std::int32_t streamClass,
    const std::int32_t sizeVariant
  )
  {
    static_cast<void>(sizeVariant);
    return kHardwareVertexStrideBase + ((streamClass != 0) ? 4U : 0U);
  }

  /**
   * Address: 0x0094D550 (FUN_0094D550)
   *
   * What it does:
   * Packs one mesh vertex into vertex format 14: the geometry record for
   * stream class 0 (position with w = 1), the instance record otherwise.
   */
  void HardwareVertexFormatterD3D10::WriteFormattedVertex(
    const std::int32_t streamClass,
    void* const destinationVertex,
    const MeshVertex& source,
    const std::int32_t writeVariant
  )
  {
    static_cast<void>(writeVariant);

    if (streamClass != 0) {
      auto& destination = *static_cast<HardwareVertexInstance*>(destinationVertex);
      destination.instanceIndex = source.instanceIndex;
      destination.color = source.color;
      destination.shaderTime = source.shaderTime;
      static_cast<void>(CopyMatrix4x3Rows(
        destination.transform[0], destination.transform[1], destination.transform[2], destination.transform[3],
        source.transform
      ));
      destination.bonePaletteBase = source.bonePaletteBase;
      destination.secondaryDataMask = (source.useSecondaryData != 0U) ? static_cast<std::uint8_t>(0xFFU) : 0U;
      destination.scroll[0] = source.scroll[0];
      destination.scroll[1] = source.scroll[1];
      destination.dissolve = source.dissolve;
      destination.parameter = source.parameter;
      destination.meshColor = source.meshColor;
      return;
    }

    auto& destination = *static_cast<HardwareVertex*>(destinationVertex);
    destination.boneIndices[0] = source.boneIndices[0];
    destination.boneIndices[1] = source.boneIndices[1];
    destination.boneIndices[2] = source.boneIndices[2];
    destination.boneIndices[3] = source.boneIndices[3];
    destination.position[0] = source.position[0];
    destination.position[1] = source.position[1];
    destination.position[2] = source.position[2];
    destination.position[3] = 1.0f;
    destination.normal[0] = source.normal[0];
    destination.normal[1] = source.normal[1];
    destination.normal[2] = source.normal[2];
    destination.binormal[0] = source.binormal[0];
    destination.binormal[1] = source.binormal[1];
    destination.binormal[2] = source.binormal[2];
    destination.tangent[0] = source.tangent[0];
    destination.tangent[1] = source.tangent[1];
    destination.tangent[2] = source.tangent[2];
    destination.texCoords[0] = source.texCoord0[0];
    destination.texCoords[1] = source.texCoord0[1];
    destination.texCoords[2] = source.texCoord1[0];
    destination.texCoords[3] = source.texCoord1[1];
  }

  /**
   * Address: 0x0094D770 (FUN_0094D770, ??0Float16HardwareVertexFormatterD3D10@gal@gpg@@QAE@@Z)
   *
   * What it does:
   * Initializes one D3D10 float16 hardware-vertex formatter wrapper.
   */
  Float16HardwareVertexFormatterD3D10::Float16HardwareVertexFormatterD3D10() = default;

  /**
   * Address: 0x0094D780 (FUN_0094D780)
   * Address: 0x0094D910 (FUN_0094D910, slot 0: the scalar deleting destructor)
   *
   * What it does:
   * Nothing of its own; reinstalls the base `MeshFormatter` vtable.
   */
  Float16HardwareVertexFormatterD3D10::~Float16HardwareVertexFormatterD3D10() = default;

  /**
   * Address: 0x0094D790 (FUN_0094D790)
   *
   * What it does:
   * Needs both of the active device context's flags, hardware instancing
   * (+0x11) and float16 (+0x12); ignores the `mesh_Rebatch` switches.
   */
  bool Float16HardwareVertexFormatterD3D10::AllowMeshInstancing()
  {
    const DeviceContext* const context = Device::GetInstance()->GetDeviceContext();
    return context->mHWBasedInstancing && context->mSupportsFloat16;
  }

  /**
   * Address: 0x0094D930 (FUN_0094D930)
   *
   * What it does:
   * Creates vertex format 15 on the active device. Unlike the D3D9 float16
   * formatter it never picks 16: `layoutVariant` is ignored.
   */
  boost::shared_ptr<VertexFormat> Float16HardwareVertexFormatterD3D10::CreateVertexFormat(
    const std::int32_t layoutVariant
  )
  {
    static_cast<void>(layoutVariant);
    return Device::GetInstance()->CreateVertexFormat(kFloat16VertexFormatToken);
  }

  /**
   * Address: 0x0094D7C0 (FUN_0094D7C0)
   *
   * What it does:
   * Returns float16 packed stride for the requested stream class.
   */
  std::uint32_t Float16HardwareVertexFormatterD3D10::GetVertexStride(
    const std::int32_t streamClass,
    const std::int32_t sizeVariant
  )
  {
    static_cast<void>(sizeVariant);
    return (streamClass != 0) ? kFloat16VertexStrideStream1 : kFloat16VertexStrideStream0;
  }

  /**
   * Address: 0x0094D7E0 (FUN_0094D7E0)
   *
   * What it does:
   * Packs the instance half of one mesh vertex into the format 15 instance
   * record (half-precision scalars). The body never looks at `streamClass`
   * or `writeVariant` - the D3D10 float16 formatter only ever writes the
   * instance stream.
   */
  void Float16HardwareVertexFormatterD3D10::WriteFormattedVertex(
    const std::int32_t streamClass,
    void* const destinationVertex,
    const MeshVertex& source,
    const std::int32_t writeVariant
  )
  {
    static_cast<void>(streamClass);
    static_cast<void>(writeVariant);

    auto& destination = *static_cast<Float16HardwareVertexInstance*>(destinationVertex);
    destination.instanceIndex = source.instanceIndex;
    destination.color = source.color;
    destination.shaderTime = ConvertFloatToHalf(source.shaderTime);
    static_cast<void>(CopyMatrix4x3Rows(
      destination.transform[0], destination.transform[1], destination.transform[2], destination.transform[3],
      source.transform
    ));
    destination.bonePaletteBase = source.bonePaletteBase;
    destination.secondaryDataMask = (source.useSecondaryData != 0U) ? static_cast<std::uint8_t>(0xFFU) : 0U;
    destination.scroll[1] = ConvertFloatToHalf(source.scroll[1]);
    destination.scroll[0] = ConvertFloatToHalf(source.scroll[0]);
    destination.dissolve = source.dissolve;
    destination.parameter = ConvertFloatToHalf(source.parameter);
    destination.meshColor = source.meshColor;
  }
} // namespace gpg::gal
