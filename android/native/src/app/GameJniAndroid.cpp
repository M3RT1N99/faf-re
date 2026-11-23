// The natives of the app's Game class (android/app/.../Game.java) that the game on SDL's activity has: what the activity
// tells the program, and what NetworkGameService asks of it; and the program's word to the activity for a finger's buzz.

#include <jni.h>

#include <string>

#include <SDL3/SDL_system.h>

#include "GameAndroid.h"
#include "gpg/core/utils/Logging.h"
#include "moho/net/NetPeerTransport.h"

namespace moho
{
  // The patched CNetUDPConnector.cpp's (patches/README.md): how many UDP connectors have their socket open.
  [[nodiscard]] int NET_OpenUDPConnectors() noexcept;
  // The patched CUIWorldViewEvents.cpp's: whether a game goes on behind the game's UI.
  [[nodiscard]] bool UI_GamePlaying() noexcept;
} // namespace moho

/// The activity came to the front or went to the back.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_foreground(JNIEnv*, jclass, const jboolean front)
{
  platform::android::SetForeground(front != JNI_FALSE);
}

/// The activity got the audio focus or lost it.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_audioFocus(JNIEnv*, jclass, const jboolean held)
{
  platform::android::SetAudioFocus(held != JNI_FALSE);
}

/// Whether a network lobby or game has its connections: a UDP connector with its socket open, or a peer link of the
/// game's own (native mode, /nativenet), which runs on libjuice's sockets and registers a transport for the game's
/// datagrams to its peer. The LAN screen's search for games has a datagram socket, and a skirmish none.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_networkInUse(JNIEnv*, jclass)
{
  return moho::NET_OpenUDPConnectors() > 0 || moho::NET_PeerTransportsActive() ? JNI_TRUE : JNI_FALSE;
}

/// Whether a game goes on behind the game's UI, as opposed to the menus and the score screen.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_m3rt1n99_fafre_Game_gamePlaying(JNIEnv*, jclass)
{
  return moho::UI_GamePlaying() ? JNI_TRUE : JNI_FALSE;
}

/// A line of the game's report from the app's side.
extern "C" JNIEXPORT void JNICALL Java_io_github_m3rt1n99_fafre_Game_report(JNIEnv* const env, jclass, const jstring line)
{
  const char* const text = env->GetStringUTFChars(line, nullptr);
  if (text == nullptr) {
    return; // OutOfMemoryError is pending
  }
  platform::android::Report(text);
  env->ReleaseStringUTFChars(line, text);
}

void platform::android::Buzz()
{
  // Game.BUZZ: the second of the commands SDL leaves to the app.
  constexpr Uint32 kBuzz = 0x8001;
  if (!SDL_SendAndroidMessage(kBuzz, 0)) {
    gpg::Warnf("Buzz: no word to the activity: %s", SDL_GetError());
  }
}
