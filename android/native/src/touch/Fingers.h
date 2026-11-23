#pragma once

// What a screen's fingers do, whoever makes something of them: TouchInput in a game, PageInput in the menus. Whoever takes
// them from the window system calls these in the order the fingers did it, on one thread.

#include "touch/Numbers.h"

namespace touch
{
  class Fingers
  {
  public:
    virtual ~Fingers() = default;

    /// A finger came down; `first`: no other is left on the glass, whatever this class heard of them.
    virtual void Down(int id, float x, float y, Millis time, bool first) = 0;
    /// Where a finger is now. Moved follows when all of an event's fingers are in.
    virtual void Move(int id, float x, float y) = 0;
    /// The event's fingers are in; `time`: the event's, as Down's and Up's.
    virtual void Moved(Millis time) = 0;
    /// A finger lifted; `last`: no other is left on the glass.
    virtual void Up(int id, Millis time, bool last) = 0;
    /// The system says that a finger which lifted was a palm, with another still down.
    virtual void Forget(int id, Millis time) = 0;
    /// The system took the fingers.
    virtual void Cancel() = 0;
  };
} // namespace touch
