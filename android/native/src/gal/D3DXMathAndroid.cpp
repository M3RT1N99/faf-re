// D3DXMatrixLookAtRH for the Android client build, where there is no D3DX: the one D3DX math
// function that shared client code calls itself (moho/render/Shadow.cpp, the shadow's light
// camera), declared there with the engine's types, which have D3DX's layouts.
//
// It computes as D3DX9_43 (x64, the DLL the Windows build links) does, operation for operation,
// with D3DX's C routine for the two normalizations: the one D3DX runs without its CPU-specific
// code (DisableD3DXPSGP), as gal's matrix math on Android is D3DX's without its SSE code
// (gpg/gal/MatrixPortable.cpp). On an Intel CPU D3DX normalizes with rsqrtss and a Newton step
// instead, and on other CPUs with an SSE routine of its own; those differ from this in the last
// bits (on Intel, in 69 % of random matrices).
//
// Checked against the DLL's C routine for 50 million random inputs: the same bits, but for the
// sign of the NaNs that infinite and NaN inputs give, which follows the instructions' operand
// order (and differs between x86 and ARM anyway).

#include <Windows.h>

#include <cmath>

#include "Wm3Vector3.h"
#include "moho/math/VMatrix4.h"

namespace
{
  /// D3DXVec3Normalize's C routine: a vector within 2^-23 of unit length stays as it is, one of
  /// a length squared no more than FLT_MIN (or NaN) becomes zero.
  Wm3::Vector3f Normalize(const Wm3::Vector3f& v)
  {
    const float lengthSquared = v.x * v.x + v.y * v.y + v.z * v.z;
    const float offUnit = lengthSquared - 1.0f;
    if (offUnit >= -1.1920929e-07f && offUnit <= 1.1920929e-07f) {
      return v;
    }
    if (lengthSquared > 1.17549435e-38f) {
      const float scale = 1.0f / std::sqrt(lengthSquared);
      return Wm3::Vector3f(v.x * scale, v.y * scale, v.z * scale);
    }
    return Wm3::Vector3f(0.0f, 0.0f, 0.0f);
  }

  void SetRow(moho::Vector4f& row, const float x, const float y, const float z, const float w)
  {
    row.x = x;
    row.y = y;
    row.z = z;
    row.w = w;
  }
} // namespace

/// The right-handed view matrix from `eye` towards `at`: z = normalize(eye - at),
/// x = normalize(up x z), y = z x x, and the translation -(x.eye, y.eye, z.eye).
extern "C" moho::VMatrix4* WINAPI D3DXMatrixLookAtRH(
  moho::VMatrix4* const out, const Wm3::Vector3f* const eye, const Wm3::Vector3f* const at, const Wm3::Vector3f* const up
)
{
  const Wm3::Vector3f z = Normalize(Wm3::Vector3f(eye->x - at->x, eye->y - at->y, eye->z - at->z));
  const Wm3::Vector3f x =
    Normalize(Wm3::Vector3f(up->y * z.z - up->z * z.y, up->z * z.x - up->x * z.z, up->x * z.y - up->y * z.x));
  const Wm3::Vector3f y(z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x);

  SetRow(out->r[0], x.x, y.x, z.x, 0.0f);
  SetRow(out->r[1], x.y, y.y, z.y, 0.0f);
  SetRow(out->r[2], x.z, y.z, z.z, 0.0f);
  SetRow(
    out->r[3], -(x.x * eye->x + x.y * eye->y + x.z * eye->z), -(y.x * eye->x + y.y * eye->y + y.z * eye->z),
    -(z.x * eye->x + z.y * eye->y + z.z * eye->z), 1.0f
  );
  return out;
}
