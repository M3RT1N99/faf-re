// faf_audio_tests: the game's XACT engine (src/audio/XactEngineAndroid.cpp) with the game's banks,
// run on a device or the emulator, where nobody hears it: the mix's numbers, through the platform's
// observer, are the ears. X3DAudio on F3DAudio as well (src/audio/AudioAndroid.cpp).
//
//   faf_audio_tests <the game's sounds folder>
//
// It prints each test's name and the checks that failed, then `<n> tests, <m> failed`. Exit code 0
// when every check passes, 1 otherwise.

#include <Windows.h>
#include <mmreg.h>

#include <FAudio.h>
#include <aaudio/AAudio.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../src/audio/FAudioPlatformAndroid.h"
#include "moho/audio/AudioEngine.h"

extern "C" HRESULT FafAndroid_CreateClientInstance(REFCLSID classId, REFIID interfaceId, void** object);
extern "C" void X3DAudioInitialize(std::uint32_t speakerChannelMask, float speedOfSound, void* instance);
extern "C" void X3DAudioCalculate(
  const void* instance, const moho::Audio3DListener* listener, const moho::Audio3DEmitter* emitter, std::uint32_t flags, moho::Audio3DDspSettings* settings
);

namespace
{
  using namespace std::chrono_literals;

  constexpr GUID kRetailEngine = {0x343e68e6u, 0x8f82u, 0x4a8du, {0xa2u, 0xdau, 0x6eu, 0x9au, 0x94u, 0x4bu, 0x37u, 0x8cu}};
  constexpr GUID kDebugEngine = {0x3cbb606bu, 0x06f1u, 0x473eu, {0x9du, 0xd5u, 0x0eu, 0x4au, 0x3bu, 0x47u, 0x14u, 0x13u}};
  constexpr GUID kAuditionEngine = {0xcedde475u, 0x50b5u, 0x47efu, {0x91u, 0xa7u, 0x3bu, 0x49u, 0xa0u, 0xe8u, 0xe5u, 0x88u}};
  constexpr GUID kNoEngine = {0x00000001u, 0x0002u, 0x0003u, {4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u}};
  constexpr GUID kEngineInterface = {0x893ff2e4u, 0x8d03u, 0x4d5fu, {0xb0u, 0xaau, 0x36u, 0x3au, 0x9cu, 0xbbu, 0xf4u, 0x37u}};

  // The states the engine compares with (XACT_CUESTATE_*).
  constexpr std::int32_t kCuePrepared = 0x04;
  constexpr std::int32_t kCuePlaying = 0x08;
  constexpr std::int32_t kCueStopping = 0x10;
  constexpr std::int32_t kCueStopped = 0x20;

  constexpr std::int32_t kInstanceLimit = static_cast<std::int32_t>(0x8AC70008u); // XACTENGINE_E_INSTANCELIMITFAILTOPLAY
  constexpr std::int32_t kNoGlobalSettings = static_cast<std::int32_t>(0x8AC70009u);
  constexpr std::uint16_t kInvalid = 0xFFFFU;
  // Until a change reached the mix: FACT passes over its cues every 10 ms, a busy host permitting.
  constexpr std::chrono::milliseconds kFactPass{200};
  constexpr std::uint32_t kCalculateMatrixDopplerAngle = 0x61U; // what moho/audio/AudioEngine.cpp asks X3DAudio for

  std::string gSounds;
  int gFailedChecks = 0; // in the test that runs

  void Fail(const int line, const std::string& what)
  {
    ++gFailedChecks;
    std::printf("  line %d: %s\n", line, what.c_str());
    std::fflush(stdout);
  }

#define CHECK(condition) ((condition) ? (void)0 : Fail(__LINE__, #condition))
#define CHECK_MESSAGE(condition, ...)                  \
  do {                                                 \
    if (!(condition)) {                                \
      char text[256];                                  \
      std::snprintf(text, sizeof(text), __VA_ARGS__);  \
      Fail(__LINE__, std::string(#condition ": ") + text); \
    }                                                  \
  } while (false)

  // --- The ears ---------------------------------------------------------------------------------

  struct Heard
  {
    float peak = 0.0F;
    float peakLeft = 0.0F;
    float peakRight = 0.0F;
    double squares = 0.0;
    std::uint64_t frames = 0U;

    [[nodiscard]] float Rms() const
    {
      return (frames != 0U) ? static_cast<float>(std::sqrt(squares / (2.0 * static_cast<double>(frames)))) : 0.0F;
    }
  };

  std::mutex gHeardMutex;
  Heard gHeard;

  void HearMix(const float* const samples, const std::uint32_t frames, const std::uint32_t channels, void*)
  {
    const std::lock_guard<std::mutex> lock(gHeardMutex);
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
      const float left = samples[frame * channels];
      const float right = samples[frame * channels + 1U];
      gHeard.peakLeft = std::max(gHeard.peakLeft, std::fabs(left));
      gHeard.peakRight = std::max(gHeard.peakRight, std::fabs(right));
      gHeard.squares += static_cast<double>(left) * left + static_cast<double>(right) * right;
    }
    gHeard.peak = std::max(gHeard.peakLeft, gHeard.peakRight);
    gHeard.frames += frames;
  }

  void ForgetHeard()
  {
    const std::lock_guard<std::mutex> lock(gHeardMutex);
    gHeard = Heard{};
  }

  [[nodiscard]] Heard HeardSoFar()
  {
    const std::lock_guard<std::mutex> lock(gHeardMutex);
    return gHeard;
  }

  /// What the next `duration` of the mix holds. The mix's own time: it goes on as fast as the output
  /// takes it, which a busy host holds up, and a clock would hear nothing then.
  [[nodiscard]] Heard Listen(const std::chrono::milliseconds duration)
  {
    constexpr std::uint64_t kFramesPerMillisecond = 48U;
    ForgetHeard();
    const auto end = std::chrono::steady_clock::now() + duration + 10s;
    while (HeardSoFar().frames < static_cast<std::uint64_t>(duration.count()) * kFramesPerMillisecond && std::chrono::steady_clock::now() < end) {
      std::this_thread::sleep_for(5ms);
    }
    const Heard heard = HeardSoFar();
    CHECK_MESSAGE(
      heard.frames >= static_cast<std::uint64_t>(duration.count()) * kFramesPerMillisecond,
      "the stream took %llu frames in time for %lld ms",
      static_cast<unsigned long long>(heard.frames),
      static_cast<long long>(duration.count())
    );
    return heard;
  }

  /// What goes out in `duration` by the clock, for the times when nothing should.
  [[nodiscard]] Heard ListenAway(const std::chrono::milliseconds duration)
  {
    ForgetHeard();
    std::this_thread::sleep_for(duration);
    return HeardSoFar();
  }

  // --- A lost device ----------------------------------------------------------------------------

  std::atomic<bool> gLoseDevice{false};
  std::atomic<bool> gNoOutput{false};
  std::atomic<bool> gStartFails{false};
  std::atomic<bool> gTakesNothing{false};
  std::atomic<int> gEmptyWrites{0};
  std::atomic<int> gStreamsOpened{0}; // the tries to
  std::atomic<std::chrono::steady_clock::rep> gStreamClosed{0}; // when, as steady_clock counts
} // namespace

// These stand in for AAudio's functions in this program, which reach the real ones: a test can pull
// the headphones out, which it cannot do to the emulator.
extern "C" aaudio_result_t AAudioStream_write(AAudioStream* const stream, const void* const buffer, const int32_t frames, const int64_t timeoutNanoseconds)
{
  using Function = aaudio_result_t (*)(AAudioStream*, const void*, int32_t, int64_t);
  static const auto real = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "AAudioStream_write"));
  if (gLoseDevice.exchange(false)) {
    return AAUDIO_ERROR_DISCONNECTED;
  }
  if (gTakesNothing) {
    ++gEmptyWrites;
    return 0;
  }
  return real(stream, buffer, frames, timeoutNanoseconds);
}

