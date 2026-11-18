// What a tap of the touch scheme Griff uses (include/GriffTap.h): the rail's buttons by what the tap turned out to be, FAF's
// reach for a unit beside it as the console's text, what the cross puts away, where a drag on FAF's UI presses nothing and how
// a rest's ring shows what its drag gives, and when the pointer leaves while Griff's Distribute orders holds the cursor.

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

  /// A control of FAF's UI as the program walks it for a drag's press: its parent, and whether it is a Checkbox.
  struct Control
  {
    const Control* parent;
    bool checkbox;
  };

  bool PressesNothing(const Control* const control)
  {
    return grifftap::DragPressesNothing(
      control, [](const Control* const at) { return at->parent; }, [](const Control* const at) { return at->checkbox; }
    );
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

WXT_TEST(GriffTapADragPressesNothingWhereItsPressComesToACheckbox)
{
  // The orders panel's grid of Checkboxes (orders.lua AddOrder), one a toggle with its ring over it, which takes hit tests,
  // and the construction panel's grid of FAF's buttons beside it.
  const Control root{nullptr, false};
  const Control orders{&root, false};
  const Control stop{&orders, true};
  const Control toggle{&orders, true};
  const Control ring{&toggle, false};
  const Control construction{&root, false};
  const Control queued{&construction, false};
  WXT_CHECK(PressesNothing(&stop));
  // The press on the ring goes on to the toggle, which acts on it.
  WXT_CHECK(PressesNothing(&ring));
  // A factory queue's item keeps its press, which its drag to reorder starts with; so does the panel between the buttons.
  WXT_CHECK(!PressesNothing(&queued));
  WXT_CHECK(!PressesNothing(&orders));
  WXT_CHECK(!PressesNothing(&root));
  // Where no control takes the press, a mouse's drag presses as ever.
  WXT_CHECK(!PressesNothing(nullptr));
}

WXT_TEST(GriffTapARestsRingShowsWhatItsDragGives)
{
  using grifftap::RestLook;
  using grifftap::RestLookOf;
  // On the ground for FAF's formation; elsewhere the left button's drag: a box, an armed mode's drag, a panel's.
  WXT_CHECK_EQ(RestLookOf(false, false, true), RestLook::kFormation);
  WXT_CHECK_EQ(RestLookOf(false, false, false), RestLook::kLeftDrag);
  // Nothing on a marked waypoint, and on a Checkbox of FAF's, whose press a drag leaves alone: its lift is the right click.
  WXT_CHECK_EQ(RestLookOf(false, true, false), RestLook::kNothing);
  WXT_CHECK_EQ(RestLookOf(true, false, false), RestLook::kNothing);
  // griffmap.lua's rings by number: the move ping's, the alert ping's, the attack ping's.
  WXT_CHECK_EQ(static_cast<int>(RestLook::kFormation), 1);
  WXT_CHECK_EQ(static_cast<int>(RestLook::kLeftDrag), 2);
  WXT_CHECK_EQ(static_cast<int>(RestLook::kNothing), 3);
}

WXT_TEST(GriffTapThePointersLeaveWaitsWhileDistributeOrdersHoldsTheCursor)
{
  // The activity asks for the leave 300 ms after the last finger lifted, with the count of fingers that had come down then.
  grifftap::Leave leave;
  WXT_CHECK(leave.Asked(3, 3, false, false));
  // A finger came since: the pointer is its own. Griff's clicks run: their end has it leave anew. Neither waits.
  WXT_CHECK(!leave.Asked(3, 4, false, false));
  WXT_CHECK(!leave.Asked(3, 3, true, false));
  WXT_CHECK(!leave.Asked(3, 3, true, true));
  WXT_CHECK(!leave.Freed(4, false));
  // The handle's lift that distributes: the frame came after the leave, which waits until Griff frees the cursor.
  WXT_CHECK(!leave.Asked(5, 5, false, true));
  WXT_CHECK(leave.Freed(5, false));
  // Once: the next free has nothing that waits.
  WXT_CHECK(!leave.Freed(5, false));
  // A finger that came while the leave waited keeps the pointer, as do Griff's clicks that run when it is freed.
  WXT_CHECK(!leave.Asked(6, 6, false, true));
  WXT_CHECK(!leave.Freed(7, false));
  WXT_CHECK(!leave.Asked(8, 8, false, true));
  WXT_CHECK(!leave.Freed(8, true));
  WXT_CHECK(!leave.Freed(8, false));
  // Of two leaves asked while the cursor is held, the later counts: the fingers that came between have lifted again.
  WXT_CHECK(!leave.Asked(9, 9, false, true));
  WXT_CHECK(!leave.Asked(10, 10, false, true));
  WXT_CHECK(leave.Freed(10, false));
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
