#pragma once
// What the activity gives the game's thread on SDL's activity (src/host/HostSdl.cpp), as the earlier host's message layer
// gave it: tasks in the order they were given, each run where its wake comes in SDL's queue, so that what was given before
// a task has been taken when it runs, and the input it gives comes next (SDL's NextEvent takes what the host queued before
// its next event, patches/src/platform/sdl/WindowSdl.cpp.patch). Also Windows' rule for a double click, which the input the
// host gives follows. Headers only, so that tests/griff pins the rules without SDL or the engine.

#include <cstdint>
#include <cstdlib>
#include <deque>
#include <mutex>

namespace hostqueue
{
  /// A task for the game's thread, and the number it takes (two places of the picture, TaskPlaces.h).
  using Task = void (*)(std::uintptr_t argument);

  struct Entry
  {
    Task task = nullptr;
    std::uintptr_t argument = 0;
    bool newest = false; // given by PushNewest: a later one of the same task may take its place
    bool idle = false;   // the idle task's place: it runs what PushIdle last gave
  };

  /// The tasks that wait, one wake each in SDL's queue: a wake takes the oldest, so the tasks keep the order they were
  /// given in among themselves and with what SDL queued between them. Any thread may give; the game's thread takes. A
  /// `wake` puts a wake in SDL's queue, under the queue's lock, so that tasks two threads give at once have their wakes in
  /// their own order; it says whether it could (SDL's events may not be there yet), and a task without its wake is not
  /// kept, as the earlier host ran nothing without a window.
  class Queue
  {
  public:
    /// Puts `task` after everything given before it.
    template <class Wake>
    bool Push(const Task task, const std::uintptr_t argument, Wake&& wake)
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      mEntries.push_back(Entry{task, argument, false, false});
      return Woken(wake);
    }

    /// The same, in place of the last thing given where that is this task's from PushNewest and still waits, as Windows
    /// coalesces mouse moves: then it takes that place and needs no wake of its own.
    template <class Wake>
    bool PushNewest(const Task task, const std::uintptr_t argument, Wake&& wake)
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      if (!mEntries.empty() && mEntries.back().newest && mEntries.back().task == task) {
        mEntries.back().argument = argument;
        return true;
      }
      mEntries.push_back(Entry{task, argument, true, false});
      return Woken(wake);
    }

    /// `task` runs once nothing else waits: what was given before it and what came after it, and where it is given outside
    /// the game's events (a frame's Lua, before the frame's paint) that frame's paint too. It takes the place of an idle
    /// task that still waits.
    template <class Wake>
    bool PushIdle(const Task task, const std::uintptr_t argument, Wake&& wake)
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      mIdleTask = task;
      mIdleArgument = argument;
      if (mIdleWaits) {
        return true;
      }
      mEntries.push_back(Entry{nullptr, 0, false, true});
      mIdleWaits = Woken(wake);
      return mIdleWaits;
    }

    /// For one wake: the task to run, if any. Where the idle task's turn comes while others wait behind it, it goes behind
    /// them again with a new wake.
    template <class Wake>
    [[nodiscard]] bool Take(Entry& taken, Wake&& wake)
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      if (mEntries.empty()) {
        return false;
      }
      const Entry entry = mEntries.front();
      mEntries.pop_front();
      if (!entry.idle) {
        taken = entry;
        return true;
      }
      if (!mEntries.empty()) {
        mEntries.push_back(entry);
        mIdleWaits = Woken(wake);
        return false;
      }
      mIdleWaits = false;
      taken = Entry{mIdleTask, mIdleArgument, false, false};
      return mIdleTask != nullptr;
    }

    /// How many tasks wait, the idle task's place among them.
    [[nodiscard]] std::size_t Waiting() const
    {
      const std::lock_guard<std::mutex> lock(mMutex);
      return mEntries.size();
    }

  private:
    /// The wake of the entry just put last, under the lock; without it the entry goes again.
    template <class Wake>
    bool Woken(Wake& wake)
    {
      if (wake()) {
        return true;
      }
      mEntries.pop_back();
      return false;
    }

    mutable std::mutex mMutex;
    std::deque<Entry> mEntries;
    Task mIdleTask = nullptr;
    std::uintptr_t mIdleArgument = 0;
    bool mIdleWaits = false;
  };

  /// Windows' double click for a window whose class has CS_DBLCLKS, as the earlier host's message layer had it: a press of
  /// the same button within kTime of the last and within half of kDistance of it each way is the second of a double
  /// click, and the press after that is one of its own. Times in milliseconds of a clock that only goes forward.
  class DoubleClicks
  {
  public:
    static constexpr std::uint32_t kTime = 500; // GetDoubleClickTime
    static constexpr int kDistance = 4;         // SM_CXDOUBLECLK, SM_CYDOUBLECLK

    /// How long a press of `button` at x, y at `now` would still be a double click with the last press; 0 where it would be
    /// a press of its own. Who wants two presses there, as a player who wants two single clicks, waits that long.
    [[nodiscard]] std::uint32_t Wait(const int button, const int x, const int y, const std::uint32_t now) const
    {
      if (mButton == 0 || button != mButton || now - mTime > kTime || std::abs(x - mX) > kDistance / 2
          || std::abs(y - mY) > kDistance / 2) {
        return 0;
      }
      return kTime + 1 - (now - mTime);
    }

    /// A press of `button` at x, y at `now`: 2 where it is the second of a double click, else 1.
    [[nodiscard]] int Press(const int button, const int x, const int y, const std::uint32_t now)
    {
      if (Wait(button, x, y, now) != 0) {
        mButton = 0;
        return 2;
      }
      mButton = button;
      mX = x;
      mY = y;
      mTime = now;
      return 1;
    }

  private:
    int mButton = 0; // 0: no press a next one could double
    int mX = 0;
    int mY = 0;
    std::uint32_t mTime = 0;
  };
} // namespace hostqueue
