// The XACT engine of the Android client build: FACT, FAudio's reimplementation of XACT 3
// (cmake/FafAndroidFAudio.cmake), behind the engine's own XACT interfaces (moho/audio/AudioEngine.h).
// FAudioPlatformAndroid.c plays what it mixes. CoCreateInstance (src/win32/Win32Android.cpp) creates
// it through FafAndroid_CreateClientInstance for XACT's three class IDs.
//
// Each interface holds its FACT object, and each method is FACT's function of that name. The
// engine's structures are XACT's and FACT's its own, so they are copied field by field. Where FACT
// has nothing, the method answers as an engine without the feature would:
//
// - PrepareWave, PrepareInMemoryWave, PrepareStreamingWave: TODOs in FACT that hand back no wave.
//   They fail here; the engine never calls them.
// - QueryInterface: FACT has no COM. There is the one interface.
// - The sound bank's and cue's reserved slots: the engine never calls them.

#include <Windows.h>
#include <mmreg.h>

#include <FACT.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "FAudioPlatformAndroid.h"
#include "gpg/core/utils/Logging.h"
#include "moho/audio/AudioEngine.h"

namespace
{
  // The retail, debug and audition engines' class IDs (moho/audio/AudioEngine.cpp).
  constexpr GUID kXactClasses[] = {
    {0x343e68e6u, 0x8f82u, 0x4a8du, {0xa2u, 0xdau, 0x6eu, 0x9au, 0x94u, 0x4bu, 0x37u, 0x8cu}},
    {0x3cbb606bu, 0x06f1u, 0x473eu, {0x9du, 0xd5u, 0x0eu, 0x4au, 0x3bu, 0x47u, 0x14u, 0x13u}},
    {0xcedde475u, 0x50b5u, 0x47efu, {0x91u, 0xa7u, 0x3bu, 0x49u, 0xa0u, 0xe8u, 0xe5u, 0x88u}},
  };

  // XACTENGINE_E_NOGLOBALSETTINGS (moho/audio/AudioEngine.cpp has its text).
  constexpr std::int32_t kNoGlobalSettings = static_cast<std::int32_t>(0x8AC70009u);

  [[maybe_unused]] const bool gLogHandlerSet = [] {
    FAudioAndroid_SetLogHandler([](const char* const line) { gpg::Logf("FAudio: %s", line); });
    return true;
  }();

  // FACT's codes are HRESULTs in an unsigned type.
  [[nodiscard]] constexpr bool Failed(const std::uint32_t result)
  {
    return static_cast<std::int32_t>(result) < 0;
  }

  // --- Notifications ----------------------------------------------------------------------------
  // FACT's callback gets FACT's objects and has no context of ours, so the way back to the engine's
  // wave banks and its callback is process-wide. The engine gives all its instances one callback, and
  // registers for the wave banks' and the GUI's notifications alone (moho/audio/AudioEngine.cpp).

  std::mutex gInterfacesMutex;
  std::unordered_map<const void*, void*> gInterfaces; // FACT's object: the interface over it
  std::atomic<XACT_NOTIFICATION_CALLBACK> gNotificationCallback{nullptr};

  void BindInterface(const void* const fact, void* const engineInterface)
  {
    const std::lock_guard<std::mutex> lock(gInterfacesMutex);
    gInterfaces[fact] = engineInterface;
  }

  void UnbindInterface(const void* const fact)
  {
    const std::lock_guard<std::mutex> lock(gInterfacesMutex);
    gInterfaces.erase(fact);
  }

  template <class Opaque>
  [[nodiscard]] Opaque* InterfaceOver(const void* const fact)
  {
    const std::lock_guard<std::mutex> lock(gInterfacesMutex);
    const auto found = gInterfaces.find(fact);
    return (found != gInterfaces.end()) ? static_cast<Opaque*>(found->second) : nullptr;
  }

