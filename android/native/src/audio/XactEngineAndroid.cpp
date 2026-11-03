// The XACT engine of the Android client build: one that is there and plays nothing, until C3
// brings FAudio's FACT (CLIENT_PLAN.md). Without an engine every sound function raises a Lua error
// ("SND: Error retrieving XACT COM interface"), as on a PC without the XACT runtime. That stops the
// import of /lua/usermusic.lua and with it /lua/options/options.lua, so every option reads as an
// empty string, and an empty "secondary_adapter" makes CScApp::CreateDevice ask for a second head
// on the one screen. With this engine the game runs as on a PC whose sound is muted.
//
// The interfaces are the engine's own (moho/audio/AudioEngine.h). Without looking into the banks,
// the engine answers as XACT does for names it knows: every cue, category and variable name has an
// index of its own, a cue plays for a moment and then stops, and a global variable keeps what was
// set. CoCreateInstance (src/win32/Win32Android.cpp) creates it through
// FafAndroid_CreateClientInstance for XACT's three class IDs.

#include <Windows.h>
#include <mmreg.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "moho/audio/AudioEngine.h"

namespace
{
  // The retail, debug and audition engines' class IDs (moho/audio/AudioEngine.cpp).
  constexpr GUID kXactClasses[] = {
    {0x343e68e6u, 0x8f82u, 0x4a8du, {0xa2u, 0xdau, 0x6eu, 0x9au, 0x94u, 0x4bu, 0x37u, 0x8cu}},
    {0x3cbb606bu, 0x06f1u, 0x473eu, {0x9du, 0xd5u, 0x0eu, 0x4au, 0x3bu, 0x47u, 0x14u, 0x13u}},
    {0xcedde475u, 0x50b5u, 0x47efu, {0x91u, 0xa7u, 0x3bu, 0x49u, 0xa0u, 0xe8u, 0xe5u, 0x88u}},
  };

  // XACT's cue states (XACT_CUESTATE_*).
  constexpr std::int32_t kCuePrepared = 0x04;
  constexpr std::int32_t kCuePlaying = 0x08;
  constexpr std::int32_t kCueStopped = 0x20;

  // How long a cue counts as playing. The sound manager frees a one-shot sound's record when its
  // cue stops and starts a looping one (music, ambience) again, so a short time keeps both cheap.
  constexpr std::chrono::milliseconds kCuePlayTime{1000};

  /** Names and the indices handed out for them, in the order they were first asked for. */
  class NameIndex
  {
  public:
    [[nodiscard]] std::uint16_t Get(const char* const name)
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      const auto [entry, added] =
        mIndices.try_emplace((name != nullptr) ? name : "", static_cast<std::uint16_t>(mIndices.size()));
      return entry->second;
    }

