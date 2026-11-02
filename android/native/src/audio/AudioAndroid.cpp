// The audio platform of the Android client build: none yet. The game runs without sound, as on a
// Windows PC without the XACT runtime and without a sound device (CLIENT_PLAN.md: C3 brings
// FAudio's FACT, a reimplementation of XACT 3). What the engine names of it:
//
// - DirectSoundCreate (include/win32/dsound.h): no device, DSERR_NODRIVER;
// - the XACT engine: CoCreateInstance finds no class (src/win32/Win32Android.cpp);
// - X3DAudio and the XACT clock (moho/audio/XactClock.cpp, left out of this build): only an XACT
//   engine leads to them, so they are never reached here.

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
  // The Windows build patches the XACT engine's clock imports (XactClock.cpp); there is no XACT
  // engine here to patch.
  void SND_InstallXactClock(const IXACTEngine&) {}
} // namespace moho
