#pragma once
// The host on SDL's activity: what the activity's fingers give the game, its tasks and its input, as the earlier host's
// Win32 message layer gave them (include/HostQueue.h has the rules, src/host/HostSdl.cpp does it). A task runs on the
// game's thread in the order it was given: what was given before it has been taken, and the input it gives comes next,
// ahead of what was given after it. The input is the engine's window events, in the picture's pixels: Griff's tasks
// (GriffAndroid.cpp), the menus' fingers (MenuFingers.cpp) and the activity's input give it.

#include <cstdint>

#include <HostQueue.h>

namespace host
{
  using Task = hostqueue::Task;

  /// Any thread: runs `task` on the game's thread in the queue's order. Nothing runs before SDL has a window.
  void RunOnGameThread(Task task, std::uintptr_t argument);
  /// The same, in place of this task if it is still the last thing waiting, as Windows coalesces mouse moves.
  void RunNewestOnGameThread(Task task, std::uintptr_t argument);
  /// Runs `task` on the game's thread once nothing else waits, after the frame's paint where it is given outside the
  /// game's events (a frame's Lua). In place of this task if it still waits.
  void RunWhenIdle(Task task, std::uintptr_t argument);

  // --- Input, on the game's thread (in a task) --------------------------------------------------------------------
  // Positions are the picture's pixels. A press of a button within the double-click time and distance of the last is a
  // double click (hostqueue::DoubleClicks).

  /// The earlier host's numbers of the buttons (WIN32ANDROID_BUTTON_*), which Java's and Griff's tasks pass.
  enum Button : int
  {
    kButtonLeft = 0,
    kButtonRight = 1,
    kButtonMiddle = 2,
  };

  void MouseMove(int x, int y);
  /// A mouse's button at x, y: the cursor moves there first.
  void MouseButton(int button, bool down, int x, int y);
  /// A button as a finger gives it: its press is a finger's for the engine (PressFromFinger).
  void TouchButton(int button, bool down, int x, int y);
  /// `delta` in WHEEL_DELTA units of 120 a notch, positive away from the user.
  void MouseWheel(int delta, int x, int y);
  /// A key as Windows names it (VK_*) went down or up: Griff's Shift and Ctrl, the bar's. A keyboard's keys and its text
  /// take SDL's own way.
  void Key(unsigned virtualKey, bool down);

  /// How many milliseconds a press of `button` at x, y would still make a double click with the press before it (0:
  /// none, it would be a press of its own). Who wants two presses there, as a player who wants two single clicks, waits.
  [[nodiscard]] std::uint32_t DoubleClickWait(int button, int x, int y);
  /// Where the host's cursor is, in the picture's pixels.
  void CursorPos(int& x, int& y);

  // --- For the engine and the app -----------------------------------------------------------------------------------

  /// The game's thread: whether the press the engine takes now is a finger's (TouchButton), which in build mode only
  /// puts the building's ghost (patches/src/moho/ui/CUIWorldViewEvents.cpp.patch).
  [[nodiscard]] bool PressFromFinger();
  /// The tool hook's part (src/app/ScreenKeyboardAndroid.cpp sets the hook): an SDL event before the engine translates
  /// it. True where it was the host's and is taken.
  [[nodiscard]] bool OnSdlEvent(const void* sdlEvent);
} // namespace host