extern "C" aaudio_result_t AAudioStream_close(AAudioStream* const stream)
{
  using Function = aaudio_result_t (*)(AAudioStream*);
  static const auto real = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "AAudioStream_close"));
  gStreamClosed = std::chrono::steady_clock::now().time_since_epoch().count();
  return real(stream);
}

extern "C" aaudio_result_t AAudioStream_requestStart(AAudioStream* const stream)
{
  using Function = aaudio_result_t (*)(AAudioStream*);
  static const auto real = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "AAudioStream_requestStart"));
  if (gStartFails.exchange(false)) {
    return AAUDIO_ERROR_INTERNAL;
  }
  return real(stream);
}

extern "C" aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder* const builder, AAudioStream** const stream)
{
  using Function = aaudio_result_t (*)(AAudioStreamBuilder*, AAudioStream**);
  static const auto real = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "AAudioStreamBuilder_openStream"));
  ++gStreamsOpened;
  if (gNoOutput) {
    return AAUDIO_ERROR_UNAVAILABLE;
  }
  return real(builder, stream);
}

namespace
{
  // --- The engine -------------------------------------------------------------------------------

  [[nodiscard]] std::vector<char> ReadWhole(const std::string& path)
  {
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
  }

  std::mutex gNotesMutex;
  std::vector<moho::AudioNotification> gNotes;

  void HearNotification(const moho::AudioNotification* const note)
  {
    const std::lock_guard<std::mutex> lock(gNotesMutex);
    gNotes.push_back(*note);
  }

  /// An engine as AudioEngine.cpp sets one up, on SupCom.xgs, with what it loads in order: wave
  /// banks first, as their sound banks find them by name.
  class Engine
  {
  public:
    Engine()
      : mSettings(ReadWhole(gSounds + "/SupCom.xgs"))
    {
      void* object = nullptr;
      CHECK(FafAndroid_CreateClientInstance(kRetailEngine, kEngineInterface, &object) == S_OK);
      mEngine = static_cast<moho::IXACTEngine*>(object);

      moho::AudioRuntimeParams parameters{};
      parameters.lookAheadTime = 250U;
      parameters.pGlobalSettingsBuffer = mSettings.data();
      parameters.globalSettingsBufferSize = static_cast<DWORD>(mSettings.size());
      parameters.fnNotificationCallback = &HearNotification;
      CHECK(mEngine->Initialize(&parameters) == S_OK);
    }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    ~Engine()
    {
      for (moho::IXACTSoundBank* const bank : mSoundBanks) {
        bank->Destroy();
      }
      for (moho::IXACTWaveBank* const bank : mWaveBanks) {
        bank->Destroy();
      }
      for (const HANDLE file : mFiles) {
        ::CloseHandle(file);
      }
      mEngine->ShutDown();
      CHECK(mEngine->Release() == 0U);
    }

    moho::IXACTEngine* operator->() const
    {
      return mEngine;
    }

    /// `name`.xwb and `name`.xsb, as the engine reads them.
    moho::IXACTSoundBank* LoadInMemory(const std::string& name)
    {
      LoadWaves(name);
      return LoadSounds(name);
    }

    moho::IXACTWaveBank* LoadWaves(const std::string& name)
    {
      mMemory.push_back(ReadWhole(gSounds + "/" + name + ".xwb"));
      moho::IXACTWaveBank* bank = nullptr;
      const std::int32_t result = mEngine->CreateInMemoryWaveBank(mMemory.back().data(), static_cast<std::int32_t>(mMemory.back().size()), 0U, 0U, &bank);
      CHECK_MESSAGE(result == S_OK && bank != nullptr, "%s: 0x%08X", name.c_str(), static_cast<unsigned>(result));
      mWaveBanks.push_back(bank);
      return bank;
    }

    moho::IXACTSoundBank* LoadSounds(const std::string& name)
    {
      mMemory.push_back(ReadWhole(gSounds + "/" + name + ".xsb"));
      moho::IXACTSoundBank* bank = nullptr;
      const std::int32_t result = mEngine->CreateSoundBank(mMemory.back().data(), static_cast<std::int32_t>(mMemory.back().size()), 0U, 0U, &bank);
      CHECK_MESSAGE(result == S_OK && bank != nullptr, "%s: 0x%08X", name.c_str(), static_cast<unsigned>(result));
      mSoundBanks.push_back(bank);
      return bank;
    }

    /// What AudioStreamingWaveBankLoader::Load does: its file handle and its parameters.
    moho::IXACTWaveBank* LoadStreamingWaves(const std::string& name)
    {
      const HANDLE file = ::CreateFileA((gSounds + "/" + name + ".xwb").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0x60000000U, nullptr);
      CHECK(file != INVALID_HANDLE_VALUE);
      mFiles.push_back(file);
      XACT_STREAMING_PARAMETERS parameters{};
      parameters.file = file;
      parameters.packetSize = 0x12U;
      moho::IXACTWaveBank* bank = nullptr;
      const std::int32_t result = mEngine->CreateStreamingWaveBank(&parameters, &bank);
      CHECK_MESSAGE(result == S_OK && bank != nullptr, "%s: 0x%08X", name.c_str(), static_cast<unsigned>(result));
      mWaveBanks.push_back(bank);
      return bank;
    }

