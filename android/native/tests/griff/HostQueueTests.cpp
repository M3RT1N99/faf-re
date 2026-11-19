// The order of what the activity gives the game's thread on SDL's activity (include/HostQueue.h): a task runs where its
// wake comes in SDL's queue, the input it gives comes next, ahead of what was given after the task, a give-up never lands
// ahead of its press, coalesced and idle tasks keep their places; and Windows' double click, which the host's input follows.

#include <cstdint>
#include <deque>
#include <string>

#include <HostQueue.h>

#include "Check.h"

namespace
{
  /// The game's events as the host meets them: SDL's queue, with a wake for each task and the system's own events between
  /// them, and the engine's queue the host's input goes to (QueueEvent), which NextEvent takes before SDL's next event, as
  /// patches/src/platform/sdl/WindowSdl.cpp.patch has it.
  struct Game
  {
    hostqueue::Queue queue;
    std::deque<std::string> sdl;
    std::deque<std::string> engine;
    std::string seen; // what the engine got, in its order

    bool Wake()
    {
      sdl.push_back("wake");
      return true;
    }
    void Give(const hostqueue::Task task, const std::uintptr_t argument)
    {
      queue.Push(task, argument, [this] { return Wake(); });
    }
    void GiveNewest(const hostqueue::Task task, const std::uintptr_t argument)
    {
      queue.PushNewest(task, argument, [this] { return Wake(); });
    }
    void GiveIdle(const hostqueue::Task task, const std::uintptr_t argument)
    {
      queue.PushIdle(task, argument, [this] { return Wake(); });
    }
    /// The system's own event, such as a key of a keyboard.
    void Other(const std::string& name)
    {
      sdl.push_back(name);
    }
    void Got(const std::string& name)
    {
      seen += seen.empty() ? name : " " + name;
    }
    /// The engine's events until nothing waits: its queue first, then SDL's next event; a wake runs its task.
    void Run()
    {
      for (;;) {
        if (!engine.empty()) {
          Got(engine.front());
          engine.pop_front();
          continue;
        }
        if (sdl.empty()) {
          return;
        }
        const std::string next = sdl.front();
        sdl.pop_front();
        if (next != "wake") {
          Got(next);
          continue;
        }
        hostqueue::Entry entry;
        if (queue.Take(entry, [this] { return Wake(); })) {
          entry.task(entry.argument);
        }
      }
    }
  };

  Game* gGame = nullptr; // the tasks are plain functions, as the host's are

  const char* const kInputs[] = {"press", "move", "release", "a", "b", "c", "idle", "first", "second", "third"};
  enum Input : std::uintptr_t
  {
    kPress,
    kMove,
    kRelease,
    kA,
    kB,
    kC,
    kIdle,
    kFirst,
    kSecond,
    kThird,
  };

  /// A task that gives one input.
  void Gives(const std::uintptr_t input)
  {
    gGame->engine.push_back(kInputs[input]);
  }

  /// A drag's start: its press and a move, as Griff's DragTask gives them.
  void Drag(std::uintptr_t)
  {
    gGame->engine.push_back("press");
    gGame->engine.push_back("move");
  }

  void GiveUp(std::uintptr_t)
  {
    gGame->engine.push_back("release");
  }

  /// A task that gives an input and then a task of its own, which goes after what was given before it.
  void GivesAThenB(std::uintptr_t)
  {
    gGame->engine.push_back("a");
    gGame->Give(&Gives, kB);
  }
} // namespace

WXT_TEST(HostQueueATasksInputComesAheadOfWhatWasGivenAfterIt)
{
  Game game;
  gGame = &game;
  game.Give(&Gives, kPress);
  game.Other("key");
  game.Give(&Gives, kRelease);
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("press key release"));
}

WXT_TEST(HostQueueAGiveUpNeverLandsAheadOfItsPress)
{
  // Java gives the give-up before the drag's task has run: the press and its move still come first.
  Game game;
  gGame = &game;
  game.Give(&Drag, 0);
  game.Give(&GiveUp, 0);
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("press move release"));
}

WXT_TEST(HostQueueATaskGivenByATaskGoesAfterWhatWasGivenBeforeIt)
{
  Game game;
  gGame = &game;
  game.Give(&GivesAThenB, 0);
  game.Give(&Gives, kC);
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("a c b"));
}

