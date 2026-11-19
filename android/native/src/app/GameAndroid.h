#pragma once

// What the app's files give each other (src/app, src/host): the game's window, its report and its sound.

#include <string>

struct SDL_Window;

namespace platform::android
{
  /// The game's window: the one with the keyboard's focus, else the first. Null before SDL made it.
  [[nodiscard]] SDL_Window* GameWindow();
  /// A line of the game's report (GameMainAndroid.cpp, FAF_GAME_REPORT), which also goes to the system log. On any thread.
  void Report(const std::string& line);

  /// The game's sound follows the activity and the audio focus (SoundAndroid.cpp): the activity is in front or not, it
  /// holds the audio focus or not. On any thread.
  void SetForeground(bool front);
  void SetAudioFocus(bool held);
  /// The sound engine has opened its output (GameMainAndroid.cpp sees it in the engine's log). On any thread.
  void SoundStarted();
} // namespace platform::android
