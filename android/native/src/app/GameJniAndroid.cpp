// The natives of the app's Game class (android/app/.../Game.java) that the game on SDL's activity has: what the activity
// tells the program. The class's other natives are the earlier host's (src/host), which SDL's activity does not call.

#include <jni.h>

#include "GameAndroid.h"

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