WXT_TEST(HostQueueANewestTaskTakesThePlaceOfTheLastWhileItWaits)
{
  Game game;
  gGame = &game;
  game.GiveNewest(&Gives, kFirst);
  game.GiveNewest(&Gives, kSecond);
  WXT_CHECK_EQ(game.queue.Waiting(), std::size_t{1});
  game.Give(&Gives, kA);
  game.GiveNewest(&Gives, kThird); // the last thing given is another task's: a place of its own
  WXT_CHECK_EQ(game.queue.Waiting(), std::size_t{3});
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("second a third"));
}

WXT_TEST(HostQueueANewestTaskThatRanLeavesNoPlaceToTake)
{
  Game game;
  gGame = &game;
  game.GiveNewest(&Gives, kFirst);
  game.Run();
  game.GiveNewest(&Gives, kSecond);
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("first second"));
}

WXT_TEST(HostQueueAPlainTaskIsNeverCoalesced)
{
  Game game;
  gGame = &game;
  game.Give(&Gives, kFirst);
  game.GiveNewest(&Gives, kSecond);
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("first second"));
}

WXT_TEST(HostQueueTheIdleTaskRunsOnceNothingElseWaits)
{
  // Given before two tasks, it goes behind them when its turn comes; given twice, it runs once, with the later number.
  Game game;
  gGame = &game;
  game.GiveIdle(&Gives, kA);
  game.Give(&Gives, kB);
  game.GiveIdle(&Gives, kIdle);
  game.Give(&Gives, kC);
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("b c idle"));
  WXT_CHECK_EQ(game.queue.Waiting(), std::size_t{0});
}

WXT_TEST(HostQueueTheIdleTaskAloneRunsAtItsWake)
{
  Game game;
  gGame = &game;
  game.GiveIdle(&Gives, kIdle);
  game.Other("key");
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("idle key"));
}

WXT_TEST(HostQueueAWakeWithNothingToTakeRunsNothing)
{
  Game game;
  gGame = &game;
  game.Wake();
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string());
}

WXT_TEST(HostQueueATaskWithoutItsWakeIsNotKept)
{
  // Before SDL's events are there: nothing waits for a wake that never comes, and the next task takes its own.
  Game game;
  gGame = &game;
  WXT_CHECK(!game.queue.Push(&Gives, kA, [] { return false; }));
  WXT_CHECK(!game.queue.PushIdle(&Gives, kIdle, [] { return false; }));
  WXT_CHECK_EQ(game.queue.Waiting(), std::size_t{0});
  game.Give(&Gives, kB);
  game.Run();
  WXT_CHECK_EQ(game.seen, std::string("b"));
}

WXT_TEST(HostQueueDoubleClickWithinTheTimeAndTwoPixels)
{
  hostqueue::DoubleClicks clicks;
  WXT_CHECK_EQ(clicks.Press(1, 100, 100, 1000), 1);
  WXT_CHECK_EQ(clicks.Wait(1, 102, 98, 1100), std::uint32_t{401});
  WXT_CHECK_EQ(clicks.Press(1, 102, 98, 1500), 2);
  // The press after a double click is one of its own, and the one after it may double it.
  WXT_CHECK_EQ(clicks.Press(1, 102, 98, 1600), 1);
  WXT_CHECK_EQ(clicks.Press(1, 102, 98, 1700), 2);
}

WXT_TEST(HostQueueNoDoubleClickPastTheTimeTheDistanceOrTheButton)
{
  hostqueue::DoubleClicks clicks;
  WXT_CHECK_EQ(clicks.Press(1, 100, 100, 1000), 1);
  WXT_CHECK_EQ(clicks.Wait(1, 100, 100, 1501), std::uint32_t{0});
  WXT_CHECK_EQ(clicks.Wait(1, 103, 100, 1100), std::uint32_t{0});
  WXT_CHECK_EQ(clicks.Wait(1, 100, 97, 1100), std::uint32_t{0});
  WXT_CHECK_EQ(clicks.Wait(3, 100, 100, 1100), std::uint32_t{0});
  WXT_CHECK_EQ(clicks.Press(3, 100, 100, 1100), 1);
  // The right button's press is now the last: a left one there is a press of its own.
  WXT_CHECK_EQ(clicks.Press(1, 100, 100, 1200), 1);
}
