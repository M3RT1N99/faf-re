// The host on SDL's activity (HostSdl.h): a task the activity gives waits in hostqueue::Queue with one wake for it in SDL's
// queue; the window system's hook (ScreenKeyboardAndroid.cpp sets it) runs the task where its wake comes, on the program's
// thread, before the engine translates SDL's next event. The input a task gives goes to the engine as the window events
// SDL's own would have become (src/platform/sdl/WindowSdl.cpp's Translate), through the engine's queue, which NextEvent
// takes before SDL's next event (patches/src/platform/sdl/WindowSdl.cpp.patch): so it comes next, ahead of what was given
// after the task, as the earlier host's message layer had it.

#include "HostSdl.h"

#include <SDL3/SDL.h>

#include <Windows.h>

#include "../app/GameAndroid.h"
#include "gpg/core/utils/Logging.h"
#include "platform/Window.h"
#include "platform/sdl/WindowSdlPlatform.h"

namespace
{
  using platform::window::Event;
  using platform::window::EventType;

  hostqueue::Queue gQueue;

  // The input the host gives, on the program's thread.
  hostqueue::DoubleClicks gDoubleClicks;
  std::uint32_t gButtonsDown = 0;  // platform::window::ButtonsDown of the buttons the host holds
  std::uint32_t gModifiers = 0;    // platform::window::Modifier of the keys the host holds
  bool gPressFromFinger = false; // the press the engine takes next is a finger's
  int gCursorX = 0;
  int gCursorY = 0;

  /// The type of the host's wakes in SDL's queue.
  [[nodiscard]] Uint32 WakeType()
  {
    static const Uint32 type = SDL_RegisterEvents(1);
    return type;
  }

  /// A wake in SDL's queue for the task just given; false while SDL's events are not there.
  [[nodiscard]] bool Wake()
  {
    const Uint32 type = WakeType();
    if (type == 0) {
      return false;
    }
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    return SDL_PushEvent(&event);
  }

  /// The keys a keyboard holds and the host's, as the engine's events name them.
  [[nodiscard]] std::uint32_t Modifiers()
  {
    const SDL_Keymod keyboard = SDL_GetModState();
    std::uint32_t modifiers = gModifiers;
    if ((keyboard & SDL_KMOD_SHIFT) != 0) {
      modifiers |= platform::window::kModifierShift;
    }
    if ((keyboard & SDL_KMOD_CTRL) != 0) {
      modifiers |= platform::window::kModifierControl;
    }
    if ((keyboard & SDL_KMOD_ALT) != 0) {
      modifiers |= platform::window::kModifierAlt;
    }
    if ((keyboard & SDL_KMOD_GUI) != 0) {
      modifiers |= platform::window::kModifierMeta;
    }
    return modifiers;
  }

  /// To the engine, next.
  void Deliver(Event event)
  {
    event.window = platform::android::GameWindow();
    event.modifiers = Modifiers();
    platform::window::sdl::QueueEvent(event);
  }

  [[nodiscard]] int EngineButton(const int button)
  {
    switch (button) {
    case host::kButtonRight:
      return platform::window::kButtonRight;
    case host::kButtonMiddle:
      return platform::window::kButtonMiddle;
    default:
      return platform::window::kButtonLeft;
    }
  }

  [[nodiscard]] std::uint32_t DownBit(const int button)
  {
    switch (button) {
    case host::kButtonRight:
      return platform::window::kRightDown;
    case host::kButtonMiddle:
      return platform::window::kMiddleDown;
    default:
      return platform::window::kLeftDown;
    }
  }

  void GiveButton(const int button, const bool down, const int x, const int y, const bool finger)
  {
    if (x != gCursorX || y != gCursorY) {
      host::MouseMove(x, y);
    }
    Event event;
    event.type = down ? EventType::MouseDown : EventType::MouseUp;
    event.button = EngineButton(button);
    event.x = x;
    event.y = y;
    if (down) {
      event.clicks = gDoubleClicks.Press(button, x, y, static_cast<std::uint32_t>(SDL_GetTicks()));
      gButtonsDown |= DownBit(button);
      gPressFromFinger = finger;
    } else {
      gButtonsDown &= ~DownBit(button);
    }
    event.buttonsDown = gButtonsDown;
    Deliver(event);
  }

