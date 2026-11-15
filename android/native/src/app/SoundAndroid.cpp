// The game's sound follows the activity and Android's audio focus (android/README.md, Game), as on the earlier host: it
// plays while the activity is in front and holds the focus, and pauses in the back and while another app has the focus
// (a call, a video, a voice assistant). SDL pauses its audio on Android only where it holds the program in the back
// (SDL_HINT_ANDROID_BLOCK_ON_PAUSE, which GameMainAndroid.cpp turns off, as the program goes on there) and knows nothing
// of the audio focus, which the activity asks for (GameActivity.java).
//
// The pause is SDL's own for its blocking case (Android_PauseAudio in SDL_androidevents.c): AAUDIO_PauseDevices and
// OPENSLES_PauseDevices pause the output under all of the program's audio devices, the game's sound (FAudio) and a
// movie's (SdlMovieSink) alike. Patched (patches/README.md), it holds for the streams SDL builds meanwhile as well: for
// a device the program opens, and the one AAudio's driver builds anew when Android disconnects a stream, a paused one
// too, as it does when the output device changes. They run on SDL's main thread, the program's, where SDL makes those
// calls and where the program opens and closes its devices.

#include <atomic>

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include "GameAndroid.h"

extern "C"
{
  // SDL's (src/audio/aaudio/SDL_aaudio.h, src/audio/openslES/SDL_openslES.h): each does nothing where its driver is not
  // the one SDL took (AAudio from Android 8.1 on, OpenSL ES before).
  void AAUDIO_PauseDevices(void);
  void AAUDIO_ResumeDevices(void);
  void OPENSLES_PauseDevices(void);
  void OPENSLES_ResumeDevices(void);
}

namespace
{
  // The activity's word, from Android's main thread. The program starts with the activity in front.
  std::atomic<bool> gFront{true};
  std::atomic<bool> gFocus{true};
  // SDL's main thread's: whether the output is paused.
  bool gPaused = false;

  void Apply(void*)
  {
    // Before SDL's audio is up there is no output to pause; the sound's start applies what holds then.
    if (SDL_GetCurrentAudioDriver() == nullptr) {
      return;
    }
    const bool front = gFront.load();
    const bool pause = !front || !gFocus.load();
    if (pause == gPaused) {
      return;
    }
    gPaused = pause;
    if (pause) {
      AAUDIO_PauseDevices();
      OPENSLES_PauseDevices();
      platform::android::Report(front ? "sound: paused, another app has the audio focus" : "sound: paused in the back");
    } else {
      AAUDIO_ResumeDevices();
      OPENSLES_ResumeDevices();
      platform::android::Report("sound: plays again");
    }
  }
} // namespace

void platform::android::SetForeground(const bool front)
{
  gFront = front;
  (void)SDL_RunOnMainThread(&Apply, nullptr, false);
}

void platform::android::SetAudioFocus(const bool held)
{
  gFocus = held;
  (void)SDL_RunOnMainThread(&Apply, nullptr, false);
}

void platform::android::SoundStarted()
{
  (void)SDL_RunOnMainThread(&Apply, nullptr, false);
}
