#pragma once

// How the touch layer (src/touch) measures and rounds: as the app's Java gesture layer did, whose tests the port passes
// (tests/griff/Touch*Tests.cpp). A distance is taken in double whatever its arguments were, a float rounds half up, and a
// float that becomes an int saturates, NaN giving 0, so a number from elsewhere never makes undefined behaviour.

#include <cmath>
#include <cstdint>
#include <limits>

namespace touch
{
  /// The length of (x, y), in double: float differences are widened first, as the comparisons with the slops want.
  [[nodiscard]] inline double Hypot(const double x, const double y)
  {
    return std::hypot(x, y);
  }

  /// The int nearest to `value`, halves up (-2.5 gives -2), saturating; NaN gives 0.
  [[nodiscard]] inline int Round(const float value)
  {
    const double rounded = std::floor(static_cast<double>(value) + 0.5);
    if (std::isnan(rounded)) {
      return 0;
    }
    if (rounded <= static_cast<double>(std::numeric_limits<int>::min())) {
      return std::numeric_limits<int>::min();
    }
    if (rounded >= static_cast<double>(std::numeric_limits<int>::max())) {
      return std::numeric_limits<int>::max();
    }
    return static_cast<int>(rounded);
  }

  /// `value` towards zero as an int, saturating; NaN gives 0.
  [[nodiscard]] inline int ToInt(const float value)
  {
    if (std::isnan(value)) {
      return 0;
    }
    if (value <= static_cast<float>(std::numeric_limits<int>::min())) {
      return std::numeric_limits<int>::min();
    }
    if (value >= static_cast<float>(std::numeric_limits<int>::max())) {
      return std::numeric_limits<int>::max();
    }
    return static_cast<int>(value);
  }

  /// `value` towards zero as an int, saturating; NaN gives 0: Java's (int) of a double.
  [[nodiscard]] inline int ToInt(const double value)
  {
    if (std::isnan(value)) {
      return 0;
    }
    if (value <= static_cast<double>(std::numeric_limits<int>::min())) {
      return std::numeric_limits<int>::min();
    }
    if (value >= static_cast<double>(std::numeric_limits<int>::max())) {
      return std::numeric_limits<int>::max();
    }
    return static_cast<int>(value);
  }

  /// Whether two floats are the same number: NaN is NaN, and -0 is not 0.
  [[nodiscard]] inline bool SameFloat(const float a, const float b)
  {
    if (std::isnan(a) || std::isnan(b)) {
      return std::isnan(a) && std::isnan(b);
    }
    return a == b && std::signbit(a) == std::signbit(b);
  }

  /// Times are milliseconds on one clock, the fingers' events' and the timer's.
  using Millis = std::int64_t;

  /// A time long before any other, which no "within so long" reaches.
  constexpr Millis kLongAgo = std::numeric_limits<Millis>::min() / 2;
} // namespace touch
