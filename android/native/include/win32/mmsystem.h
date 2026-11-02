#pragma once

// The Android client build's <mmsystem.h>: the engine's POSIX one (src/platform/posix/win32), and
// the wave format the audio engine reads, with the SDK's layout (packed to 1 byte, 18 bytes).

#include_next <mmsystem.h>

#include <Windows.h>

#define WAVE_FORMAT_PCM 1

// The timer resolution: the kernel's timers are finer than a millisecond already, so a request for
// one succeeds and changes nothing.
#define TIMERR_NOERROR 0u
#define TIMERR_NOCANDO 97u
inline UINT timeBeginPeriod(UINT) noexcept
{
  return TIMERR_NOERROR;
}
inline UINT timeEndPeriod(UINT) noexcept
{
  return TIMERR_NOERROR;
}

#pragma pack(push, 1)
struct WAVEFORMATEX
{
  WORD wFormatTag;
  WORD nChannels;
  DWORD nSamplesPerSec;
  DWORD nAvgBytesPerSec;
  WORD nBlockAlign;
  WORD wBitsPerSample;
  WORD cbSize;
};
#pragma pack(pop)
static_assert(sizeof(WAVEFORMATEX) == 18, "WAVEFORMATEX is the SDK's 18 bytes");