  void HandleFactNotification(const FACTNotification* const fact)
  {
    XACT_NOTIFICATION notification{};
    notification.type = fact->type;
    notification.timeStamp = fact->timeStamp;
    notification.pvContext = fact->pvContext;
    switch (fact->type) {
    case FACTNOTIFICATIONTYPE_WAVEBANKDESTROYED:
    case FACTNOTIFICATIONTYPE_WAVEBANKPREPARED:
    case FACTNOTIFICATIONTYPE_WAVEBANKSTREAMING_INVALIDCONTENT:
      notification.waveBank.pWaveBank = InterfaceOver<IXACT3WaveBank>(fact->waveBank.pWaveBank);
      break;
    case FACTNOTIFICATIONTYPE_GUICONNECTED:
    case FACTNOTIFICATIONTYPE_GUIDISCONNECTED:
      notification.gui.reserved = fact->gui.reserved;
      break;
    default:
      break;
    }
    if (const XACT_NOTIFICATION_CALLBACK callback = gNotificationCallback.load(); callback != nullptr) {
      callback(&notification);
    }
  }

  // --- The interfaces ---------------------------------------------------------------------------

  class FactCue final : public moho::IXACTCue
  {
  public:
    explicit FactCue(FACTCue* const cue)
      : mCue(cue)
    {}

    [[nodiscard]] FACTCue* Fact() const
    {
      return mCue;
    }

    std::int32_t __stdcall Play() override
    {
      return static_cast<std::int32_t>(FACTCue_Play(mCue));
    }

    void __stdcall Stop(const std::int32_t stopFlags) override
    {
      FACTCue_Stop(mCue, static_cast<std::uint32_t>(stopFlags));
    }

    // XACT's cue has one state, and the engine compares with it. FACT's are flags that stay on:
    // PREPARED on a cue stopped before it played, PLAYING on one that fades out.
    std::int32_t __stdcall GetState(std::int32_t* const outState) override
    {
      std::uint32_t state = 0U;
      const std::uint32_t result = FACTCue_GetState(mCue, &state);
      if ((state & FACT_STATE_STOPPED) != 0U) {
        state = FACT_STATE_STOPPED;
      } else if ((state & FACT_STATE_STOPPING) != 0U) {
        state = FACT_STATE_STOPPING;
      }
      if (outState != nullptr) {
        *outState = static_cast<std::int32_t>(state);
      }
      return static_cast<std::int32_t>(result);
    }

    std::int32_t __stdcall Destroy() override
    {
      const std::uint32_t result = FACTCue_Destroy(mCue);
      delete this;
      return static_cast<std::int32_t>(result);
    }

    void __stdcall Reserved10() override {}
    void __stdcall Reserved14() override {}
    void __stdcall Reserved18() override {}
    void __stdcall Reserved1C() override {}

    std::int32_t __stdcall SetMatrixCoefficients(
      const std::uint32_t srcChannelCount, const std::uint32_t dstChannelCount, const float* const matrixCoefficients
    ) override
    {
      return static_cast<std::int32_t>(
        FACTCue_SetMatrixCoefficients(mCue, srcChannelCount, dstChannelCount, const_cast<float*>(matrixCoefficients))
      );
    }

    std::uint16_t __stdcall GetVariableIndex(const char* const variableName) override
    {
      return FACTCue_GetVariableIndex(mCue, variableName);
    }

    std::int32_t __stdcall SetVariable(const std::uint16_t variableIndex, const float value) override
    {
      return static_cast<std::int32_t>(FACTCue_SetVariable(mCue, variableIndex, value));
    }

  private:
    ~FactCue() = default;

    FACTCue* mCue;
  };

  class FactSoundBank final : public moho::IXACTSoundBank
  {
  public:
    explicit FactSoundBank(FACTSoundBank* const bank)
      : mBank(bank)
    {}

    [[nodiscard]] FACTSoundBank* Fact() const
    {
      return mBank;
    }

    std::uint16_t __stdcall GetCueIndex(const char* const cueName) override
    {
      return FACTSoundBank_GetCueIndex(mBank, cueName);
    }

    std::int32_t __stdcall Reserved04() override
    {
      return E_NOTIMPL;
    }

    std::int32_t __stdcall Reserved08() override
    {
      return E_NOTIMPL;
    }

