#include "gpg/gal/Matrix.h"

#include "platform/Platform.h"

// D3DX exists only on Windows. On other targets only `mul` is built, with a portable body: the sim
// reaches it (CEfxEmitter::InterpolatePosition, CEfxEmitter.cpp:443, for effects attached to a bone,
// from CEffectImpl::SetBone and CEffectManagerImpl::Tick); the other wrappers have no caller there.
#if defined(_WIN32)
extern "C"
{
  moho::VMatrix4* WINAPI D3DXMatrixMultiply(
    moho::VMatrix4* outMatrix,
    const moho::VMatrix4* lhs,
    const moho::VMatrix4* rhs
  );

  moho::VMatrix4* WINAPI D3DXMatrixInverse(
    moho::VMatrix4* outMatrix,
    float* determinantOut,
    const moho::VMatrix4* source
  );

  moho::VMatrix4* WINAPI D3DXMatrixTranslation(moho::VMatrix4* outMatrix, float x, float y, float z);
  moho::VMatrix4* WINAPI D3DXMatrixScaling(moho::VMatrix4* outMatrix, float x, float y, float z);
  moho::VMatrix4* WINAPI D3DXMatrixRotationX(moho::VMatrix4* outMatrix, float angle);
  moho::VMatrix4* WINAPI D3DXMatrixRotationY(moho::VMatrix4* outMatrix, float angle);
  moho::VMatrix4* WINAPI D3DXMatrixRotationZ(moho::VMatrix4* outMatrix, float angle);
  moho::VMatrix4* WINAPI D3DXMatrixRotationAxis(
    moho::VMatrix4* outMatrix,
    const Wm3::Vector3f* axis,
    float angle
  );
  moho::VMatrix4* WINAPI D3DXMatrixRotationQuaternion(
    moho::VMatrix4* outMatrix,
    const Wm3::Quaternionf* rotation
  );
}
#endif

namespace gpg::gal::Math
{
  /**
   * Address: 0x00940690 (FUN_00940690, ?mul@Math@gal@gpg@@YAPBUMatrix@23@PAU423@PBU423@1@Z)
   *
   * What it does:
   * Multiplies two matrices through the D3DX matrix-multiply lane and returns
   * `outMatrix`.
   */
  Matrix* mul(Matrix* const outMatrix, const Matrix* const lhs, const Matrix* const rhs)
  {
#if defined(_WIN32)
    D3DXMatrixMultiply(outMatrix, lhs, rhs);
#else
    // D3DXMatrixMultiply's contract: out = lhs * rhs over row vectors, out[i][j] = sum over k of
    // lhs[i][k] * rhs[k][j], and `outMatrix` may alias either input (hence the copies). Single
    // precision, summed k = 0..3 left to right, no fused multiply-add (-ffp-contract=off). Not
    // proven bit-exact against d3dx9's CPU-dispatched implementation (W5).
    const Matrix a = *lhs;
    const Matrix b = *rhs;
    for (int row = 0; row < 4; ++row) {
      const moho::Vector4f& l = a.r[row];
      moho::Vector4f& o = outMatrix->r[row];
      o.x = l.x * b.r[0].x + l.y * b.r[1].x + l.z * b.r[2].x + l.w * b.r[3].x;
      o.y = l.x * b.r[0].y + l.y * b.r[1].y + l.z * b.r[2].y + l.w * b.r[3].y;
      o.z = l.x * b.r[0].z + l.y * b.r[1].z + l.z * b.r[2].z + l.w * b.r[3].z;
      o.w = l.x * b.r[0].w + l.y * b.r[1].w + l.z * b.r[2].w + l.w * b.r[3].w;
    }
#endif
    return outMatrix;
  }

#if defined(_WIN32)
  /**
   * Address: 0x009406B0 (FUN_009406B0, ?invert@Math@gal@gpg@@YAPBUMatrix@23@PAU423@PBU423@@Z)
   *
   * What it does:
   * Computes matrix inverse through D3DX inverse lane and returns `outMatrix`.
   */
  Matrix* invert(Matrix* const outMatrix, const Matrix* const sourceMatrix)
  {
    D3DXMatrixInverse(outMatrix, nullptr, sourceMatrix);
    return outMatrix;
  }

  /**
   * Address: 0x00940770 (FUN_00940770, sub_940770)
   *
   * What it does:
   * Builds one translation matrix through the D3DX lane and returns
   * `outMatrix`.
   */
  Matrix* translation(Matrix* const outMatrix, const float x, const float y, const float z)
  {
    D3DXMatrixTranslation(outMatrix, x, y, z);
    return outMatrix;
  }

