// What the engine's audio needs beside the XACT engine (XactEngineAndroid.cpp):
//
// - DirectSoundCreate (include/win32/dsound.h): no device, DSERR_NODRIVER, so movies have no sound;
// - X3DAudio: F3DAudio, FAudio's reimplementation of it (cmake/FafAndroidFAudio.cmake);
// - the XACT clock (moho/audio/XactClock.cpp, left out of this build): there is no XACT DLL to
//   patch.

#include <dsound.h>

#include <F3DAudio.h>

#include <cstdint>

#include "moho/audio/AudioEngine.h"
#include "moho/audio/XactClock.h"

namespace
{
  // What Audio3DEmitter's curve pointers hold: AudioEngine.cpp's AudioDistanceCurve, in the layout
  // that X3DAudio's own has on Windows. F3DAudio's is packed.
  struct EngineCurvePoint
  {
    float distance;
    float setting;
  };

  struct EngineCurve
  {
    const EngineCurvePoint* points;
    std::uint32_t pointCount;
  };

  static_assert(sizeof(EngineCurvePoint) == sizeof(F3DAUDIO_DISTANCE_CURVE_POINT), "both are a pair of floats");
  static_assert(sizeof(moho::AudioEngineImpl::mAudioHandle) == F3DAUDIO_HANDLE_BYTESIZE, "X3DAudio's instance is F3DAudio's");

  [[nodiscard]] F3DAUDIO_VECTOR ToF3d(const moho::Audio3DVector& vector)
  {
    return F3DAUDIO_VECTOR{vector.x, vector.y, vector.z};
  }

  /// F3DAudio's curve in `storage`, for one of the emitter's; null for one it has not.
  [[nodiscard]] F3DAUDIO_DISTANCE_CURVE* ToF3d(const void* const engineCurve, F3DAUDIO_DISTANCE_CURVE& storage)
  {
    if (engineCurve == nullptr) {
      return nullptr;
    }
    const auto& curve = *static_cast<const EngineCurve*>(engineCurve);
    storage.pPoints = reinterpret_cast<F3DAUDIO_DISTANCE_CURVE_POINT*>(const_cast<EngineCurvePoint*>(curve.points));
    storage.PointCount = curve.pointCount;
    return &storage;
  }
} // namespace

HRESULT DirectSoundCreate(const GUID*, IDirectSound** const directSound, IUnknown*)
{
  if (directSound != nullptr) {
    *directSound = nullptr;
  }
  return DSERR_NODRIVER;
}

// Declared by moho/audio/AudioEngine.cpp (X3DAudio.lib on Windows).
extern "C" void X3DAudioInitialize(const std::uint32_t speakerChannelMask, const float speedOfSound, void* const instance)
{
  F3DAudioInitialize(speakerChannelMask, speedOfSound, static_cast<std::uint8_t*>(instance));
}