    std::int32_t __stdcall Prepare(
      const std::uint16_t cueIndex, const std::uint32_t flags, const std::uint32_t timeOffset, moho::IXACTCue** const outCue
    ) override
    {
      if (outCue == nullptr) {
        return E_INVALIDARG;
      }
      *outCue = nullptr;
      FACTCue* cue = nullptr;
      const std::uint32_t result = FACTSoundBank_Prepare(mBank, cueIndex, flags, static_cast<std::int32_t>(timeOffset), &cue);
      if (!Failed(result)) {
        *outCue = new FactCue(cue);
      }
      return static_cast<std::int32_t>(result);
    }

    // Without a cue to hand back, FACT owns the cue and destroys it when it ends. With one this is
    // not FACTSoundBank_Play: that drops what FACTCue_Play says about the instance limit, and a cue
    // that cannot play is not handed out.
    std::int32_t __stdcall Play(
      const std::uint16_t cueIndex, const std::uint32_t flags, const std::uint32_t timeOffset, moho::IXACTCue** const outCue
    ) override
    {
      if (outCue == nullptr) {
        return static_cast<std::int32_t>(FACTSoundBank_Play(mBank, cueIndex, flags, static_cast<std::int32_t>(timeOffset), nullptr));
      }
      const std::int32_t prepared = Prepare(cueIndex, flags, timeOffset, outCue);
      if (prepared < 0) {
        return prepared;
      }
      const std::int32_t played = (*outCue)->Play();
      if (played < 0) {
        (*outCue)->Destroy();
        *outCue = nullptr;
      }
      return played;
    }

    std::int32_t __stdcall Reserved14() override
    {
      return E_NOTIMPL;
    }

    void __stdcall Destroy() override
    {
      FACTSoundBank_Destroy(mBank);
      delete this;
    }

  private:
    ~FactSoundBank() = default;

    FACTSoundBank* mBank;
  };

  class FactWaveBank final : public moho::IXACTWaveBank
  {
  public:
    /// `stream`: what a streaming bank reads from, for as long as it lives; else null.
    FactWaveBank(FACTWaveBank* const bank, FAudioIOStream* const stream)
      : mBank(bank)
      , mStream(stream)
    {
      BindInterface(mBank, static_cast<moho::IXACTWaveBank*>(this));
    }

    [[nodiscard]] FACTWaveBank* Fact() const
    {
      return mBank;
    }

    void __stdcall Destroy() override
    {
      FACTWaveBank_Destroy(mBank);
      UnbindInterface(mBank);
      if (mStream != nullptr) {
        FAudio_close(mStream);
      }
      delete this;
    }

  private:
    ~FactWaveBank() = default;

    FACTWaveBank* mBank;
    FAudioIOStream* mStream;
  };

  // XACT reads a streaming bank from the file handle it is given; FACT's reader takes a stream.
  // The engine's handle is one of its POSIX layer's, and it closes the handle after the bank.
  std::size_t ReadHandle(void* const handle, void* const destination, const std::size_t size, const std::size_t count)
  {
    DWORD read = 0U;
    if (size == 0U || !::ReadFile(handle, destination, static_cast<DWORD>(size * count), &read, nullptr)) {
      return 0U;
    }
    return read / size;
  }

  std::int64_t SeekHandle(void* const handle, const std::int64_t offset, const int origin)
  {
    LARGE_INTEGER distance{};
    distance.QuadPart = offset;
    LARGE_INTEGER position{};
    // FAudio's FAUDIO_SEEK_ values are FILE_BEGIN and the others.
    if (!::SetFilePointerEx(handle, distance, &position, static_cast<DWORD>(origin))) {
      return -1;
    }
    return position.QuadPart;
  }

