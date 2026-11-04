#pragma once

// <xact3.h> for the Android client build. The engine brings its own XACT interfaces
// (moho/audio/AudioEngine.h) and takes from the SDK header only the data the XACT engine and the
// game exchange: the runtime parameters, the notification structures, the streaming parameters.
// Their layouts are the June 2010 SDK's: the notifications packed to 1 byte, the rest at the
// default alignment. AudioEngine.h checks the sizes and offsets it relies on. The XACT engine is
// src/audio/XactEngineAndroid.cpp.

#include <Windows.h>

#include <cstdint>

using XACTINDEX = WORD;
using XACTNOTIFICATIONTYPE = BYTE;
using XACTVARIABLEINDEX = XACTINDEX;
using XACTVARIABLEVALUE = float;

struct IXACT3SoundBank;
struct IXACT3WaveBank;
struct IXACT3Cue;
struct IXACT3Wave;
struct IXAudio2;
struct IXAudio2MasteringVoice;
struct XACT_NOTIFICATION;

using XACT_READFILE_CALLBACK = BOOL (*)(HANDLE file, LPVOID buffer, DWORD bytesToRead, DWORD* bytesRead, OVERLAPPED* overlapped);
using XACT_GETOVERLAPPEDRESULT_CALLBACK = BOOL (*)(HANDLE file, OVERLAPPED* overlapped, DWORD* bytesTransferred, BOOL wait);

struct XACT_FILEIO_CALLBACKS
{
  XACT_READFILE_CALLBACK readFileCallback;
  XACT_GETOVERLAPPEDRESULT_CALLBACK getOverlappedResultCallback;
};

using XACT_NOTIFICATION_CALLBACK = void (*)(const XACT_NOTIFICATION* notification);

struct XACT_RUNTIME_PARAMETERS
{
  DWORD lookAheadTime;
  void* pGlobalSettingsBuffer;
  DWORD globalSettingsBufferSize;
  DWORD globalSettingsFlags;
  DWORD globalSettingsAllocAttributes;
  XACT_FILEIO_CALLBACKS fileIOCallbacks;
  XACT_NOTIFICATION_CALLBACK fnNotificationCallback;
  wchar_t* pRendererID;
  IXAudio2* pXAudio2;
  IXAudio2MasteringVoice* pMasteringVoice;
};

struct XACT_STREAMING_PARAMETERS
{
  HANDLE file;
  DWORD offset;
  DWORD flags;
  WORD packetSize;
};
using XACT_WAVEBANK_STREAMING_PARAMETERS = XACT_STREAMING_PARAMETERS;

static constexpr XACTNOTIFICATIONTYPE XACTNOTIFICATIONTYPE_WAVEBANKPREPARED = 17;

#pragma pack(push, 1)
struct XACT_NOTIFICATION_DESCRIPTION
{
  XACTNOTIFICATIONTYPE type;
  BYTE flags;
  IXACT3SoundBank* pSoundBank;
  IXACT3WaveBank* pWaveBank;
  IXACT3Cue* pCue;
  IXACT3Wave* pWave;
  XACTINDEX cueIndex;
  XACTINDEX waveIndex;
  void* pvContext;
};

struct XACT_NOTIFICATION_CUE
{
  XACTINDEX cueIndex;
  IXACT3SoundBank* pSoundBank;
  IXACT3Cue* pCue;
};

struct XACT_NOTIFICATION_MARKER
{
  XACTINDEX cueIndex;
  IXACT3SoundBank* pSoundBank;
  IXACT3Cue* pCue;
  DWORD marker;
};

struct XACT_NOTIFICATION_SOUNDBANK
{
  IXACT3SoundBank* pSoundBank;
};

struct XACT_NOTIFICATION_WAVEBANK
{
  IXACT3WaveBank* pWaveBank;
};

struct XACT_NOTIFICATION_VARIABLE
{
  XACTINDEX cueIndex;
  IXACT3SoundBank* pSoundBank;
  IXACT3Cue* pCue;
  XACTVARIABLEINDEX variableIndex;
  XACTVARIABLEVALUE variableValue;
  BOOL local;
};

struct XACT_NOTIFICATION_GUI
{
  DWORD reserved;
};

struct XACT_NOTIFICATION_WAVE
{
  IXACT3WaveBank* pWaveBank;
  XACTINDEX waveIndex;
  XACTINDEX cueIndex;
  IXACT3SoundBank* pSoundBank;
  IXACT3Cue* pCue;
  IXACT3Wave* pWave;
};

struct XACT_NOTIFICATION
{
  XACTNOTIFICATIONTYPE type;
  LONG timeStamp;
  void* pvContext;
  union
  {
    XACT_NOTIFICATION_CUE cue;
    XACT_NOTIFICATION_MARKER marker;
    XACT_NOTIFICATION_SOUNDBANK soundBank;
    XACT_NOTIFICATION_WAVEBANK waveBank;
    XACT_NOTIFICATION_VARIABLE variable;
    XACT_NOTIFICATION_GUI gui;
    XACT_NOTIFICATION_WAVE wave;
  };
};
#pragma pack(pop)
