// The audio platform of the Android client build: none yet. The game runs without sound, as on a
// Windows PC without the XACT runtime and without a sound device (CLIENT_PLAN.md: C3 brings
// FAudio's FACT, a reimplementation of XACT 3). What the engine names of it:
//
// - DirectSoundCreate (include/win32/dsound.h): no device, DSERR_NODRIVER;
// - the XACT engine: one that plays nothing (XactEngineAndroid.cpp), so the sound functions and
//   the options work;
// - X3DAudio: nothing to place, as nothing plays;
// - the XACT clock (moho/audio/XactClock.cpp, left out of this build): there is no XACT DLL to
//   patch.

#include <dsound.h>

#include <cstdint>

#include "moho/audio/AudioEngine.h"
#include "moho/audio/XactClock.h"

HRESULT DirectSoundCreate(const GUID*, IDirectSound** const directSound, IUnknown*)
{
  if (directSound != nullptr) {
    *directSound = nullptr;
  }
  return DSERR_NODRIVER;
}

// Declared by moho/audio/AudioEngine.cpp (X3DAudio.lib on Windows).
extern "C" void X3DAudioCalculate(
  const void*, const moho::Audio3DListener*, const moho::Audio3DEmitter*, std::uint32_t, moho::Audio3DDspSettings*
)
{}

extern "C" void X3DAudioInitialize(std::uint32_t, float, void*) {}

namespace moho
{
  // The Windows build patches the XACT engine DLL's clock imports (XactClock.cpp); the engine here
  // is no DLL and has no clock.
  void SND_InstallXactClock(const IXACTEngine&) {}
} // namespace moho