  [[nodiscard]] FACTNotificationDescription ToFact(const moho::AudioNotificationDesc& description)
  {
    FACTNotificationDescription fact{};
    fact.type = description.type;
    fact.flags = description.flags;
    if (IXACT3SoundBank* const bank = description.pSoundBank; bank != nullptr) {
      fact.pSoundBank = static_cast<FactSoundBank*>(reinterpret_cast<moho::IXACTSoundBank*>(bank))->Fact();
    }
    if (IXACT3WaveBank* const bank = description.pWaveBank; bank != nullptr) {
      fact.pWaveBank = static_cast<FactWaveBank*>(reinterpret_cast<moho::IXACTWaveBank*>(bank))->Fact();
    }
    if (IXACT3Cue* const cue = description.pCue; cue != nullptr) {
      fact.pCue = static_cast<FactCue*>(reinterpret_cast<moho::IXACTCue*>(cue))->Fact();
    }
    fact.pWave = reinterpret_cast<FACTWave*>(description.pWave);
    fact.cueIndex = description.cueIndex;
    fact.waveIndex = description.waveIndex;
    fact.pvContext = description.pvContext;
    return fact;
  }

  class FactEngine final : public moho::IXACTEngine
  {
  public:
    explicit FactEngine(FACTAudioEngine* const engine)
      : mEngine(engine)
    {}

    std::int32_t __stdcall QueryInterface(const void*, void** const outObject) override
    {
      if (outObject == nullptr) {
        return E_INVALIDARG;
      }
      AddRef();
      *outObject = this;
      return S_OK;
    }

    std::uint32_t __stdcall AddRef() override
    {
      return FACTAudioEngine_AddRef(mEngine);
    }

    std::uint32_t __stdcall Release() override
    {
      const std::uint32_t left = FACTAudioEngine_Release(mEngine);
      if (left == 0U) {
        delete this;
      }
      return left;
    }

    std::int32_t __stdcall GetRendererCount(std::uint16_t* const outRendererCount) override
    {
      if (outRendererCount == nullptr) {
        return E_INVALIDARG;
      }
      return static_cast<std::int32_t>(FACTAudioEngine_GetRendererCount(mEngine, outRendererCount));
    }

    // XACT_RENDERER_DETAILS: FACT's structure has Windows' 16-bit wchar_t, and the layout.
    std::int32_t __stdcall GetRendererDetails(const std::uint16_t rendererIndex, void* const outRendererDetails) override
    {
      if (outRendererDetails == nullptr) {
        return E_INVALIDARG;
      }
      return static_cast<std::int32_t>(
        FACTAudioEngine_GetRendererDetails(mEngine, rendererIndex, static_cast<FACTRendererDetails*>(outRendererDetails))
      );
    }

    std::int32_t __stdcall GetFinalMixFormat(void* const outWaveFormatExtensible) override
    {
      if (outWaveFormatExtensible == nullptr) {
        return E_INVALIDARG;
      }
      FAudioWaveFormatExtensible fact{};
      const std::uint32_t result = FACTAudioEngine_GetFinalMixFormat(mEngine, &fact);
      if (Failed(result)) {
        return static_cast<std::int32_t>(result);
      }
      WAVEFORMATEXTENSIBLE format{};
      format.Format.wFormatTag = fact.Format.wFormatTag;
      format.Format.nChannels = fact.Format.nChannels;
      format.Format.nSamplesPerSec = fact.Format.nSamplesPerSec;
      format.Format.nAvgBytesPerSec = fact.Format.nAvgBytesPerSec;
      format.Format.nBlockAlign = fact.Format.nBlockAlign;
      format.Format.wBitsPerSample = fact.Format.wBitsPerSample;
      format.Format.cbSize = fact.Format.cbSize;
      format.Samples.wValidBitsPerSample = fact.Samples.wValidBitsPerSample;
      format.dwChannelMask = fact.dwChannelMask;
      format.SubFormat.Data1 = fact.SubFormat.Data1;
      format.SubFormat.Data2 = fact.SubFormat.Data2;
      format.SubFormat.Data3 = fact.SubFormat.Data3;
      std::memcpy(format.SubFormat.Data4, fact.SubFormat.Data4, sizeof(format.SubFormat.Data4));
      std::memcpy(outWaveFormatExtensible, &format, sizeof(format));
      return S_OK;
    }

