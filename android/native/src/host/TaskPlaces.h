#pragma once

// The argument of the host's tasks on the program's thread (Win32Android_RunOnWindowThread).

#include <cstdint>

#include <Windows.h>

namespace hosttask
{
  static_assert(sizeof(WPARAM) >= 8, "two places of the picture go in a task's argument");

  /// Up to two places of the picture, each coordinate below 65536, or a place and two other numbers.
  [[nodiscard]] inline WPARAM Places(const int x0, const int y0, const int x1 = 0, const int y1 = 0)
  {
    const auto word = [](const int value) { return static_cast<WPARAM>(value) & 0xFFFFU; };
    return word(x0) | word(y0) << 16 | word(x1) << 32 | word(y1) << 48;
  }

  [[nodiscard]] inline int Coordinate(const WPARAM places, const int index)
  {
    return static_cast<int>(places >> (16 * index) & 0xFFFFU);
  }
} // namespace hosttask
