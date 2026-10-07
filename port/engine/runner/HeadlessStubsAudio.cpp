// Sound (moho/audio/AudioEngine.cpp, CUserSoundManager.cpp, XAudioError.cpp) for the Android headless
// runner; see HeadlessStubs.h.
//
// The runner state these follow is the Windows runner on a machine without XACT, the state of the
// M3a reference runs (T1-T3): the M3a T1 log has "Error in file /lua/SessionInit.lua : SND: Error
// retrieving XACT COM interface. Unknown XACT Error". The loader's SessionInit.lua reaches
// AudioSetLanguage (FAF lua/system/Localization.lua), whose binder (Sim.cpp, cfunc_AudioSetLanguageUserL) calls
// USER_GetSound(); that constructs a CUserSoundManager, whose member initialiser calls
// AudioEngine::Create("/sounds") (CUserSoundManager.cpp:806); Create runs func_InitSound
// (AudioEngine.cpp:1888) and the AudioEngine constructor, which throws XAudioError when XACT's COM
// object cannot be created (AudioEngine.cpp:1424). The exception leaves USER_GetSound, so
// `gUserSoundManager` stays null and every later call throws the same way; the error ends the script
// right after `__language` is set. Android has no XACT, so the same holds there.
//
// What func_InitSound leaves behind on Windows is a SoundConfiguration with no engines (the throwing
// AudioEngine was never registered). Every reader in the closure tests for a non-empty engine list
// before using it (Sim::Setup in Sim.cpp, CSndParams.cpp:1227, CSndVar.cpp:367, SND_FindEngine,
// SND_GetGlobalVarIndex), so it answers exactly as the null `sSoundConfiguration` here does.

#include "HeadlessStubs.h"

#include "moho/audio/AudioEngine.h"
#include "moho/audio/CUserSoundManager.h"
#include "moho/audio/IUserSoundManager.h"
#include "moho/audio/SofdecRuntime.h"
#include "moho/audio/XAudioError.h"

namespace moho
{
  namespace
  {
    // AudioEngine.cpp:1423 with func_SoundErrorCodeToMsg's text for the failure, as in the M3a log.
    constexpr const char* kNoXactError = "SND: Error retrieving XACT COM interface. Unknown XACT Error";
  } // namespace

  // XAudioError.cpp:8, the same (defaulted) destructor: the class's key function, so its vtable and
  // type_info come with it. XAudioError.cpp is not in the closure; only AudioEngine.cpp throws it.
  XAudioError::~XAudioError() = default;

  // AudioEngine.cpp:41. Null; see the top of this file for why that equals Windows' engine-less
  // configuration for every reader.
  boost::scoped_ptr<SoundConfiguration> sSoundConfiguration;

  // AudioEngine.cpp:1759. The pointer above is never set here (only func_InitSound allocates a
  // configuration, and it is not linked), so its destructor only has to exist for the scoped_ptr.
  SoundConfiguration::~SoundConfiguration()
  {
    FAF_RUNNER_STUB("SoundConfiguration::~SoundConfiguration");
  }

  // CUserSoundManager.cpp:842 in the no-XACT state: the CUserSoundManager constructor throws (see the
  // top of this file). Reached in every run, from the loader's SessionInit.lua.
  IUserSoundManager* USER_GetSound()
  {
    FAF_RUNNER_STUB("USER_GetSound");
    throw XAudioError(kNoXactError);
  }

  // CUserSoundManager.cpp:2330: a console command that calls USER_GetSound() first.
  void Con_DumpActiveLoops()
  {
    FAF_RUNNER_STUB("Con_DumpActiveLoops");
    (void)USER_GetSound();
  }

  // AudioEngine.cpp:2271 in the no-XACT state: the AudioEngine constructor throws. Its callers in the
  // closure (the AudioSetLanguage binder in Sim.cpp) run only after USER_GetSound()
  // returned a manager, which never happens here.
  boost::shared_ptr<AudioEngine> AudioEngine::Create(const gpg::StrArg voicePath)
  {
    (void)voicePath;
    FAF_RUNNER_STUB("AudioEngine::Create");
    throw XAudioError(kNoXactError);
  }