extern "C" void X3DAudioCalculate(
  const void* const instance,
  const moho::Audio3DListener* const listener,
  const moho::Audio3DEmitter* const emitter,
  const std::uint32_t flags,
  moho::Audio3DDspSettings* const settings
)
{
  F3DAUDIO_LISTENER f3dListener{};
  f3dListener.OrientFront = ToF3d(listener->mOrientFront);
  f3dListener.OrientTop = ToF3d(listener->mOrientTop);
  f3dListener.Position = ToF3d(listener->mPosition);
  f3dListener.Velocity = ToF3d(listener->mVelocity);
  // A cone is eight floats on both sides.
  f3dListener.pCone = static_cast<F3DAUDIO_CONE*>(listener->mCone);

  F3DAUDIO_DISTANCE_CURVE volumeCurve{};
  F3DAUDIO_DISTANCE_CURVE lfeCurve{};
  F3DAUDIO_DISTANCE_CURVE lpfDirectCurve{};
  F3DAUDIO_DISTANCE_CURVE lpfReverbCurve{};
  F3DAUDIO_DISTANCE_CURVE reverbCurve{};

  F3DAUDIO_EMITTER f3dEmitter{};
  f3dEmitter.pCone = static_cast<F3DAUDIO_CONE*>(emitter->mCone);
  f3dEmitter.OrientFront = ToF3d(emitter->mOrientFront);
  f3dEmitter.OrientTop = ToF3d(emitter->mOrientTop);
  f3dEmitter.Position = ToF3d(emitter->mPosition);
  f3dEmitter.Velocity = ToF3d(emitter->mVelocity);
  f3dEmitter.InnerRadius = emitter->mInnerRadius;
  f3dEmitter.InnerRadiusAngle = emitter->mInnerRadiusAngle;
  f3dEmitter.ChannelCount = emitter->mChannelCount;
  f3dEmitter.ChannelRadius = emitter->mChannelRadius;
  f3dEmitter.pChannelAzimuths = emitter->mChannelAzimuths;
  f3dEmitter.pVolumeCurve = ToF3d(emitter->mVolumeCurve, volumeCurve);
  f3dEmitter.pLFECurve = ToF3d(emitter->mLfeCurve, lfeCurve);
  f3dEmitter.pLPFDirectCurve = ToF3d(emitter->mLpfDirectCurve, lpfDirectCurve);
  f3dEmitter.pLPFReverbCurve = ToF3d(emitter->mLpfReverbCurve, lpfReverbCurve);
  f3dEmitter.pReverbCurve = ToF3d(emitter->mReverbCurve, reverbCurve);
  f3dEmitter.CurveDistanceScaler = emitter->mCurveDistanceScaler;
  f3dEmitter.DopplerScaler = emitter->mDopplerScaler;

  F3DAUDIO_DSP_SETTINGS f3dSettings{};
  f3dSettings.pMatrixCoefficients = settings->mMatrixCoefficients;
  f3dSettings.pDelayTimes = settings->mDelayTimes;
  f3dSettings.SrcChannelCount = settings->mSrcChannelCount;
  f3dSettings.DstChannelCount = settings->mDstChannelCount;
  f3dSettings.LPFDirectCoefficient = settings->mLpfDirectCoefficient;
  f3dSettings.LPFReverbCoefficient = settings->mLpfReverbCoefficient;
  f3dSettings.ReverbLevel = settings->mReverbLevel;
  f3dSettings.DopplerFactor = settings->mDopplerFactor;
  f3dSettings.EmitterToListenerAngle = settings->mEmitterToListenerAngle;
  f3dSettings.EmitterToListenerDistance = settings->mEmitterToListenerDistance;
  f3dSettings.EmitterVelocityComponent = settings->mEmitterVelocityComponent;
  f3dSettings.ListenerVelocityComponent = settings->mListenerVelocityComponent;

  F3DAudioCalculate(static_cast<const std::uint8_t*>(instance), &f3dListener, &f3dEmitter, flags, &f3dSettings);

  settings->mLpfDirectCoefficient = f3dSettings.LPFDirectCoefficient;
  settings->mLpfReverbCoefficient = f3dSettings.LPFReverbCoefficient;
  settings->mReverbLevel = f3dSettings.ReverbLevel;
  settings->mDopplerFactor = f3dSettings.DopplerFactor;
  settings->mEmitterToListenerAngle = f3dSettings.EmitterToListenerAngle;
  settings->mEmitterToListenerDistance = f3dSettings.EmitterToListenerDistance;
  settings->mEmitterVelocityComponent = f3dSettings.EmitterVelocityComponent;
  settings->mListenerVelocityComponent = f3dSettings.ListenerVelocityComponent;
}

namespace moho
{
  // The Windows build patches the XACT engine DLL's clock imports (XactClock.cpp); the engine here
  // is no DLL and has no clock.
  void SND_InstallXactClock(const IXACTEngine&) {}
} // namespace moho
