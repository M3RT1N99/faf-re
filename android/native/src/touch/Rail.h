#pragma once

// The touch scheme Griff's rail (buildstage/touch-concept-v2.md; Griff v4): Box and Plus, as TouchInput's kRail bits. A tap
// lights a button for the next action, which puts it out, as does a second tap; held, it counts until it goes up, and a moment
// after, as a thumb and a finger often lift together. A press that an action used lights nothing; a press that was no tap (a
// hold, a cancel, a finger that slid off) leaves the light as it was, unless the hold used it.
//
// What a finger's tap or rest uses: the buttons that counted for it, whatever it turned out to be, Box as it counted when the
// finger came down (TouchInput). Box only where it took one's own unit into the selection or out of it: through a tap on FAF's
// UI, an armed command mode's tap or a rest's right click on a panel Box stays lit while Plus goes out, and Box's tap on nothing
// at all is no action and uses none (the mockup's). Without Box the layer puts them out at once; with Box the program says what
// it used (its rule in include/GriffTap.h). Of the other actions a box on the map uses Box and Plus, a formation Plus, the
// draft's box over deposits Box, a route Plus, the draft's tick Plus; a pan, the handle's tap and its Distribute orders use
// none, and in the draft its plans and its taps, rests and slides on FAF's UI none, so that a lit Plus stays for the tick (a
// held one is Shift for them as ever).
//
// FAF's UI draws it (griffhud.lua: FAF's order buttons at the HUD's factor, at the top left, the user's decision of
// 2026-11-13), as each button looks (Look); the layer takes the fingers on it. FAF's orders panel has the attack-move, so the
// rail has no Attack.

#include <array>

#include "touch/Numbers.h"

namespace touch
{
  class Rail
  {
  public:
    /// A press shorter than this, which no action used and which stayed on the button, is a tap.
    static constexpr Millis kTapMilliseconds = 300;
    /// How long a held button still counts after it went up.
    static constexpr Millis kLiftMilliseconds = 150;

    /// How a button looks (Look): idle, held, or lit for the next action.
    static constexpr int kIdle = 0;
    static constexpr int kHeld = 1;
    static constexpr int kLit = 2;

    /// `bit`: one of TouchInput's kRail bits.
    void Press(int bit, Millis time);
    /// `stayed`: the finger went up on the button, without having moved away.
    void Release(int bit, Millis time, bool stayed);
    /// The system took the finger: no tap, and no lift whose moment after counts.
    void Cancel(int bit);
    /// The kRail bits that count for an action now.
    [[nodiscard]] int Counting(Millis time) const;
    /// The kRail bits held or lit, as the handle shows them.
    [[nodiscard]] int Shown() const;
    /// How the button of `bit` looks: held while a finger is on it, also where it was lit before (the finger's press may yet
    /// put it out); lit while it waits for the next action; else idle, also in the moment after a lift, which counts unseen.
    [[nodiscard]] int Look(int bit) const;
    [[nodiscard]] bool Held(int bit) const;
    /// All out, as if none had been touched: the rail is put away.
    void Clear();
    /// An action used these: a held one counts as used, a lit one goes out, and the moment after a lift is spent.
    void Used(int bits);

  private:
    static constexpr int kButtons = 2;

    struct Button
    {
      bool held = false;
      bool lit = false;
      bool litBefore = false;
      bool used = false;
      Millis pressedAt = 0;
      Millis upAt = kLongAgo;
    };

    [[nodiscard]] Button& ButtonOf(int bit);
    [[nodiscard]] const Button& ButtonOf(int bit) const;

    std::array<Button, kButtons> mButtons{};
  };
} // namespace touch
