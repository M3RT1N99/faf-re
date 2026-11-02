#pragma once

// The Android client build's <dsound.h>: the engine's POSIX one (src/platform/posix/win32), and
// the DirectSound the audio engine (the speaker setup) and the movies' sound sink use. There is no
// DirectSound on Android: DirectSoundCreate fails with DSERR_NODRIVER, as on a Windows PC without
// a sound device, and the engine takes its default speaker setup and plays movies without sound
// (src/audio/AudioAndroid.cpp). The interfaces have the SDK's methods in the SDK's order.

#include_next <dsound.h>

#include <Windows.h>
#include <mmsystem.h>

struct IDirectSound;
struct IDirectSoundBuffer;
using LPDIRECTSOUND = IDirectSound*;
using LPDIRECTSOUNDBUFFER = IDirectSoundBuffer*;

struct DSCAPS;
struct DSBCAPS;

struct DSBUFFERDESC
{
  DWORD dwSize;
  DWORD dwFlags;
  DWORD dwBufferBytes;
  DWORD dwReserved;
  WAVEFORMATEX* lpwfxFormat;
  GUID guid3DAlgorithm;
};

#define DSBCAPS_CTRLVOLUME 0x00000080u
#define DSBCAPS_GLOBALFOCUS 0x00008000u
#define DSBCAPS_GETCURRENTPOSITION2 0x00010000u
#define DSBPLAY_LOOPING 0x00000001u
#define DSBVOLUME_MIN (-10000)
#define DSBVOLUME_MAX 0
#define DSSCL_NORMAL 0x00000001u
#define DSSCL_PRIORITY 0x00000002u
#define DSSCL_EXCLUSIVE 0x00000003u
#define DSSCL_WRITEPRIMARY 0x00000004u
#define DSERR_NODRIVER (static_cast<HRESULT>(0x88780078u))

struct IDirectSound : IUnknown
{
  virtual HRESULT CreateSoundBuffer(const DSBUFFERDESC* description, IDirectSoundBuffer** buffer, IUnknown* outer) = 0;
  virtual HRESULT GetCaps(DSCAPS* caps) = 0;
  virtual HRESULT DuplicateSoundBuffer(IDirectSoundBuffer* original, IDirectSoundBuffer** duplicate) = 0;
  virtual HRESULT SetCooperativeLevel(HWND window, DWORD level) = 0;
  virtual HRESULT Compact() = 0;
  virtual HRESULT GetSpeakerConfig(DWORD* speakerConfig) = 0;
  virtual HRESULT SetSpeakerConfig(DWORD speakerConfig) = 0;
  virtual HRESULT Initialize(const GUID* device) = 0;
};

struct IDirectSoundBuffer : IUnknown
{
  virtual HRESULT GetCaps(DSBCAPS* caps) = 0;
  virtual HRESULT GetCurrentPosition(DWORD* playCursor, DWORD* writeCursor) = 0;
  virtual HRESULT GetFormat(WAVEFORMATEX* format, DWORD sizeAllocated, DWORD* sizeWritten) = 0;
  virtual HRESULT GetVolume(LONG* volume) = 0;
  virtual HRESULT GetPan(LONG* pan) = 0;
  virtual HRESULT GetFrequency(DWORD* frequency) = 0;
  virtual HRESULT GetStatus(DWORD* status) = 0;
  virtual HRESULT Initialize(IDirectSound* device, const DSBUFFERDESC* description) = 0;
  virtual HRESULT Lock(DWORD offset, DWORD bytes, void** audio1, DWORD* bytes1, void** audio2, DWORD* bytes2, DWORD flags) = 0;
  virtual HRESULT Play(DWORD reserved, DWORD priority, DWORD flags) = 0;
  virtual HRESULT SetCurrentPosition(DWORD position) = 0;
  virtual HRESULT SetFormat(const WAVEFORMATEX* format) = 0;
  virtual HRESULT SetVolume(LONG volume) = 0;
  virtual HRESULT SetPan(LONG pan) = 0;
  virtual HRESULT SetFrequency(DWORD frequency) = 0;
  virtual HRESULT Stop() = 0;
  virtual HRESULT Unlock(void* audio1, DWORD bytes1, void* audio2, DWORD bytes2) = 0;
  virtual HRESULT Restore() = 0;
};

/// Fails with DSERR_NODRIVER: there is no DirectSound on Android.
HRESULT DirectSoundCreate(const GUID* device, IDirectSound** directSound, IUnknown* outer);
