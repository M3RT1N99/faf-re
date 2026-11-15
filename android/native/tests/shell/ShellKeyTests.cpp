// faf_shell_tests (android/native/README.md, Tests): the patched shell's rule for a key-down a handler took
// (patches/src/platform/shell/Shell.cpp.patch, patches/README.md): it gets no char, neither the char the shell makes
// for a key SDL types no text for (Enter) nor the window system's text for it, which comes before the key's key-up; a
// key-down the handlers skip gets both; a key-up ends the rule, so that later text, which no key of its own brought,
// gets its char. ShellApp::Dispatch takes the events from a window layer of the test's own (platform/Window.h)
// and hands them to a frame whose first handler is the test's. Linked statically, so it runs on the emulator and on
// Linux hosts. Exit code 0 when every check passes.

#include <Windows.h>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <vector>

#include "platform/Window.h"
#include "platform/shell/app.h"
#include "platform/shell/event.h"
#include "platform/shell/frame.h"

namespace
{
  int gFailedChecks = 0;

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      std::printf("  %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                    \
      ++gFailedChecks;                                                                                                 \
    }                                                                                                                  \
  } while (false)

  // The one native window the test's window layer makes, and the events it hands out.
  int gNativeWindow = 0;
  std::deque<platform::window::Event> gEvents;

  /// What the frame's handlers saw of the keyboard, in order: "down", "up", and "char <code>".
  struct Keys final : ShellEvtHandler
  {
    std::vector<std::string> seen;
    // A handler takes each key-down, as the console's CUIKeyHandler::OnKeyDown takes the grave accent's.
    bool takeKeyDowns = false;

    bool ProcessEvent(
      ShellEvent& event
    ) override
    {
      switch (event.GetEventType()) {
      case SHELL_EVT_KEY_DOWN:
        seen.push_back("down");
        return takeKeyDowns;
      case SHELL_EVT_KEY_UP:
        seen.push_back("up");
        return false;
      case SHELL_EVT_CHAR:
        seen.push_back("char " + std::to_string(static_cast<ShellKeyEvent&>(event).GetKeyCode()));
        return false;
      default:
        return false;
      }
    }
  };

  platform::window::Event Key(
    const platform::window::EventType type,
    const int virtualKey
  )
  {
    platform::window::Event event;
    event.type = type;
    event.window = &gNativeWindow;
    event.virtualKey = virtualKey;
    return event;
  }

  platform::window::Event Text(
    const std::uint32_t codepoint
  )
  {
    platform::window::Event event;
    event.type = platform::window::EventType::Text;
    event.window = &gNativeWindow;
    event.codepoint = codepoint;
    return event;
  }

  struct TestApp final : ShellApp
  {};

  /// What the frame's handlers see of `events`, which the application dispatches as its loop does.
  std::vector<std::string> Seen(
    Keys& keys,
    const std::vector<platform::window::Event>& events
  )
  {
    keys.seen.clear();
    gEvents.assign(events.begin(), events.end());
    while (gShellApp->Pending()) {
      gShellApp->Dispatch();
    }
    return keys.seen;
  }

  using Order = std::vector<std::string>;
  using platform::window::EventType;

  /// Whether `seen` is `expected`; else what was seen.
  bool Is(
    const Order& seen,
    const Order& expected
  )
  {
    if (seen == expected) {
      return true;
    }
    std::printf("  seen:");
    for (const std::string& key : seen) {
      std::printf(" [%s]", key.c_str());
    }
    std::printf("\n");
    return false;
  }

  void ATakenKeyDownHasNoText(
    Keys& keys
  )
  {
    // ` (VK_OEM_3), as a keyboard types it: its text between its key-down and its key-up.
    keys.takeKeyDowns = true;
    CHECK(Is(
      Seen(keys, {Key(EventType::KeyDown, VK_OEM_3), Text('`'), Key(EventType::KeyUp, VK_OEM_3)}), Order{"down", "up"}
    ));
  }

  void ASkippedKeyDownHasItsText(
    Keys& keys
  )
  {
    keys.takeKeyDowns = false;
    CHECK(Is(
      Seen(keys, {Key(EventType::KeyDown, VK_OEM_3), Text('`'), Key(EventType::KeyUp, VK_OEM_3)}),
      Order{"down", "char 96", "up"}
    ));
  }

  void ATakenEnterHasNoCharOfTheWindowLibrary(
    Keys& keys
  )
  {
    // SDL types no text for Enter: the shell gives its char with the key-down (DeliverCharForKey), not for a taken one.
    keys.takeKeyDowns = true;
    CHECK(Is(Seen(keys, {Key(EventType::KeyDown, VK_RETURN), Key(EventType::KeyUp, VK_RETURN)}), Order{"down", "up"}));
    keys.takeKeyDowns = false;
    CHECK(Is(
      Seen(keys, {Key(EventType::KeyDown, VK_RETURN), Key(EventType::KeyUp, VK_RETURN)}), Order{"down", "char 13", "up"}
    ));
  }

  void TextAfterTheKeyUpIsNotTheKeyDowns(
    Keys& keys
  )
  {
    // A test's text with no key of its own, and an input method's committed text, after a taken key.
    keys.takeKeyDowns = true;
    CHECK(Is(
      Seen(keys, {Key(EventType::KeyDown, VK_OEM_3), Key(EventType::KeyUp, VK_OEM_3), Text('x')}),
      Order{"down", "up", "char 120"}
    ));
  }

  void OnlyTheFirstTextAfterATakenKeyDownGoes(
    Keys& keys
  )
  {
    keys.takeKeyDowns = true;
    CHECK(Is(
      Seen(keys, {Key(EventType::KeyDown, VK_OEM_3), Text('`'), Text('y'), Key(EventType::KeyUp, VK_OEM_3)}),
      Order{"down", "char 121", "up"}
    ));
  }

  struct Test
  {
    const char* name;
    void (*run)(Keys&);
  };

  const Test kTests[] = {
    {"ATakenKeyDownHasNoText", &ATakenKeyDownHasNoText},
    {"ASkippedKeyDownHasItsText", &ASkippedKeyDownHasItsText},
    {"ATakenEnterHasNoCharOfTheWindowLibrary", &ATakenEnterHasNoCharOfTheWindowLibrary},
    {"TextAfterTheKeyUpIsNotTheKeyDowns", &TextAfterTheKeyUpIsNotTheKeyDowns},
    {"OnlyTheFirstTextAfterATakenKeyDownGoes", &OnlyTheFirstTextAfterATakenKeyDownGoes},
  };
} // namespace

// The window layer of the test's own: one window, which has the keyboard, and the queued events.
namespace platform::window
{
  bool Init()
  {
    return true;
  }
  void Shutdown() {}
  bool IsInitialized()
  {
    return true;
  }
  Handle CreateTopLevel(
    const char*,
    int,
    int,
    int,
    int,
    bool,
    bool
  )
  {
    return &gNativeWindow;
  }
  void SetFrameStyle(
    Handle,
    bool,
    bool
  )
  {}
  Handle CreateChild(
    Handle,
    int,
    int,
    int,
    int
  )
  {
    return nullptr;
  }
  void Destroy(
    Handle
  )
  {}
  void Show(
    Handle,
    bool
  )
  {}
  void SetTitle(Handle, const char*)
  {}
  void SetBounds(
    Handle,
    int,
    int,
    int,
    int
  )
  {}
  void Maximize(
    Handle
  )
  {}
  void Minimize(
    Handle
  )
  {}
  void Restore(
    Handle
  )
  {}
  void SetFullscreen(
    Handle,
    bool
  )
  {}
  void Raise(
    Handle
  )
  {}
  void GetBounds(
    Handle,
    int* const x,
    int* const y,
    int* const width,
    int* const height
  )
  {
    for (int* const value : {x, y}) {
      if (value != nullptr) {
        *value = 0;
      }
    }
    if (width != nullptr) {
      *width = 1024;
    }
    if (height != nullptr) {
      *height = 768;
    }
  }
  void SetKeyboardFocus(
    Handle
  )
  {}
  void GetFramePosition(
    Handle,
    int* const x,
    int* const y
  )
  {
    *x = 0;
    *y = 0;
  }
  void SetFramePosition(
    Handle,
    int,
    int
  )
  {}
  bool HasKeyboardFocus()
  {
    return true;
  }
  void* NativeHandle(
    Handle window
  )
  {
    return window;
  }
  Handle FromNativeHandle(
    void* native
  )
  {
    return native;
  }
  void AddSystemMenuItem(Handle, int, const char*)
  {}
  bool HasEvents()
  {
    return !gEvents.empty();
  }
  bool PollEvent(
    Event& event
  )
  {
    if (gEvents.empty()) {
      return false;
    }
    event = gEvents.front();
    gEvents.pop_front();
    return true;
  }
} // namespace platform::window

int main()
{
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  TestApp app;
  ShellTopLevelWindow frame(nullptr, -1, ShellString(L"keys"));
  Keys keys;
  frame.PushEventHandler(&keys);

  int failedTests = 0;
  for (const Test& test : kTests) {
    gFailedChecks = 0;
    std::printf("%s\n", test.name);
    test.run(keys);
    if (gFailedChecks != 0) {
      ++failedTests;
      std::printf("  FAIL\n");
    }
  }
  std::printf("%zu tests, %d failed\n", std::size(kTests), failedTests);
  std::fflush(stdout);
  std::_Exit(failedTests == 0 ? 0 : 1);
}
