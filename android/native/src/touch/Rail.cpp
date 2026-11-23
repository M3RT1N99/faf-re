#include "touch/Rail.h"

#include <bit>

touch::Rail::Button& touch::Rail::ButtonOf(const int bit)
{
  return mButtons[static_cast<std::size_t>(std::countr_zero(static_cast<unsigned>(bit)))];
}

const touch::Rail::Button& touch::Rail::ButtonOf(const int bit) const
{
  return mButtons[static_cast<std::size_t>(std::countr_zero(static_cast<unsigned>(bit)))];
}

void touch::Rail::Press(const int bit, const Millis time)
{
  Button& button = ButtonOf(bit);
  button.held = true;
  button.pressedAt = time;
  button.litBefore = button.lit;
  button.lit = false;
  button.used = false;
}

void touch::Rail::Release(const int bit, const Millis time, const bool stayed)
{
  Button& button = ButtonOf(bit);
  if (!button.held) {
    return;
  }
  button.held = false;
  if (stayed && !button.used && time - button.pressedAt < kTapMilliseconds) {
    // A tap: it held nothing, so no moment after it counts.
    button.lit = !button.litBefore;
    button.upAt = kLongAgo;
  } else {
    button.lit = button.litBefore && !button.used;
    button.upAt = time;
  }
}

void touch::Rail::Cancel(const int bit)
{
  Button& button = ButtonOf(bit);
  if (!button.held) {
    return;
  }
  button.held = false;
  button.lit = button.litBefore && !button.used;
  button.upAt = kLongAgo;
}

int touch::Rail::Counting(const Millis time) const
{
  int bits = 0;
  for (int at = 0; at < kButtons; ++at) {
    const Button& button = mButtons[at];
    if (button.held || button.lit || time - button.upAt < kLiftMilliseconds) {
      bits |= 1 << at;
    }
  }
  return bits;
}

int touch::Rail::Shown() const
{
  int bits = 0;
  for (int at = 0; at < kButtons; ++at) {
    if (mButtons[at].held || mButtons[at].lit) {
      bits |= 1 << at;
    }
  }
  return bits;
}

int touch::Rail::Look(const int bit) const
{
  const Button& button = ButtonOf(bit);
  return button.held ? kHeld : button.lit ? kLit : kIdle;
}

bool touch::Rail::Held(const int bit) const
{
  return ButtonOf(bit).held;
}

void touch::Rail::Clear()
{
  mButtons = {};
}

void touch::Rail::Used(const int bits)
{
  for (int at = 0; at < kButtons; ++at) {
    if ((bits & 1 << at) == 0) {
      continue;
    }
    Button& button = mButtons[at];
    if (button.held) {
      button.used = true;
    }
    button.lit = false;
    button.upAt = kLongAgo;
  }
}