  /// The modifier a key holds, as the engine's events name them (0: none).
  [[nodiscard]] std::uint32_t ModifierOf(const unsigned virtualKey)
  {
    switch (virtualKey) {
    case VK_SHIFT:
      return platform::window::kModifierShift;
    case VK_CONTROL:
      return platform::window::kModifierControl;
    case VK_MENU:
      return platform::window::kModifierAlt;
    default:
      return 0;
    }
  }

  /// The key of the left side for a modifier, which the engine's key state has beside it, as a keyboard's gives it.
  [[nodiscard]] unsigned SidedKeyOf(const unsigned virtualKey)
  {
    switch (virtualKey) {
    case VK_SHIFT:
      return VK_LSHIFT;
    case VK_CONTROL:
      return VK_LCONTROL;
    case VK_MENU:
      return VK_LMENU;
    default:
      return 0;
    }
  }
} // namespace

void host::RunOnGameThread(const Task task, const std::uintptr_t argument)
{
  if (!gQueue.Push(task, argument, &Wake)) {
    gpg::Warnf("Host: a task was given before SDL's events, and is dropped");
  }
}

void host::RunNewestOnGameThread(const Task task, const std::uintptr_t argument)
{
  if (!gQueue.PushNewest(task, argument, &Wake)) {
    gpg::Warnf("Host: a task was given before SDL's events, and is dropped");
  }
}

void host::RunWhenIdle(const Task task, const std::uintptr_t argument)
{
  if (!gQueue.PushIdle(task, argument, &Wake)) {
    gpg::Warnf("Host: an idle task was given before SDL's events, and is dropped");
  }
}

void host::MouseMove(const int x, const int y)
{
  gCursorX = x;
  gCursorY = y;
  // Android's window is the screen, at its origin: the cursor's place on the screen is its place in the window.
  platform::window::sdl::OnCursorMoved(x, y);
  Event event;
  event.type = EventType::MouseMove;
  event.x = x;
  event.y = y;
  event.buttonsDown = gButtonsDown;
  Deliver(event);
}

void host::MouseButton(const int button, const bool down, const int x, const int y)
{
  GiveButton(button, down, x, y, false);
}

void host::TouchButton(const int button, const bool down, const int x, const int y)
{
  GiveButton(button, down, x, y, true);
}

void host::MouseWheel(const int delta, const int x, const int y)
{
  if (x != gCursorX || y != gCursorY) {
    MouseMove(x, y);
  }
  Event event;
  event.type = EventType::MouseWheel;
  event.wheel = static_cast<float>(delta) / 120.0f;
  event.x = x;
  event.y = y;
  event.buttonsDown = gButtonsDown;
  Deliver(event);
}

void host::Key(const unsigned virtualKey, const bool down)
{
  if (const unsigned sided = SidedKeyOf(virtualKey); sided != 0) {
    platform::window::sdl::OnKey(static_cast<int>(sided), down);
  }
  platform::window::sdl::OnKey(static_cast<int>(virtualKey), down);
  if (const std::uint32_t modifier = ModifierOf(virtualKey); modifier != 0) {
    gModifiers = down ? gModifiers | modifier : gModifiers & ~modifier;
  }
  Event event;
  event.type = down ? EventType::KeyDown : EventType::KeyUp;
  event.virtualKey = static_cast<int>(virtualKey);
  Deliver(event);
}

std::uint32_t host::DoubleClickWait(const int button, const int x, const int y)
{
  return gDoubleClicks.Wait(button, x, y, static_cast<std::uint32_t>(SDL_GetTicks()));
}

void host::CursorPos(int& x, int& y)
{
  x = gCursorX;
  y = gCursorY;
}

bool host::PressFromFinger()
{
  return gPressFromFinger;
}

bool host::OnSdlEvent(const void* const sdlEvent)
{
  const SDL_Event& event = *static_cast<const SDL_Event*>(sdlEvent);
  if (event.type == WakeType()) {
    hostqueue::Entry entry;
    if (gQueue.Take(entry, &Wake)) {
      entry.task(entry.argument);
    }
    return true;
  }
  if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    gPressFromFinger = false; // a mouse's own, which SDL translates
  }
  return false;
}
