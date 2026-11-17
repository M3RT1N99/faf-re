// What a tap of the touch scheme Griff uses (include/GriffTap.h): the rail's buttons by what the tap turned out to be, and FAF's
// reach for a unit beside it as the console's text.

#include <cstdlib>
#include <string>

#include <GriffTap.h>

#include "Check.h"

namespace
{
  using grifftap::FloatText;
  using grifftap::Outcome;
  using grifftap::Used;

  constexpr int kBox = grifftap::kRailBox;
  constexpr int kPlus = grifftap::kRailQueue;

  /// As FAF's console reads a float (ParseFloatToken).
  float Read(const std::string& text)
  {
    return static_cast<float>(std::atof(text.c_str()));
  }
} // namespace

WXT_TEST(GriffTapBoxTakingAUnitUsesAllThatCounted)
{
  WXT_CHECK_EQ(Used(kBox | kPlus, Outcome::kBoxToggled), kBox | kPlus);
  WXT_CHECK_EQ(Used(kBox, Outcome::kBoxToggled), kBox);
}

WXT_TEST(GriffTapBoxsTapOnNothingUsesNone)
{
  // The mockup's (T1.11): nothing was clicked, and Box stays lit for the next try, with what else counted.
  WXT_CHECK_EQ(Used(kBox | kPlus, Outcome::kBoxFoundNothing), 0);
  WXT_CHECK_EQ(Used(kBox, Outcome::kBoxFoundNothing), 0);
}

WXT_TEST(GriffTapWithBoxOnTheUiPutsPlusOutAndLeavesBoxLit)
{
  // As without Box: a tap on FAF's UI, a command mode's or a rest's on a panel is the next action for Plus.
  WXT_CHECK_EQ(Used(kBox | kPlus, Outcome::kOther), kPlus);
  WXT_CHECK_EQ(Used(kBox, Outcome::kOther), 0);
}

WXT_TEST(GriffTapWithoutBoxUsesAllThatCounted)
{
  // The activity's own rule for a tap without Box, which the program's agrees with.
  WXT_CHECK_EQ(Used(kPlus, Outcome::kOther), kPlus);
  WXT_CHECK_EQ(Used(0, Outcome::kOther), 0);
}

WXT_TEST(GriffEscapePutsAwayTheDraftThenTheModeThenTheSelection)
{
  // FAF's HandleEsc in its order, the draft first: one tap of the cross puts away one of them.
  using grifftap::Escape;
  using grifftap::EscapeFor;
  WXT_CHECK_EQ(EscapeFor(true, true, true), Escape::kDropDraft);
  WXT_CHECK_EQ(EscapeFor(true, false, false), Escape::kDropDraft);
  WXT_CHECK_EQ(EscapeFor(false, true, true), Escape::kEndCommandMode);
  WXT_CHECK_EQ(EscapeFor(false, true, false), Escape::kEndCommandMode);
  WXT_CHECK_EQ(EscapeFor(false, false, true), Escape::kDeselect);
  // With none of them the cross does not show.
  WXT_CHECK_EQ(EscapeFor(false, false, false), Escape::kNothing);
}

WXT_TEST(GriffTapReachIsReadBackAsTheSameFloat)
{
  // FAF's reach for a unit beside a tap, 3.5 mm and 2 mm at 5 to 30 of the picture's pixels a mm, as the activity works them
  // out (a double's product, made a float): the console reads each back to the float it was, so that the program knows its
  // own value again and puts the player's back as it was. %g's six digits miss 37.0588226 (3.5 mm at 10.588235), for one,
  // and 96 % of these.
  WXT_CHECK_EQ(Read(FloatText(37.0588226f)), 37.0588226f);
  int missed = 0;
  int tried = 0;
  for (int step = 0; step <= 10007; ++step) {
    // Densities as screens give them, no tidy decimals: 25 / 10007 a step.
    const double pixels = 5 + step * 25.0 / 10007;
    for (const double mm : {3.5, 2.0}) {
      const float reach = static_cast<float>(mm * pixels);
      ++tried;
      if (Read(FloatText(reach)) != reach) {
        ++missed;
      }
    }
  }
  WXT_CHECK_EQ(tried, 20016);
  WXT_CHECK_EQ(missed, 0);
  // And what FAF's options give a player, whole and fractional.
  for (const float player : {0.0f, 4.0f, 7.0f, 10.5f, 0.1f, 123.456f}) {
    WXT_CHECK_EQ(Read(FloatText(player)), player);
  }
}