  /**
   * Address: 0x009407A0 (FUN_009407A0, sub_9407A0)
   *
   * What it does:
   * Builds one scale matrix through the D3DX lane and returns `outMatrix`.
   */
  Matrix* scaling(Matrix* const outMatrix, const float x, const float y, const float z)
  {
    D3DXMatrixScaling(outMatrix, x, y, z);
    return outMatrix;
  }

  /**
   * Address: 0x009406D0 (FUN_009406D0, ?rotationAxisX@Math@gal@gpg@@YAPBUMatrix@23@PAU423@M@Z)
   *
   * What it does:
   * Builds an X-axis rotation matrix through the D3DX lane and returns
   * `outMatrix`.
   */
  Matrix* rotationAxisX(Matrix* const outMatrix, const float angle)
  {
    D3DXMatrixRotationX(outMatrix, angle);
    return outMatrix;
  }

  /**
   * Address: 0x009406F0 (FUN_009406F0, ?rotationAxisY@Math@gal@gpg@@YAPBUMatrix@23@PAU423@M@Z)
   *
   * What it does:
   * Builds a Y-axis rotation matrix through the D3DX lane and returns
   * `outMatrix`.
   */
  Matrix* rotationAxisY(Matrix* const outMatrix, const float angle)
  {
    D3DXMatrixRotationY(outMatrix, angle);
    return outMatrix;
  }

  /**
   * Address: 0x00940710 (FUN_00940710, ?rotationAxisZ@Math@gal@gpg@@YAPBUMatrix@23@PAU423@M@Z)
   *
   * What it does:
   * Builds a Z-axis rotation matrix through the D3DX lane and returns
   * `outMatrix`.
   */
  Matrix* rotationAxisZ(Matrix* const outMatrix, const float angle)
  {
    D3DXMatrixRotationZ(outMatrix, angle);
    return outMatrix;
  }

  /**
   * Address: 0x0089C830 (FUN_0089C830)
   *
   * What it does:
   * Register-shape wrapper that forwards to `rotationAxisX` and returns
   * `outMatrix`.
   */
  [[maybe_unused]] Matrix* RotationAxisXRegisterAdapter(Matrix* const outMatrix, const float angle)
  {
    (void)rotationAxisX(outMatrix, angle);
    return outMatrix;
  }

  /**
   * Address: 0x0089C850 (FUN_0089C850)
   *
   * What it does:
   * Register-shape wrapper that forwards to `rotationAxisY` and returns
   * `outMatrix`.
   */
  [[maybe_unused]] Matrix* RotationAxisYRegisterAdapter(Matrix* const outMatrix, const float angle)
  {
    (void)rotationAxisY(outMatrix, angle);
    return outMatrix;
  }

  /**
   * Address: 0x0089C870 (FUN_0089C870)
   *
   * What it does:
   * Register-shape wrapper that forwards to `rotationAxisZ` and returns
   * `outMatrix`.
   */
  [[maybe_unused]] Matrix* RotationAxisZRegisterAdapter(Matrix* const outMatrix, const float angle)
  {
    (void)rotationAxisZ(outMatrix, angle);
    return outMatrix;
  }

  /**
   * Address: 0x00940730 (FUN_00940730, ?rotationAxis@Math@gal@gpg@@YAPBUMatrix@23@PAU423@PBUVector3@23@M@Z)
   *
   * What it does:
   * Builds one axis-angle rotation matrix through the D3DX lane and returns
   * `outMatrix`.
   */
  Matrix* rotationAxis(Matrix* const outMatrix, const Wm3::Vector3f* const axis, const float angle)
  {
    D3DXMatrixRotationAxis(outMatrix, axis, angle);
    return outMatrix;
  }

  /**
   * Address: 0x00940750 (FUN_00940750, ?rotationQuaternion@Math@gal@gpg@@YAPBUMatrix@23@PAU423@PBUQuaternion@23@@Z)
   *
   * What it does:
   * Builds one quaternion rotation matrix through the D3DX lane and returns
   * `outMatrix`.
   */
  Matrix* rotationQuaternion(Matrix* const outMatrix, const Wm3::Quaternionf* const rotation)
  {
    D3DXMatrixRotationQuaternion(outMatrix, rotation);
    return outMatrix;
  }
#endif
} // namespace gpg::gal::Math