  private:
    std::vector<char> mSettings;
    std::vector<std::vector<char>> mMemory;
    std::vector<HANDLE> mFiles;
    std::vector<moho::IXACTWaveBank*> mWaveBanks;
    std::vector<moho::IXACTSoundBank*> mSoundBanks;
    moho::IXACTEngine* mEngine = nullptr;
  };

  [[nodiscard]] std::int32_t StateOf(moho::IXACTCue* const cue)
  {
    std::int32_t state = 0;
    CHECK(cue->GetState(&state) == S_OK);
    return state;
  }

  /// Runs `engine->DoWork` as a frame would, until the cue stopped or `limit` passed.
  [[nodiscard]] bool WaitStopped(Engine& engine, moho::IXACTCue* const cue, const std::chrono::milliseconds limit)
  {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (StateOf(cue) != kCueStopped) {
      if (std::chrono::steady_clock::now() > end) {
        return false;
      }
      engine->DoWork();
      std::this_thread::sleep_for(10ms);
    }
    return true;
  }

  [[nodiscard]] moho::IXACTCue* PlayCue(moho::IXACTSoundBank* const bank, const char* const name)
  {
    const std::uint16_t index = bank->GetCueIndex(name);
    CHECK_MESSAGE(index != kInvalid, "no cue %s", name);
    moho::IXACTCue* cue = nullptr;
    const std::int32_t result = bank->Play(index, 0U, 0U, &cue);
    CHECK_MESSAGE(result == S_OK && cue != nullptr, "%s: 0x%08X", name, static_cast<unsigned>(result));
    return cue;
  }

  // --- The tests --------------------------------------------------------------------------------

  void TestCreation()
  {
    for (const GUID& known : {kRetailEngine, kDebugEngine, kAuditionEngine}) {
      void* object = nullptr;
      CHECK(FafAndroid_CreateClientInstance(known, kEngineInterface, &object) == S_OK);
      CHECK(object != nullptr);
      if (object != nullptr) {
        auto* const engine = static_cast<moho::IXACTEngine*>(object);
        void* same = nullptr;
        CHECK(engine->QueryInterface(&kEngineInterface, &same) == S_OK);
        CHECK(same == object);
        CHECK(engine->Release() == 1U);
        CHECK(engine->Release() == 0U);
      }
    }
    void* object = nullptr;
    CHECK(FafAndroid_CreateClientInstance(kNoEngine, kEngineInterface, &object) == REGDB_E_CLASSNOTREG);
    CHECK(object == nullptr);
  }

  void TestNoSoundsFolder()
  {
    void* object = nullptr;
    CHECK(FafAndroid_CreateClientInstance(kRetailEngine, kEngineInterface, &object) == S_OK);
    auto* const engine = static_cast<moho::IXACTEngine*>(object);
    moho::AudioRuntimeParams parameters{};
    parameters.fnNotificationCallback = &HearNotification;
    CHECK(engine->Initialize(&parameters) == kNoGlobalSettings);
    engine->ShutDown();
    CHECK(engine->Release() == 0U);
  }

  void TestFinalMixFormat()
  {
    Engine engine;
    WAVEFORMATEXTENSIBLE format{};
    CHECK(engine->GetFinalMixFormat(&format) == S_OK);
    CHECK(format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE);
    CHECK(format.Format.nChannels == 2U);
    CHECK(format.Format.nSamplesPerSec == 48000U);
    CHECK(format.Format.wBitsPerSample == 32U);
    CHECK(format.Format.nBlockAlign == 8U);
    CHECK(format.Format.nAvgBytesPerSec == 48000U * 8U);
    CHECK(format.dwChannelMask == 3U); // SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT
    std::uint16_t renderers = 0U;
    CHECK(engine->GetRendererCount(&renderers) == S_OK);
    CHECK(renderers == 1U);
  }

  void TestBanksThatDoNotLoad()
  {
    Engine engine;
    const char notABank[64] = "This is no bank of any kind, and it says so.";
    moho::IXACTSoundBank* soundBank = reinterpret_cast<moho::IXACTSoundBank*>(&engine);
    CHECK(engine->CreateSoundBank(notABank, sizeof(notABank), 0U, 0U, &soundBank) < 0);
    CHECK(soundBank == nullptr);
    moho::IXACTWaveBank* waveBank = reinterpret_cast<moho::IXACTWaveBank*>(&engine);
    CHECK(engine->CreateInMemoryWaveBank(notABank, sizeof(notABank), 0U, 0U, &waveBank) < 0);
    CHECK(waveBank == nullptr);

    const HANDLE file = ::CreateFileA((gSounds + "/SupCom.xgs").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0U, nullptr);
    XACT_STREAMING_PARAMETERS parameters{};
    parameters.file = file;
    waveBank = reinterpret_cast<moho::IXACTWaveBank*>(&engine);
    CHECK(engine->CreateStreamingWaveBank(&parameters, &waveBank) < 0);
    CHECK(waveBank == nullptr);
    ::CloseHandle(file);
  }

  void TestInMemoryCue()
  {
    Engine engine;
    moho::IXACTSoundBank* const bank = engine.LoadInMemory("Interface");
    CHECK(bank->GetCueIndex("No_Such_Cue") == kInvalid);
    moho::IXACTCue* invalid = nullptr;
    CHECK(bank->Prepare(0xFFF0U, 0U, 0U, &invalid) < 0);
    CHECK(invalid == nullptr);
    CHECK(bank->Play(0xFFF0U, 0U, 0U, nullptr) < 0);

    const Heard before = Listen(100ms);
    CHECK_MESSAGE(before.frames > 0U && before.peak == 0.0F, "frames %llu peak %f", static_cast<unsigned long long>(before.frames), before.peak);

    const std::uint16_t index = bank->GetCueIndex("X_Main_Menu_On");
    CHECK(index != kInvalid);
    moho::IXACTCue* cue = nullptr;
    CHECK(bank->Prepare(index, 0U, 0U, &cue) == S_OK);
    CHECK(StateOf(cue) == kCuePrepared);
    const Heard prepared = Listen(50ms);
    CHECK_MESSAGE(prepared.peak == 0.0F, "peak %f", prepared.peak);

    CHECK(cue->Play() == S_OK);
    CHECK(StateOf(cue) == kCuePlaying);
    const Heard playing = Listen(300ms);
    std::printf("  X_Main_Menu_On: peak %.3f rms %.4f\n", playing.peak, playing.Rms());
    CHECK_MESSAGE(playing.peak > 0.001F && playing.Rms() > 0.0001F, "peak %f rms %f", playing.peak, playing.Rms());
    CHECK(WaitStopped(engine, cue, 15s));
    const Heard stopped = Listen(100ms);
    CHECK_MESSAGE(stopped.peak == 0.0F, "peak %f", stopped.peak);
    CHECK(cue->Destroy() == S_OK);

    // A cue the engine does not keep ends by itself.
    CHECK(bank->Play(index, 0U, 0U, nullptr) == S_OK);
    const Heard unowned = Listen(300ms);
    CHECK_MESSAGE(unowned.peak > 0.001F, "peak %f", unowned.peak);
    engine->Stop(engine->GetCategory("Global"), 1);
    const Heard stoppedAll = Listen(100ms);
    CHECK_MESSAGE(stoppedAll.peak == 0.0F, "peak %f", stoppedAll.peak);
  }