    // No file callbacks: the engine sets none, and reads go through what CreateStreamingWaveBank
    // makes of its handle. No renderer ID: the engine sets none, and there is the one output.
    std::int32_t __stdcall Initialize(const moho::AudioRuntimeParams* const runtimeParams) override
    {
      if (runtimeParams == nullptr) {
        return E_INVALIDARG;
      }
      // An import without sounds/. FACT would make up a few categories and open an output for
      // nothing, and the engine would turn sound off only at the variable it then misses.
      if (runtimeParams->pGlobalSettingsBuffer == nullptr) {
        return kNoGlobalSettings;
      }
      gNotificationCallback.store(runtimeParams->fnNotificationCallback);

      FACTRuntimeParameters fact{};
      fact.lookAheadTime = runtimeParams->lookAheadTime;
      fact.pGlobalSettingsBuffer = runtimeParams->pGlobalSettingsBuffer;
      fact.globalSettingsBufferSize = runtimeParams->globalSettingsBufferSize;
      fact.globalSettingsFlags = runtimeParams->globalSettingsFlags;
      fact.globalSettingsAllocAttributes = runtimeParams->globalSettingsAllocAttributes;
      fact.fnNotificationCallback = (runtimeParams->fnNotificationCallback != nullptr) ? &HandleFactNotification : nullptr;
      return static_cast<std::int32_t>(FACTAudioEngine_Initialize(mEngine, &fact));
    }

    void __stdcall ShutDown() override
    {
      FACTAudioEngine_ShutDown(mEngine);
    }

    std::int32_t __stdcall DoWork() override
    {
      return static_cast<std::int32_t>(FACTAudioEngine_DoWork(mEngine));
    }

    std::int32_t __stdcall CreateSoundBank(
      const void* const data,
      const std::int32_t sizeBytes,
      const std::uint32_t flags,
      const std::uint32_t allocAttributes,
      moho::IXACTSoundBank** const outSoundBank
    ) override
    {
      if (outSoundBank == nullptr) {
        return E_INVALIDARG;
      }
      *outSoundBank = nullptr;
      FACTSoundBank* bank = nullptr;
      const std::uint32_t result =
        FACTAudioEngine_CreateSoundBank(mEngine, data, static_cast<std::uint32_t>(sizeBytes), flags, allocAttributes, &bank);
      if (!Failed(result)) {
        *outSoundBank = new FactSoundBank(bank);
      }
      return static_cast<std::int32_t>(result);
    }

    std::int32_t __stdcall CreateInMemoryWaveBank(
      const void* const data,
      const std::int32_t sizeBytes,
      const std::uint32_t flags,
      const std::uint32_t allocAttributes,
      moho::IXACTWaveBank** const outWaveBank
    ) override
    {
      if (outWaveBank == nullptr) {
        return E_INVALIDARG;
      }
      *outWaveBank = nullptr;
      FACTWaveBank* bank = nullptr;
      const std::uint32_t result =
        FACTAudioEngine_CreateInMemoryWaveBank(mEngine, data, static_cast<std::uint32_t>(sizeBytes), flags, allocAttributes, &bank);
      if (!Failed(result)) {
        *outWaveBank = new FactWaveBank(bank, nullptr);
      }
      return static_cast<std::int32_t>(result);
    }

    std::int32_t __stdcall CreateStreamingWaveBank(const void* const streamingParams, moho::IXACTWaveBank** const outWaveBank) override
    {
      if (streamingParams == nullptr || outWaveBank == nullptr) {
        return E_INVALIDARG;
      }
      *outWaveBank = nullptr;
      const auto& parameters = *static_cast<const XACT_STREAMING_PARAMETERS*>(streamingParams);
      FAudioIOStream* const stream = FAudioAndroid_OpenStream(parameters.file, &ReadHandle, &SeekHandle);
      FACTStreamingParameters fact{};
      fact.file = stream;
      fact.offset = parameters.offset;
      fact.flags = parameters.flags;
      fact.packetSize = parameters.packetSize;
      FACTWaveBank* bank = nullptr;
      const std::uint32_t result = FACTAudioEngine_CreateStreamingWaveBank(mEngine, &fact, &bank);
      if (Failed(result)) {
        FAudio_close(stream);
      } else {
        *outWaveBank = new FactWaveBank(bank, stream);
      }
      return static_cast<std::int32_t>(result);
    }

