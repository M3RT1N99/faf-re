#pragma once

// Android's performance hints for the game's threads (PerformanceAndroid.cpp): what the app's other files ask of them.

namespace performance
{
  /// Sets the engine's hooks for the render thread's frames and the sim's beats, which Android's hint sessions time, or says
  /// in the report why there are none. On the program's thread, before the engine starts.
  void Install();
  /// A frame with draws went to the screen (GameMainAndroid.cpp hears it from the Vulkan surface's present): its work on the
  /// render thread is done. On the render thread.
  void FramePresented();
} // namespace performance