  void TestStreamingCue()
  {
    Engine engine;
    moho::IXACTWaveBank* const waves = engine.LoadStreamingWaves("Music");
    {
      const std::lock_guard<std::mutex> lock(gNotesMutex);
      gNotes.clear();
    }
    moho::AudioNotificationDesc prepared{};
    prepared.type = XACTNOTIFICATIONTYPE_WAVEBANKPREPARED;
    prepared.pWaveBank = reinterpret_cast<IXACT3WaveBank*>(waves);
    CHECK(engine->RegisterNotification(&prepared) == S_OK);
    CHECK(engine->DoWork() == S_OK);
    {
      const std::lock_guard<std::mutex> lock(gNotesMutex);
      CHECK(gNotes.size() == 1U);
      if (!gNotes.empty()) {
        CHECK(gNotes[0].type == XACTNOTIFICATIONTYPE_WAVEBANKPREPARED);
        CHECK(gNotes[0].waveBank.pWaveBank == reinterpret_cast<IXACT3WaveBank*>(waves));
      }
    }

    moho::IXACTSoundBank* const sounds = engine.LoadSounds("Music");
    moho::IXACTCue* const cue = PlayCue(sounds, "Main_Menu");
    CHECK(StateOf(cue) == kCuePlaying);
    // Past the stream's first buffer, a second of sound: the mix read on from the file.
    const Heard first = Listen(1500ms);
    const Heard later = Listen(1500ms);
    std::printf("  Main_Menu: peak %.3f rms %.4f, then peak %.3f rms %.4f\n", first.peak, first.Rms(), later.peak, later.Rms());
    CHECK_MESSAGE(first.peak > 0.001F && first.Rms() > 0.0001F, "peak %f rms %f", first.peak, first.Rms());
    CHECK_MESSAGE(later.peak > 0.001F && later.Rms() > 0.0001F, "peak %f rms %f", later.peak, later.Rms());
    cue->Stop(1);
    CHECK(StateOf(cue) == kCueStopped);
    const Heard stopped = Listen(100ms);
    CHECK_MESSAGE(stopped.peak == 0.0F, "peak %f", stopped.peak);
    CHECK(cue->Destroy() == S_OK);
  }

  void TestVariables()
  {
    Engine engine;
    CHECK(engine->GetGlobalVariableIndex("No_Such_Variable") == kInvalid);
    float value = 0.0F;
    CHECK(engine->GetGlobalVariable(kInvalid, &value) < 0);
    CHECK(engine->SetGlobalVariable(kInvalid, 1.0F) < 0);

    const std::uint16_t speedOfSound = engine->GetGlobalVariableIndex("SpeedOfSound");
    CHECK(speedOfSound != kInvalid);
    CHECK(engine->GetGlobalVariable(speedOfSound, &value) == S_OK);
    CHECK_MESSAGE(value == 343.5F, "%f", value);

    const std::uint16_t zoom = engine->GetGlobalVariableIndex("ZoomPercent");
    CHECK(zoom != kInvalid);
    CHECK(engine->SetGlobalVariable(zoom, 42.0F) == S_OK);
    CHECK(engine->GetGlobalVariable(zoom, &value) == S_OK);
    CHECK_MESSAGE(value == 42.0F, "%f", value);
    // As XACT does: clamped to what the settings allow.
    CHECK(engine->SetGlobalVariable(zoom, 1000.0F) == S_OK);
    CHECK(engine->GetGlobalVariable(zoom, &value) == S_OK);
    CHECK_MESSAGE(value == 100.0F, "%f", value);

    // The engine sets these on every 3D cue (ApplySettingsToCue, ComputeCueAngleDegrees).
    moho::IXACTSoundBank* const bank = engine.LoadInMemory("Interface");
    moho::IXACTCue* cue = nullptr;
    CHECK(bank->Prepare(bank->GetCueIndex("X_Main_Menu_On"), 0U, 0U, &cue) == S_OK);
    for (const char* const name : {"Distance", "DopplerPitchScalar", "OrientationAngle", "Angle"}) {
      const std::uint16_t variable = cue->GetVariableIndex(name);
      CHECK_MESSAGE(variable != kInvalid, "%s", name);
      CHECK_MESSAGE(cue->SetVariable(variable, 1.0F) == S_OK, "%s", name);
    }
    CHECK(cue->GetVariableIndex("ZoomPercent") == kInvalid); // a global one
    CHECK(cue->SetVariable(kInvalid, 1.0F) < 0);
    CHECK(cue->Destroy() == S_OK);
  }

  /// Gen_Fire_Loop loops in the Ambient category, under World and Global, on curves of its Distance
  /// and of the global Duck, and starts on a curve of AttackTime.
  [[nodiscard]] moho::IXACTCue* PlayFireLoop(moho::IXACTSoundBank* const bank)
  {
    moho::IXACTCue* const cue = PlayCue(bank, "Gen_Fire_Loop");
    std::this_thread::sleep_for(1200ms);
    return cue;
  }