    std::int32_t __stdcall PrepareWave(std::uint32_t, const char*, std::uint16_t, std::uint32_t, std::uint32_t, std::uint8_t, void** const outWave) override
    {
      return NoWave(outWave);
    }

    std::int32_t __stdcall PrepareInMemoryWave(std::uint32_t, const void*, std::uint32_t*, std::uint8_t*, std::uint32_t, std::uint8_t, void** const outWave) override
    {
      return NoWave(outWave);
    }

    std::int32_t __stdcall PrepareStreamingWave(std::uint32_t, const void*, const void*, std::uint32_t, std::uint32_t*, std::uint32_t, std::uint8_t, void** const outWave) override
    {
      return NoWave(outWave);
    }

    std::int32_t __stdcall RegisterNotification(const moho::AudioNotificationDesc* const notificationDesc) override
    {
      if (notificationDesc == nullptr) {
        return E_INVALIDARG;
      }
      const FACTNotificationDescription fact = ToFact(*notificationDesc);
      return static_cast<std::int32_t>(FACTAudioEngine_RegisterNotification(mEngine, &fact));
    }

    // Null takes all registrations off, in XACT and in FACT.
    std::int32_t __stdcall UnRegisterNotification(const moho::AudioNotificationDesc* const notificationDesc) override
    {
      if (notificationDesc == nullptr) {
        return static_cast<std::int32_t>(FACTAudioEngine_UnRegisterNotification(mEngine, nullptr));
      }
      const FACTNotificationDescription fact = ToFact(*notificationDesc);
      return static_cast<std::int32_t>(FACTAudioEngine_UnRegisterNotification(mEngine, &fact));
    }

    std::uint16_t __stdcall GetCategory(const char* const categoryName) override
    {
      return FACTAudioEngine_GetCategory(mEngine, categoryName);
    }

    void __stdcall Stop(const std::uint16_t category, const std::int32_t flags) override
    {
      FACTAudioEngine_Stop(mEngine, category, static_cast<std::uint32_t>(flags));
    }

    std::int32_t __stdcall SetVolume(const std::uint16_t category, const float volume) override
    {
      return static_cast<std::int32_t>(FACTAudioEngine_SetVolume(mEngine, category, volume));
    }

    std::int32_t __stdcall Pause(const std::uint16_t category, const std::int32_t pause) override
    {
      return static_cast<std::int32_t>(FACTAudioEngine_Pause(mEngine, category, pause));
    }

    std::uint16_t __stdcall GetGlobalVariableIndex(const char* const variableName) override
    {
      return FACTAudioEngine_GetGlobalVariableIndex(mEngine, variableName);
    }

    std::int32_t __stdcall SetGlobalVariable(const std::uint16_t variableIndex, const float value) override
    {
      return static_cast<std::int32_t>(FACTAudioEngine_SetGlobalVariable(mEngine, variableIndex, value));
    }

    std::int32_t __stdcall GetGlobalVariable(const std::uint16_t variableIndex, float* const outValue) override
    {
      if (outValue == nullptr) {
        return E_INVALIDARG;
      }
      return static_cast<std::int32_t>(FACTAudioEngine_GetGlobalVariable(mEngine, variableIndex, outValue));
    }

  private:
    ~FactEngine() = default;

    [[nodiscard]] static std::int32_t NoWave(void** const outWave)
    {
      if (outWave != nullptr) {
        *outWave = nullptr;
      }
      return E_NOTIMPL;
    }

    FACTAudioEngine* mEngine;
  };
} // namespace

// CoCreateInstance's way to the client's classes (src/win32/Win32Android.cpp).
extern "C" HRESULT FafAndroid_CreateClientInstance(REFCLSID classId, REFIID, void** const object)
{
  for (const GUID& xact : kXactClasses) {
    if (std::memcmp(&classId, &xact, sizeof(GUID)) == 0) {
      FACTAudioEngine* engine = nullptr;
      const std::uint32_t result = FACTCreateEngine(0U, &engine);
      if (Failed(result)) {
        return static_cast<HRESULT>(result);
      }
      *object = static_cast<moho::IXACTEngine*>(new FactEngine(engine));
      return S_OK;
    }
  }
  return REGDB_E_CLASSNOTREG;
}
