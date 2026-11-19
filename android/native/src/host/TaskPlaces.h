#pragma once

// The argument of the host's tasks on the program's thread (HostSdl.h).

#include <cstdint>


namespace hosttask
{
  /// A task's argument (host::Task).
  using Argument = std::uintptr_t;

  static_assert(sizeof(Argument) >= 8, "two places of the picture go in a task's argument");

  /// Up to two places of the picture, each coordinate below 65536, or a place and two other numbers.
  [[nodiscard]] inline Argument Places(const int x0, const int y0, const int x1 = 0, const int y1 = 0)
  {
    const auto word = [](const int value) { return static_cast<Argument>(value) & 0xFFFFU; };
    return word(x0) | word(y0) << 16 | word(x1) << 32 | word(y1) << 48;
  }

  [[nodiscard]] inline int Coordinate(const Argument places, const int index)
  {
    return static_cast<int>(places >> (16 * index) & 0xFFFFU);
  }

  /// A number that Places took within -32768 to 32767, such as a velocity, read back with its sign.
  [[nodiscard]] inline int SignedCoordinate(const Argument places, const int index)
  {
    return static_cast<std::int16_t>(Coordinate(places, index));
  }
} // namespace hosttask
