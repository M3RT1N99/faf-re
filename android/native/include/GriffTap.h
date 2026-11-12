#pragma once

// What a tap of the touch scheme Griff (android/TOUCH_UI.md, "Griff") uses: of the rail's buttons those its outcome used (the
// rule of the app's Rail.java), and FAF's reach for a unit beside it as the console's text. The program decides a tap
// (src/host/GriffAndroid.cpp); here the rules alone, for their tests (tests/wx/GriffTapTests.cpp).

#include <cstdio>
#include <limits>
#include <string>

namespace grifftap
{
  // TouchInput's RAIL_ bits.
  constexpr int kRailBox = 1;
  constexpr int kRailQueue = 2;
  constexpr int kRailAttack = 4;

  /// What a tap or a rest's lift turned out to be, for the rail's buttons it used.
  enum class Outcome
  {
    /// Box took one's own unit into the selection or out of it.
    kBoxToggled,
    /// Box found no unit of one's own: nothing was clicked.
    kBoxFoundNothing,
    /// Anything else, which Box was none of: FAF's UI took it, a command mode armed before, a rest's right click on a panel.
    kOther,
  };

  /// Which of the rail's buttons a tap that counted `rail` used: all that counted, as for any action, but Box only where it
  /// took or let go a unit, so that it stays lit through anything else; Box's tap on nothing is no action and uses none.
  [[nodiscard]] constexpr int Used(const int rail, const Outcome outcome)
  {
    switch (outcome) {
      case Outcome::kBoxToggled:
        return rail;
      case Outcome::kBoxFoundNothing:
        return 0;
      case Outcome::kOther:
        break;
    }
    return rail & ~kRailBox;
  }

  /// A float as the console's text, which the console reads back as the same float (std::atof, then a float: its
  /// ParseFloatToken): as many digits as a float needs for that, where %g's six round most reaches to another float.
  [[nodiscard]] inline std::string FloatText(const float value)
  {
    char text[32];
    std::snprintf(text, sizeof text, "%.*g", std::numeric_limits<float>::max_digits10, static_cast<double>(value));
    return text;
  }
} // namespace grifftap
