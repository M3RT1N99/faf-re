#pragma once

// Android's performance hints for the game's threads and the phone's thermal headroom (PerformanceAndroid.cpp): what the
// app's other files ask of them.

namespace performance
{
  /// Sets the engine's hooks for the render thread's frames and the sim's beats, which Android's hint sessions time, and
  /// starts the thermal headroom's watch; or says in the report why there is none. On the program's thread, before the
  /// engine starts.
  void Install();
  /// A frame with draws went to the screen (GameMainAndroid.cpp hears it from the Vulkan surface's present): its work on
  /// the render thread is done. On the render thread.
  void FramePresented();
  /// How much of the phone's thermal envelope is in use, as Android said last (read every 2 s): 1.0 is where Android
  /// throttles the phone (its thermal status SEVERE), and more is more of that. NaN until Android gave a reading, and on a
  /// phone that gives none. For a policy that lowers the game's settings as the phone heats up. On any thread.
  [[nodiscard]] float ThermalHeadroom() noexcept;
} // namespace performance