  void TestVariablesAreHeard()
  {
    Engine engine;
    moho::IXACTSoundBank* const bank = engine.LoadInMemory("AmbientTest");
    moho::IXACTCue* const cue = PlayFireLoop(bank);
    const Heard nearby = Listen(500ms);

    const std::uint16_t distance = cue->GetVariableIndex("Distance");
    CHECK(cue->SetVariable(distance, 10000.0F) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard far = Listen(500ms);
    std::printf("  Distance 0 to 10000: rms %.5f to %.5f\n", nearby.Rms(), far.Rms());
    CHECK_MESSAGE(nearby.Rms() > 0.0001F, "rms %f", nearby.Rms());
    CHECK_MESSAGE(far.Rms() < nearby.Rms() * 0.1F, "rms %f to %f", nearby.Rms(), far.Rms());
    CHECK(cue->SetVariable(distance, 0.0F) == S_OK);
    std::this_thread::sleep_for(kFactPass);

    const std::uint16_t duck = engine->GetGlobalVariableIndex("Duck");
    CHECK(duck != kInvalid);
    const Heard undone = Listen(500ms);
    CHECK(engine->SetGlobalVariable(duck, 1.0F) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard ducked = Listen(500ms);
    std::printf("  Duck 0 to 1: rms %.5f to %.5f\n", undone.Rms(), ducked.Rms());
    CHECK_MESSAGE(ducked.Rms() < undone.Rms() * 0.6F, "rms %f to %f", undone.Rms(), ducked.Rms());

    cue->Stop(1);
    CHECK(cue->Destroy() == S_OK);
  }

  /// What the sound options do: World's volume over its children, and again and again.
  void TestCategoryHierarchy()
  {
    Engine engine;
    moho::IXACTSoundBank* const bank = engine.LoadInMemory("AmbientTest");
    const std::uint16_t world = engine->GetCategory("World");
    CHECK(world != kInvalid);
    moho::IXACTCue* const cue = PlayFireLoop(bank);
    const Heard full = Listen(1000ms);
    CHECK_MESSAGE(full.Rms() > 0.0001F, "rms %f", full.Rms());

    CHECK(engine->SetVolume(world, 0.5F) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard half = Listen(1000ms);
    std::printf("  World at 0.5: rms %.5f to %.5f\n", full.Rms(), half.Rms());
    CHECK_MESSAGE(half.Rms() > full.Rms() * 0.3F && half.Rms() < full.Rms() * 0.75F, "rms %f to %f", full.Rms(), half.Rms());

    CHECK(engine->SetVolume(world, 0.0F) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard muted = Listen(300ms);
    CHECK_MESSAGE(muted.peak == 0.0F, "peak %f", muted.peak);

    for (int times = 0; times < 20; ++times) {
      CHECK(engine->SetVolume(world, 1.0F) == S_OK);
    }
    std::this_thread::sleep_for(kFactPass);
    const Heard again = Listen(1000ms);
    std::printf("  World at 1 for the 21st time: rms %.5f\n", again.Rms());
    CHECK_MESSAGE(again.Rms() > full.Rms() * 0.6F && again.Rms() < full.Rms() * 1.6F, "rms %f to %f", full.Rms(), again.Rms());

    cue->Stop(1);
    CHECK(cue->Destroy() == S_OK);
  }

  void TestCategories()
  {
    Engine engine;
    CHECK(engine->GetCategory("No_Such_Category") == kInvalid);
    const std::uint16_t global = engine->GetCategory("Global");
    const std::uint16_t music = engine->GetCategory("Music");
    CHECK(global != kInvalid);
    CHECK(music != kInvalid);

    engine.LoadStreamingWaves("Music");
    moho::IXACTSoundBank* const sounds = engine.LoadSounds("Music");
    moho::IXACTCue* const cue = PlayCue(sounds, "Main_Menu");

    const Heard loud = Listen(500ms);
    CHECK_MESSAGE(loud.peak > 0.001F, "peak %f", loud.peak);

    CHECK(engine->SetVolume(music, 0.0F) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard muted = Listen(300ms);
    CHECK_MESSAGE(muted.peak == 0.0F, "muted Music: peak %f", muted.peak);
    CHECK(engine->SetVolume(music, 1.0F) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard back = Listen(300ms);
    CHECK_MESSAGE(back.peak > 0.001F, "peak %f", back.peak);

    // A parent's volume reaches its children: SND_Mute mutes Global.
    CHECK(engine->SetVolume(global, 0.0F) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard mutedParent = Listen(300ms);
    CHECK_MESSAGE(mutedParent.peak == 0.0F, "muted Global: peak %f", mutedParent.peak);
    CHECK(engine->SetVolume(global, 1.0F) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard backParent = Listen(300ms);
    CHECK_MESSAGE(backParent.peak > 0.001F, "peak %f", backParent.peak);

    // Paused in its category, it is still there, and silent.
    CHECK(engine->Pause(music, 1) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard paused = Listen(200ms);
    CHECK_MESSAGE(paused.peak == 0.0F, "peak %f", paused.peak);
    CHECK(engine->Pause(music, 0) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard resumed = Listen(200ms);
    CHECK_MESSAGE(resumed.peak > 0.001F, "peak %f", resumed.peak);

    // As authored: Main_Menu goes out on a curve of ReleaseTime.
    engine->Stop(music, 0);
    CHECK(StateOf(cue) == kCueStopping);
    CHECK(WaitStopped(engine, cue, 10s));
    const Heard stopped = Listen(100ms);
    CHECK_MESSAGE(stopped.peak == 0.0F, "peak %f", stopped.peak);
    CHECK(cue->Destroy() == S_OK);
  }

  /// UI_Menu_MouseDown may play twice at a time, and a third fails: XACT's FAIL, the behaviour of
  /// most of the game's cues. The count must stay right whatever failed or never played.
  void TestInstanceLimit()
  {
    Engine engine;
    moho::IXACTSoundBank* const bank = engine.LoadInMemory("Interface");
    const std::uint16_t index = bank->GetCueIndex("UI_Menu_MouseDown");
    CHECK(index != kInvalid);

    // Twice round, with failures and prepared cues gone in between.
    for (int round = 0; round < 2; ++round) {
      moho::IXACTCue* first = nullptr;
      moho::IXACTCue* second = nullptr;
      CHECK(bank->Play(index, 0U, 0U, &first) == S_OK);
      CHECK(bank->Play(index, 0U, 0U, &second) == S_OK);
      for (int fails = 0; fails < 3; ++fails) {
        moho::IXACTCue* third = reinterpret_cast<moho::IXACTCue*>(bank);
        const std::int32_t result = bank->Play(index, 0U, 0U, &third);
        CHECK_MESSAGE(result == kInstanceLimit, "round %d, try %d: 0x%08X", round, fails, static_cast<unsigned>(result));
        CHECK(third == nullptr);
        if (third != nullptr) {
          third->Destroy();
        }
      }
      if (first != nullptr) {
        CHECK(WaitStopped(engine, first, 5s));
        first->Destroy();
      }
      if (second != nullptr) {
        CHECK(WaitStopped(engine, second, 5s));
        second->Destroy();
      }

      for (int times = 0; times < 4; ++times) {
        moho::IXACTCue* prepared = nullptr;
        CHECK(bank->Prepare(index, 0U, 0U, &prepared) == S_OK);
        if (times % 2 == 0) {
          prepared->Stop(1);
          CHECK(StateOf(prepared) == kCueStopped); // never played, and stopped
        }
        CHECK(prepared->Destroy() == S_OK);
      }
    }
  }

  void TestForegroundAndFocus()
  {
    Engine engine;
    engine.LoadStreamingWaves("Music");
    moho::IXACTSoundBank* const sounds = engine.LoadSounds("Music");
    moho::IXACTCue* const cue = PlayCue(sounds, "Main_Menu");
    const Heard playing = Listen(300ms);
    CHECK(playing.frames > 0U);
    CHECK_MESSAGE(playing.peak > 0.001F, "peak %f", playing.peak);

    const auto expectPausedAndBack = [&](void (*const set)(int)) {
      gStreamClosed = 0;
      const auto asked = std::chrono::steady_clock::now();
      set(0);
      std::this_thread::sleep_for(500ms);
      const auto closed = std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(gStreamClosed.load())) - asked;
      const auto closedAfter = std::chrono::duration_cast<std::chrono::milliseconds>(closed).count();
      std::printf("  the stream closed %lld ms after the word\n", static_cast<long long>(closedAfter));
      CHECK(gStreamClosed.load() != 0);
      const Heard away = ListenAway(300ms);
      CHECK_MESSAGE(away.frames == 0U, "frames %llu", static_cast<unsigned long long>(away.frames));
      CHECK(StateOf(cue) == kCuePlaying);
      set(1);
      const Heard back = Listen(300ms);
      CHECK(back.frames > 0U);
      CHECK_MESSAGE(back.peak > 0.001F, "peak %f", back.peak);
    };
    expectPausedAndBack(&FAudioAndroid_SetForeground);
    expectPausedAndBack(&FAudioAndroid_SetAudioFocus);

    // Both away, one back: still away.
    FAudioAndroid_SetForeground(0);
    FAudioAndroid_SetAudioFocus(0);
    FAudioAndroid_SetForeground(1);
    std::this_thread::sleep_for(150ms);
    const Heard oneBack = ListenAway(200ms);
    CHECK_MESSAGE(oneBack.frames == 0U, "frames %llu", static_cast<unsigned long long>(oneBack.frames));
    FAudioAndroid_SetAudioFocus(1);
    const Heard bothBack = Listen(200ms);
    CHECK(bothBack.frames > 0U);

    cue->Stop(1);
    CHECK(cue->Destroy() == S_OK);
  }

  /// Away and back at once, also while the stream is just starting: it plays on after each.
  void TestAwayAndBackAtOnce()
  {
    for (int round = 0; round < 5; ++round) {
      Engine engine;
      engine.LoadStreamingWaves("Music");
      moho::IXACTSoundBank* const sounds = engine.LoadSounds("Music");
      moho::IXACTCue* const cue = PlayCue(sounds, "Main_Menu");
      for (int times = 0; times < 20; ++times) {
        FAudioAndroid_SetForeground(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(times % 8));
        FAudioAndroid_SetForeground(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(times % 8));
      }
      const Heard heard = Listen(300ms);
      CHECK_MESSAGE(heard.frames > 0U && heard.peak > 0.001F, "round %d: frames %llu peak %f", round, static_cast<unsigned long long>(heard.frames), heard.peak);
      cue->Stop(1);
      CHECK(cue->Destroy() == S_OK);
    }
  }

  /// The game goes on behind the activity, and its sounds with it: what played while away ends, and
  /// nothing is left to start on the return.
  void TestSoundsEndWhileAway()
  {
    Engine engine;
    moho::IXACTSoundBank* const bank = engine.LoadInMemory("Interface");
    moho::IXACTCue* const cue = PlayCue(bank, "UI_Menu_MouseDown");
    FAudioAndroid_SetForeground(0);
    CHECK(StateOf(cue) == kCuePlaying);
    CHECK(WaitStopped(engine, cue, 5s));
    FAudioAndroid_SetForeground(1);
    const Heard back = Listen(200ms);
    CHECK(back.frames > 0U);
    CHECK_MESSAGE(back.peak == 0.0F, "peak %f", back.peak);
    CHECK(cue->Destroy() == S_OK);
  }

  /// Headphones out, Bluetooth gone: the stream goes and a new one takes over, with the cue playing on.
  void TestDeviceLost()
  {
    Engine engine;
    engine.LoadStreamingWaves("Music");
    moho::IXACTSoundBank* const sounds = engine.LoadSounds("Music");
    moho::IXACTCue* const cue = PlayCue(sounds, "Main_Menu");
    const Heard before = Listen(300ms);
    CHECK_MESSAGE(before.peak > 0.001F, "peak %f", before.peak);

    const auto waitForStream = [](const int count) {
      const auto end = std::chrono::steady_clock::now() + 3s;
      while (gStreamsOpened.load() < count && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(10ms);
      }
      CHECK(gStreamsOpened.load() == count);
    };

    const int opened = gStreamsOpened.load();
    gLoseDevice = true;
    waitForStream(opened + 1);
    std::this_thread::sleep_for(100ms);
    const Heard after = Listen(300ms);
    CHECK(after.frames > 0U);
    CHECK_MESSAGE(after.peak > 0.001F, "peak %f", after.peak);
    CHECK(StateOf(cue) == kCuePlaying);

    // Lost while the output is paused: found on the return.
    FAudioAndroid_SetForeground(0);
    gLoseDevice = true;
    std::this_thread::sleep_for(150ms);
    FAudioAndroid_SetForeground(1);
    waitForStream(opened + 2);
    std::this_thread::sleep_for(100ms);
    const Heard afterAway = Listen(300ms);
    CHECK_MESSAGE(afterAway.peak > 0.001F, "peak %f", afterAway.peak);

    cue->Stop(1);
    CHECK(cue->Destroy() == S_OK);
  }

  /// A stream whose start got lost takes nothing and says nothing: the output does not spin on it,
  /// and a new stream takes over. So it does after a start that fails.
  void TestStreamThatTakesNothing()
  {
    Engine engine;
    engine.LoadStreamingWaves("Music");
    moho::IXACTSoundBank* const sounds = engine.LoadSounds("Music");
    moho::IXACTCue* const cue = PlayCue(sounds, "Main_Menu");
    CHECK(Listen(200ms).peak > 0.001F);

    const auto waitForStreams = [](const int count) {
      const auto end = std::chrono::steady_clock::now() + 6s;
      while (gStreamsOpened.load() < count && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(10ms);
      }
      CHECK(gStreamsOpened.load() >= count);
    };

    const int opened = gStreamsOpened.load();
    gEmptyWrites = 0;
    gTakesNothing = true;
    std::this_thread::sleep_for(1s);
    const int emptyWrites = gEmptyWrites.load();
    std::printf("  %d empty writes in a second\n", emptyWrites);
    CHECK_MESSAGE(emptyWrites > 0 && emptyWrites < 1100, "%d empty writes", emptyWrites);
    waitForStreams(opened + 1);
    gTakesNothing = false;
    std::this_thread::sleep_for(100ms);
    CHECK(Listen(300ms).peak > 0.001F);

    const int beforeFailure = gStreamsOpened.load();
    FAudioAndroid_SetForeground(0);
    std::this_thread::sleep_for(100ms);
    gStartFails = true;
    FAudioAndroid_SetForeground(1);
    waitForStreams(beforeFailure + 2);
    std::this_thread::sleep_for(100ms);
    CHECK(Listen(300ms).peak > 0.001F);

    cue->Stop(1);
    CHECK(cue->Destroy() == S_OK);
  }

  /// No output to be had: the game's sounds go on unheard all the same, what the output says of it
  /// stops, and the first stream that opens plays what is going on by then.
  void TestNoOutput()
  {
    static std::atomic<int> failureLines{0};
    failureLines = 0;
    FAudioAndroid_SetLogHandler([](const char* const line) {
      if (std::strstr(line, "openStream") != nullptr) {
        ++failureLines;
      }
    });
    gNoOutput = true;
    const int triesBefore = gStreamsOpened.load();
    {
      Engine engine;
      moho::IXACTSoundBank* const bank = engine.LoadInMemory("Interface");
      moho::IXACTCue* const cue = PlayCue(bank, "UI_Menu_MouseDown");
      CHECK(WaitStopped(engine, cue, 5s));
      CHECK(cue->Destroy() == S_OK);

      const auto end = std::chrono::steady_clock::now() + 10s;
      while (gStreamsOpened.load() - triesBefore <= 10 && std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(50ms);
      }
      CHECK(gStreamsOpened.load() - triesBefore > 10);
      CHECK_MESSAGE(failureLines.load() > 0 && failureLines.load() <= 8, "%d lines", failureLines.load());

      gNoOutput = false;
      engine.LoadStreamingWaves("Music");
      moho::IXACTSoundBank* const sounds = engine.LoadSounds("Music");
      moho::IXACTCue* const music = PlayCue(sounds, "Main_Menu");
      std::this_thread::sleep_for(800ms);
      const Heard heard = Listen(300ms);
      CHECK_MESSAGE(heard.peak > 0.001F, "peak %f", heard.peak);
      music->Stop(1);
      CHECK(music->Destroy() == S_OK);
    }
    FAudioAndroid_SetLogHandler(nullptr);
  }

  /// The program can end while it is away, with the output's thread waiting for the return.
  void TestShutdownWhileAway()
  {
    {
      Engine engine;
      engine.LoadStreamingWaves("Music");
      moho::IXACTSoundBank* const sounds = engine.LoadSounds("Music");
      moho::IXACTCue* const cue = PlayCue(sounds, "Main_Menu");
      FAudioAndroid_SetForeground(0);
      std::this_thread::sleep_for(kFactPass);
      cue->Stop(1);
      CHECK(cue->Destroy() == S_OK);
    }
    FAudioAndroid_SetForeground(1);
  }

  void TestX3DAudio()
  {
    std::uint8_t instance[0x14] = {};
    X3DAudioInitialize(3U, 343.5F, instance);

    moho::Audio3DListener listener{};
    listener.mOrientFront.z = 1.0F;
    listener.mOrientTop.y = 1.0F;

    // AudioEngine.cpp's default curve: flat, in its layout.
    struct Point
    {
      float distance;
      float setting;
    };
    struct Curve
    {
      const Point* points;
      std::uint32_t count;
    };
    static const Point kFlatPoints[2] = {{0.0F, 1.0F}, {1.0F, 1.0F}};
    static const Curve kFlat = {kFlatPoints, 2U};

    moho::Audio3DEmitter emitter{};
    emitter.mChannelCount = 1U;
    emitter.mCurveDistanceScaler = 1.0F;
    emitter.mOrientFront.z = 1.0F;
    emitter.mOrientTop.y = 1.0F;
    emitter.mVolumeCurve = const_cast<Curve*>(&kFlat);
    emitter.mLfeCurve = const_cast<Curve*>(&kFlat);

    float matrix[2] = {};
    float delays[2] = {};
    moho::Audio3DDspSettings settings{};
    settings.mMatrixCoefficients = matrix;
    settings.mDelayTimes = delays;
    settings.mSrcChannelCount = 1U;
    settings.mDstChannelCount = 2U;

    emitter.mPosition = moho::Audio3DVector{10.0F, 0.0F, 0.0F};
    X3DAudioCalculate(instance, &listener, &emitter, kCalculateMatrixDopplerAngle, &settings);
    std::printf("  right: %.3f %.3f, distance %.2f, doppler %.2f, angle %.3f\n", matrix[0], matrix[1], settings.mEmitterToListenerDistance, settings.mDopplerFactor, settings.mEmitterToListenerAngle);
    CHECK_MESSAGE(matrix[1] > matrix[0], "left %f right %f", matrix[0], matrix[1]);
    CHECK_MESSAGE(std::fabs(settings.mEmitterToListenerDistance - 10.0F) < 0.001F, "%f", settings.mEmitterToListenerDistance);
    CHECK_MESSAGE(settings.mDopplerFactor == 1.0F, "%f", settings.mDopplerFactor);

    emitter.mPosition = moho::Audio3DVector{-10.0F, 0.0F, 0.0F};
    X3DAudioCalculate(instance, &listener, &emitter, kCalculateMatrixDopplerAngle, &settings);
    CHECK_MESSAGE(matrix[0] > matrix[1], "left %f right %f", matrix[0], matrix[1]);

    emitter.mPosition = moho::Audio3DVector{0.0F, 0.0F, 10.0F};
    X3DAudioCalculate(instance, &listener, &emitter, kCalculateMatrixDopplerAngle, &settings);
    CHECK_MESSAGE(std::fabs(matrix[0] - matrix[1]) < 0.01F, "left %f right %f", matrix[0], matrix[1]);

    // Coming nearer sounds higher, with the scaler the fields carry over.
    emitter.mDopplerScaler = 1.0F;
    emitter.mVelocity = moho::Audio3DVector{0.0F, 0.0F, -100.0F};
    X3DAudioCalculate(instance, &listener, &emitter, kCalculateMatrixDopplerAngle, &settings);
    CHECK_MESSAGE(settings.mDopplerFactor > 1.0F, "%f", settings.mDopplerFactor);

    // A flat curve does not fall with the distance; a curve that does shows the curves arrive.
    const float flatSum = matrix[0] + matrix[1];
    static const Point kFallingPoints[2] = {{0.0F, 1.0F}, {1.0F, 0.0F}};
    static const Curve kFalling = {kFallingPoints, 2U};
    emitter.mVolumeCurve = const_cast<Curve*>(&kFalling);
    emitter.mCurveDistanceScaler = 20.0F;
    X3DAudioCalculate(instance, &listener, &emitter, kCalculateMatrixDopplerAngle, &settings);
    const float halfSum = matrix[0] + matrix[1];
    CHECK_MESSAGE(std::fabs(halfSum - flatSum * 0.5F) < 0.01F, "flat %f, half way down the curve %f", flatSum, halfSum);
  }

  /// The whole way: a matrix from X3DAudio on a playing mono cue, in the mix's channels.
  void TestMatrixIsHeard()
  {
    Engine engine;
    moho::IXACTSoundBank* const bank = engine.LoadInMemory("Interface");
    moho::IXACTCue* const cue = PlayCue(bank, "X_Main_Menu_On");
    const float toTheRight[2] = {0.0F, 1.0F};
    CHECK(cue->SetMatrixCoefficients(1U, 2U, toTheRight) == S_OK);
    std::this_thread::sleep_for(kFactPass);
    const Heard heard = Listen(300ms);
    std::printf("  to the right: left %.4f right %.4f\n", heard.peakLeft, heard.peakRight);
    CHECK_MESSAGE(heard.peakRight > 0.001F && heard.peakLeft == 0.0F, "left %f right %f", heard.peakLeft, heard.peakRight);
    CHECK(cue->SetMatrixCoefficients(1U, 3U, toTheRight) < 0); // not the mix's channels
    cue->Stop(1);
    CHECK(cue->Destroy() == S_OK);
  }

  void TestFiles()
  {
    CHECK(FAudio_fopen((gSounds + "/No_Such_File").c_str()) == nullptr);

    FAudioIOStream* const file = FAudio_fopen((gSounds + "/SupCom.xgs").c_str());
    CHECK(file != nullptr);
    if (file != nullptr) {
      char magic[4] = {};
      CHECK(file->read(file->data, magic, sizeof(magic), 1U) == 1U);
      CHECK(std::memcmp(magic, "XGSF", 4U) == 0);
      CHECK(file->seek(file->data, 1, FAUDIO_SEEK_SET) == 1);
      CHECK(file->read(file->data, magic, 1U, sizeof(magic)) == sizeof(magic));
      CHECK(magic[0] == 'G');
      FAudio_close(file);
    }

    char bytes[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    FAudioIOStream* const memory = FAudio_memopen(bytes, sizeof(bytes));
    char got[4] = {};
    CHECK(memory->seek(memory->data, -2, FAUDIO_SEEK_END) == 6);
    CHECK(memory->read(memory->data, got, sizeof(got), 1U) == 0U); // less than one item left
    CHECK(memory->read(memory->data, got, 1U, sizeof(got)) == 2U);
    CHECK(got[0] == 'g' && got[1] == 'h');
    CHECK(memory->read(memory->data, got, 0U, 1U) == 0U);
    CHECK(memory->seek(memory->data, 1, FAUDIO_SEEK_END) == -1);
    CHECK(FAudio_memptr(memory, 3U) == reinterpret_cast<std::uint8_t*>(bytes) + 3);
    FAudio_close(memory);
  }

  struct Test
  {
    const char* name;
    void (*run)();
  };

  constexpr Test kTests[] = {
    {"Creation", &TestCreation},
    {"NoSoundsFolder", &TestNoSoundsFolder},
    {"FinalMixFormat", &TestFinalMixFormat},
    {"BanksThatDoNotLoad", &TestBanksThatDoNotLoad},
    {"InMemoryCue", &TestInMemoryCue},
    {"StreamingCue", &TestStreamingCue},
    {"Variables", &TestVariables},
    {"VariablesAreHeard", &TestVariablesAreHeard},
    {"Categories", &TestCategories},
    {"CategoryHierarchy", &TestCategoryHierarchy},
    {"InstanceLimit", &TestInstanceLimit},
    {"ForegroundAndFocus", &TestForegroundAndFocus},
    {"AwayAndBackAtOnce", &TestAwayAndBackAtOnce},
    {"SoundsEndWhileAway", &TestSoundsEndWhileAway},
    {"DeviceLost", &TestDeviceLost},
    {"StreamThatTakesNothing", &TestStreamThatTakesNothing},
    {"NoOutput", &TestNoOutput},
    {"ShutdownWhileAway", &TestShutdownWhileAway},
    {"X3DAudio", &TestX3DAudio},
    {"MatrixIsHeard", &TestMatrixIsHeard},
    {"Files", &TestFiles},
  };
} // namespace

int main(const int argc, char** const argv)
{
  if (argc != 2) {
    std::printf("Usage: %s <the game's sounds folder>\n", argv[0]);
    return 2;
  }
  gSounds = argv[1];
  FAudioAndroid_SetMixObserver(&HearMix, nullptr);

  int failedTests = 0;
  for (const Test& test : kTests) {
    std::printf("%s\n", test.name);
    std::fflush(stdout);
    gFailedChecks = 0;
    test.run();
    if (gFailedChecks != 0) {
      ++failedTests;
    }
  }
  std::printf("%d tests, %d failed\n", static_cast<int>(sizeof(kTests) / sizeof(kTests[0])), failedTests);
  return failedTests != 0 ? 1 : 0;
}