  // AudioEngine.cpp:2153: an empty pointer without registered engines.
  boost::shared_ptr<AudioEngine> SND_FindEngine(const gpg::StrArg bankName)
  {
    (void)bankName;
    FAF_RUNNER_STUB("SND_FindEngine");
    return {};
  }

  // AudioEngine.cpp:2068: false without registered engines (the output is left alone).
  bool SND_GetGlobalVarIndex(const gpg::StrArg variableName, std::uint16_t* const outVarIndex)
  {
    (void)variableName;
    (void)outVarIndex;
    FAF_RUNNER_STUB("SND_GetGlobalVarIndex");
    return false;
  }

  // AudioEngine.cpp:2511, :2539, :2781, :2305: members of an AudioEngine. No engine object exists in
  // this state (Create throws and SND_FindEngine finds none), so nothing can call them; their callers
  // are CSndParams.cpp, HSound.cpp and the AudioSetLanguage binder.
  bool AudioEngine::GetBankIndex(const gpg::StrArg bankName, std::uint16_t* const outBankId)
  {
    (void)bankName;
    (void)outBankId;
    FAF_RUNNER_STUB("AudioEngine::GetBankIndex");
    return false;
  }

  bool AudioEngine::GetCueIndex(const gpg::StrArg cueName, const std::uint16_t bankId, std::uint16_t* const outCueId)
  {
    (void)cueName;
    (void)bankId;
    (void)outCueId;
    FAF_RUNNER_STUB("AudioEngine::GetCueIndex");
    return false;
  }

  bool AudioEngine::IsStopped(IXACTCue* const cue) const
  {
    (void)cue;
    FAF_RUNNER_STUB("AudioEngine::IsStopped");
    return false;
  }

  void AudioEngine::Shutdown()
  {
    FAF_RUNNER_STUB("AudioEngine::Shutdown");
  }
} // namespace moho

// SofdecRuntime.cpp: the CRI ADX/Sofdec movie middleware. StartupHelpers.cpp calls it from
// CMovieManager's constructor and destructor and from MOV_GetDuration (a movie-UI binder); the movie
// manager is created only by the app's startup (CScApp), which the runner does not run, and no movie
// is ever played or measured in it.
extern "C" {
int ADXPC_SetupFileSystem(const char** const rootDirArgv)
{
  (void)rootDirArgv;
  FAF_RUNNER_STUB("ADXPC_SetupFileSystem");
  return 0;
}

void ADXPC_NoOpShutdownCallback()
{
  FAF_RUNNER_STUB("ADXPC_NoOpShutdownCallback");
}

void ADXM_SetupThrd(const moho::AdxmThreadStartupParams* const startupParams)
{
  (void)startupParams;
  FAF_RUNNER_STUB("ADXM_SetupThrd");
}

void ADXM_SetCbErr(const moho::AdxmErrorCallback callback, const std::int32_t callbackParam)
{
  (void)callback;
  (void)callbackParam;
  FAF_RUNNER_STUB("ADXM_SetCbErr");
}

void ADXM_Finish()
{
  FAF_RUNNER_STUB("ADXM_Finish");
}

void mwPlyInitSfdFx(moho::MwsfdInitPrm* const initParams)
{
  (void)initParams;
  FAF_RUNNER_STUB("mwPlyInitSfdFx");
}

void mwPlyFinishSfdFx()
{
  FAF_RUNNER_STUB("mwPlyFinishSfdFx");
}

// Leaves an all-zero header, which MOV_GetDuration reports as "not a valid SFD file".
void mwPlyGetHdrInf(const char* const buffer, const std::int32_t bufferSize, moho::MwsfdHdrInf* const hdrinf)
{
  (void)buffer;
  (void)bufferSize;
  FAF_RUNNER_STUB("mwPlyGetHdrInf");
  if (hdrinf != nullptr) {
    *hdrinf = moho::MwsfdHdrInf{};
  }
}
} // extern "C"