  private:
    std::mutex mMutex;
    std::unordered_map<std::string, std::uint16_t> mIndices;
  };

  class SilentCue final : public moho::IXACTCue
  {
  public:
    explicit SilentCue(NameIndex& variables)
      : mVariables(variables)
    {}

    std::int32_t __stdcall Play() override
    {
      mStarted.store(std::chrono::steady_clock::now().time_since_epoch().count());
      mPlayed.store(true);
      return S_OK;
    }

    void __stdcall Stop(std::int32_t) override
    {
      mStopped.store(true);
    }

    std::int32_t __stdcall GetState(std::int32_t* const outState) override
    {
      if (outState != nullptr) {
        *outState = State();
      }
      return S_OK;
    }

    std::int32_t __stdcall Destroy() override
    {
      delete this;
      return S_OK;
    }

    void __stdcall Reserved10() override {}
    void __stdcall Reserved14() override {}
    void __stdcall Reserved18() override {}
    void __stdcall Reserved1C() override {}

    std::int32_t __stdcall SetMatrixCoefficients(std::uint32_t, std::uint32_t, const float*) override
    {
      return S_OK;
    }

    std::uint16_t __stdcall GetVariableIndex(const char* const variableName) override
    {
      return mVariables.Get(variableName);
    }

    std::int32_t __stdcall SetVariable(std::uint16_t, float) override
    {
      return S_OK;
    }

  private:
    ~SilentCue() = default;

    [[nodiscard]] std::int32_t State() const
    {
      if (mStopped.load()) {
        return kCueStopped;
      }
      if (!mPlayed.load()) {
        return kCuePrepared;
      }
      const std::chrono::steady_clock::duration played =
        std::chrono::steady_clock::now().time_since_epoch() - std::chrono::steady_clock::duration(mStarted.load());
      return (played < kCuePlayTime) ? kCuePlaying : kCueStopped;
    }

    NameIndex& mVariables;
    std::atomic<std::chrono::steady_clock::rep> mStarted{0};
    std::atomic<bool> mPlayed{false};
    std::atomic<bool> mStopped{false};
  };

  class SilentSoundBank final : public moho::IXACTSoundBank
  {
  public:
    explicit SilentSoundBank(NameIndex& cueVariables)
      : mCueVariables(cueVariables)
    {}

    std::uint16_t __stdcall GetCueIndex(const char* const cueName) override
    {
      return mCues.Get(cueName);
    }

    std::int32_t __stdcall Reserved04() override
    {
      return E_NOTIMPL;
    }

    std::int32_t __stdcall Reserved08() override
    {
      return E_NOTIMPL;
    }

    std::int32_t __stdcall Prepare(std::uint16_t, std::uint32_t, std::uint32_t, moho::IXACTCue** const outCue) override
    {
      if (outCue == nullptr) {
        return E_INVALIDARG;
      }
      *outCue = new SilentCue(mCueVariables);
      return S_OK;
    }

    // Without a cue to hand back, XACT plays the cue on its own; here there is nothing to play.
    std::int32_t __stdcall Play(std::uint16_t, std::uint32_t, std::uint32_t, moho::IXACTCue** const outCue) override
    {
      if (outCue != nullptr) {
        *outCue = new SilentCue(mCueVariables);
        (*outCue)->Play();
      }
      return S_OK;
    }

    std::int32_t __stdcall Reserved14() override
    {
      return E_NOTIMPL;
    }

    void __stdcall Destroy() override
    {
      delete this;
    }

  private:
    ~SilentSoundBank() = default;

    NameIndex mCues;
    NameIndex& mCueVariables;
  };

  class SilentWaveBank final : public moho::IXACTWaveBank
  {
  public:
    void __stdcall Destroy() override
    {
      delete this;
    }

  private:
    ~SilentWaveBank() = default;
  };

  class SilentEngine final : public moho::IXACTEngine
  {
  public:
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
      return ++mReferences;
    }

    std::uint32_t __stdcall Release() override
    {
      const std::uint32_t left = --mReferences;
      if (left == 0U) {
        delete this;
      }
      return left;
    }

    std::int32_t __stdcall GetRendererCount(std::uint16_t* const outRendererCount) override
    {
      if (outRendererCount != nullptr) {
        *outRendererCount = 1U;
      }
      return S_OK;
    }

    std::int32_t __stdcall GetRendererDetails(std::uint16_t, void*) override
    {
      return E_NOTIMPL;
    }

    // A stereo mix of 48 kHz floats, the format of a phone's output.
    std::int32_t __stdcall GetFinalMixFormat(void* const outWaveFormatExtensible) override
    {
      if (outWaveFormatExtensible == nullptr) {
        return E_INVALIDARG;
      }
      WAVEFORMATEXTENSIBLE format{};
      format.Format.wFormatTag = 0xFFFEU; // WAVE_FORMAT_EXTENSIBLE
      format.Format.nChannels = 2U;
      format.Format.nSamplesPerSec = 48000U;
      format.Format.wBitsPerSample = 32U;
      format.Format.nBlockAlign = 8U;
      format.Format.nAvgBytesPerSec = 48000U * 8U;
      format.Format.cbSize = 22U;
      format.Samples.wValidBitsPerSample = 32U;
      format.dwChannelMask = 0x3U; // SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
      std::memcpy(outWaveFormatExtensible, &format, sizeof(format));
      return S_OK;
    }

    std::int32_t __stdcall Initialize(const moho::AudioRuntimeParams*) override
    {
      return S_OK;
    }

    void __stdcall ShutDown() override {}

    std::int32_t __stdcall DoWork() override
    {
      return S_OK;
    }

    std::int32_t __stdcall CreateSoundBank(const void*, std::int32_t, std::uint32_t, std::uint32_t, moho::IXACTSoundBank** const outSoundBank) override
    {
      if (outSoundBank == nullptr) {
        return E_INVALIDARG;
      }
      *outSoundBank = new SilentSoundBank(mCueVariables);
      return S_OK;
    }

    std::int32_t __stdcall CreateInMemoryWaveBank(const void*, std::int32_t, std::uint32_t, std::uint32_t, moho::IXACTWaveBank** const outWaveBank) override
    {
      if (outWaveBank == nullptr) {
        return E_INVALIDARG;
      }
      *outWaveBank = new SilentWaveBank();
      return S_OK;
    }

    std::int32_t __stdcall CreateStreamingWaveBank(const void*, moho::IXACTWaveBank** const outWaveBank) override
    {
      if (outWaveBank == nullptr) {
        return E_INVALIDARG;
      }
      *outWaveBank = new SilentWaveBank();
      return S_OK;
    }

    // Single waves outside a bank: the engine plays none.
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

    // No sound ends, so there is nothing to notify.
    std::int32_t __stdcall RegisterNotification(const moho::AudioNotificationDesc*) override
    {
      return S_OK;
    }

    std::int32_t __stdcall UnRegisterNotification(const moho::AudioNotificationDesc*) override
    {
      return S_OK;
    }

    std::uint16_t __stdcall GetCategory(const char* const categoryName) override
    {
      return mCategories.Get(categoryName);
    }

    void __stdcall Stop(std::uint16_t, std::int32_t) override {}

    std::int32_t __stdcall SetVolume(std::uint16_t, float) override
    {
      return S_OK;
    }

    std::int32_t __stdcall Pause(std::uint16_t, std::int32_t) override
    {
      return S_OK;
    }

    std::uint16_t __stdcall GetGlobalVariableIndex(const char* const variableName) override
    {
      return mGlobalNames.Get(variableName);
    }

    std::int32_t __stdcall SetGlobalVariable(const std::uint16_t variableIndex, const float value) override
    {
      const std::lock_guard<std::mutex> lock(mGlobalMutex);
      mGlobalValues[variableIndex] = value;
      return S_OK;
    }

    std::int32_t __stdcall GetGlobalVariable(const std::uint16_t variableIndex, float* const outValue) override
    {
      if (outValue == nullptr) {
        return E_INVALIDARG;
      }
      const std::lock_guard<std::mutex> lock(mGlobalMutex);
      const auto found = mGlobalValues.find(variableIndex);
      *outValue = (found != mGlobalValues.end()) ? found->second : 0.0F;
      return S_OK;
    }

  private:
    ~SilentEngine() = default;

    [[nodiscard]] static std::int32_t NoWave(void** const outWave)
    {
      if (outWave != nullptr) {
        *outWave = nullptr;
      }
      return E_NOTIMPL;
    }

    std::atomic<std::uint32_t> mReferences{1U};
    NameIndex mCategories;
    NameIndex mGlobalNames;
    NameIndex mCueVariables;
    std::mutex mGlobalMutex;
    std::unordered_map<std::uint16_t, float> mGlobalValues;
  };
} // namespace

// CoCreateInstance's way to the client's classes (src/win32/Win32Android.cpp).
extern "C" HRESULT FafAndroid_CreateClientInstance(REFCLSID classId, REFIID, void** const object)
{
  for (const GUID& xact : kXactClasses) {
    if (std::memcmp(&classId, &xact, sizeof(GUID)) == 0) {
      *object = static_cast<moho::IXACTEngine*>(new SilentEngine());
      return S_OK;
    }
  }
  return REGDB_E_CLASSNOTREG;
}
