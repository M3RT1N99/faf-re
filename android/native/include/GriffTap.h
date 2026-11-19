#pragma once

// What a tap of the touch scheme Griff (android/TOUCH_UI.md, "Griff") uses: of the rail's buttons those its outcome used (the
// rule of the app's Rail.java), and FAF's reach for a unit beside it as the console's text; what the tap on Griff's Esc, the
// cross, does; where a finger's drag on FAF's UI presses nothing, and how a rest's ring shows what its drag gives; and when the
// pointer leaves after a lift that Griff's Distribute orders holds the cursor for. The program decides a tap
// (src/host/GriffAndroid.cpp); here the rules alone, for their tests (tests/wx/GriffTapTests.cpp).

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

namespace grifftap
{
  // TouchInput's RAIL_ bits: Griff v4's rail has Box and Plus (FAF's orders panel has the attack-move).
  constexpr int kRailBox = 1;
  constexpr int kRailQueue = 2;

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

  /// What Griff's Esc, the cross, puts away: what a PC player's Esc does in a game (FAF's eschandler.lua HandleEsc: the armed
  /// command mode, else the selection), with Griff's draft first, which stands for FAF's build mode; nothing where none is,
  /// and the cross does not show then.
  enum class Escape
  {
    kNothing,
    kDropDraft,
    kEndCommandMode,
    kDeselect,
  };

  [[nodiscard]] constexpr Escape EscapeFor(const bool drafting, const bool commandModeArmed, const bool selected)
  {
    if (drafting) {
      return Escape::kDropDraft;
    }
    if (commandModeArmed) {
      return Escape::kEndCommandMode;
    }
    return selected ? Escape::kDeselect : Escape::kNothing;
  }

  /// Whether a finger's drag that starts on FAF's UI presses nothing where it came down (DragTask presses the left button
  /// there, as a mouse's drag): where the press comes to a Checkbox of FAF's (maui/checkbox.lua), which acts on the press, as
  /// the orders panel's buttons and toggles, the construction panel's tabs and the panels' collapse arrows do. A thumb that
  /// slid from the orders panel onto the map fired the button it started on, Stop or a toggle, which no mouse does by
  /// sliding, and a rest that reads a tooltip had no way off it that did nothing. A press goes on from the control under the
  /// finger to its parents (CMauiControl::PostEvent), so a mark over a button that takes hit tests of its own (an order
  /// toggle's ring, its question mark for a mixed selection) counts as the button. What acts at the release or by its drag
  /// keeps its press: FAF's buttons (the build icons, the factory queue's reorder), the minimap, windows, scroll bars,
  /// sliders. `parent` gives a control's parent, none above the root; `checkbox` whether a control is a Checkbox.
  template <class Control, class Parent, class IsCheckbox>
  [[nodiscard]] bool DragPressesNothing(Control* control, Parent&& parent, IsCheckbox&& checkbox)
  {
    for (; control != nullptr; control = parent(control)) {
      if (checkbox(control)) {
        return true;
      }
    }
    return false;
  }

  /// How a resting finger's ring looks (Game.REST_; griffmap.lua's rings go by these numbers), by what a drag from the rest gives
  /// (the program's DragTask): FAF's formation, the right button's drag; the left button's drag from there, as a mouse's,
  /// which is FAF's box on the map, the armed mode's own drag where a command mode is armed (its formation, an area command),
  /// the minimap's camera and a panel's drag; or nothing: on a waypoint marked for deletion, which the lift deletes, and on a
  /// Checkbox of FAF's (DragPressesNothing), where the lift is the panel's right click all the same.
  enum class RestLook
  {
    kFormation = 1,
    kLeftDrag = 2,
    kNothing = 3,
  };

  /// The look of a rest on a Checkbox of FAF's (`checkbox`), or of one that found a waypoint to mark (`waypoint`) or the ground
  /// for FAF's formation (`formation`).
  [[nodiscard]] constexpr RestLook RestLookOf(const bool checkbox, const bool waypoint, const bool formation)
  {
    if (checkbox || waypoint) {
      return RestLook::kNothing;
    }
    return formation ? RestLook::kFormation : RestLook::kLeftDrag;
  }

  /// The pointer's leave once the last finger has lifted (src/host/GameHost.cpp PointerLeave), by how many fingers had come down
  /// when the activity asked for it: none where a finger came since, nor while Griff's own clicks run, whose end has the
  /// pointer leave anew. While Griff holds the cursor where it put it, which its Distribute orders does until a frame was drawn
  /// with the cursor at the group's middle (FAF reads the world place under it), the leave waits and comes once Griff frees the
  /// cursor: the handle's lift asks for it, and where the frame came after the leave, the leave took the cursor off the group
  /// and the lift gave nothing but Cancelled.
  class Leave
  {
  public:
    /// A leave asked when `fingersThen` fingers had come down, now `fingersNow`: whether the pointer leaves now.
    [[nodiscard]] bool Asked(const std::uintptr_t fingersThen, const std::uintptr_t fingersNow, const bool clicking, const bool held)
    {
      if (fingersNow != fingersThen || clicking) {
        return false;
      }
      if (held) {
        mWaits = true;
        mFingersThen = fingersThen;
        return false;
      }
      return true;
    }

    /// Griff frees the cursor: whether the leave that waited for it comes now, by the same rules.
    [[nodiscard]] bool Freed(const std::uintptr_t fingersNow, const bool clicking)
    {
      if (!mWaits) {
        return false;
      }
      mWaits = false;
      return Asked(mFingersThen, fingersNow, clicking, false);
    }

  private:
    bool mWaits = false;
    std::uintptr_t mFingersThen = 0;
  };

  /// A float as the console's text, which the console reads back as the same float (std::atof, then a float: its
  /// ParseFloatToken): as many digits as a float needs for that, where %g's six round most reaches to another float.
  [[nodiscard]] inline std::string FloatText(const float value)
  {
    char text[32];
    std::snprintf(text, sizeof text, "%.*g", std::numeric_limits<float>::max_digits10, static_cast<double>(value));
    return text;
  }
} // namespace grifftap
