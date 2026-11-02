#pragma once

// The Android client build's <mmreg.h>: the engine's POSIX one (src/platform/posix/win32), and the
// extensible wave format the audio engine asks XACT's final mix for, with the SDK's layout (packed
// to 1 byte, 40 bytes).

#include_next <mmreg.h>

#include <mmsystem.h>

#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

#pragma pack(push, 1)
struct WAVEFORMATEXTENSIBLE
{
  WAVEFORMATEX Format;
  union
  {
    WORD wValidBitsPerSample;
    WORD wSamplesPerBlock;
    WORD wReserved;
  } Samples;
  DWORD dwChannelMask;
  GUID SubFormat;
};
#pragma pack(pop)
static_assert(sizeof(WAVEFORMATEXTENSIBLE) == 40, "WAVEFORMATEXTENSIBLE is the SDK's 40 bytes");
