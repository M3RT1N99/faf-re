// The Win32 message layer (src/win32/User32Android.cpp) against Win32's documented behaviour: the
// queue's order and filters, messages sent within and across threads, WM_QUIT and WM_PAINT, the
// host's input as mouse and key messages with Windows' parameters, the low-level keyboard hooks,
// activation, the waits, destruction and the screen's surface.

#include <Windows.h>
#include <Win32AndroidHost.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "Check.h"
#include "Support.h"

using wxtest::Messages;
using wxtest::Received;
using wxtest::RecordingWindow;
using wxtest::Sequence;

namespace
{
  constexpr DWORD kShownPopup = WS_POPUP | WS_VISIBLE;

  /// The message the queue holds next in [first, last], taken.
  bool Take(MSG* const msg, const UINT first = 0, const UINT last = 0)
  {
    return ::PeekMessageW(msg, nullptr, first, last, PM_REMOVE) != FALSE;
  }

  void Click(const int button, const int x, const int y)
  {
    Win32Android_MouseButton(button, TRUE, x, y);
    Win32Android_MouseButton(button, FALSE, x, y);
  }
} // namespace

WXT_TEST(PostedMessagesComeInOrder)
{
  RecordingWindow window(nullptr, 0, 0, 100, 100, WS_POPUP);
  wxtest::Pump();
  for (UINT i = 1; i <= 3; ++i) {
    WXT_CHECK(::PostMessageW(window.hwnd(), WM_USER + i, i, 0));
  }
  MSG msg;
  for (UINT i = 1; i <= 3; ++i) {
    WXT_CHECK(Take(&msg, WM_USER, WM_USER + 100));
    WXT_CHECK_EQ(msg.message, WM_USER + i);
    WXT_CHECK(msg.hwnd == window.hwnd());
  }
  WXT_CHECK(!Take(&msg, WM_USER, WM_USER + 100));
}

WXT_TEST(PeekTakesTheFirstInItsRangeAndWindowAndCanLeaveIt)
{
  RecordingWindow first(nullptr, 0, 0, 100, 100, WS_POPUP);
  RecordingWindow second(nullptr, 0, 0, 100, 100, WS_POPUP);
  wxtest::Pump();
  (void)::PostMessageW(first.hwnd(), WM_USER + 1, 0, 0);
  (void)::PostMessageW(first.hwnd(), WM_USER + 5, 0, 0);
  (void)::PostMessageW(second.hwnd(), WM_USER + 1, 0, 0);

  MSG msg;
  WXT_CHECK(::PeekMessageW(&msg, first.hwnd(), WM_USER + 5, WM_USER + 5, PM_NOREMOVE));
  WXT_CHECK_EQ(msg.message, WM_USER + 5);
  WXT_CHECK(::PeekMessageW(&msg, first.hwnd(), WM_USER + 5, WM_USER + 5, PM_REMOVE));
  WXT_CHECK(!::PeekMessageW(&msg, first.hwnd(), WM_USER + 5, WM_USER + 5, PM_REMOVE));

  WXT_CHECK(::PeekMessageW(&msg, second.hwnd(), 0, 0, PM_REMOVE));
  WXT_CHECK(msg.hwnd == second.hwnd());
  WXT_CHECK(::PeekMessageW(&msg, first.hwnd(), 0, 0, PM_REMOVE));
  WXT_CHECK_EQ(msg.message, WM_USER + 1);
  WXT_CHECK(msg.hwnd == first.hwnd());
}

WXT_TEST(SendMessageOnTheWindowsThreadCallsItsProcedure)
{
  RecordingWindow window(nullptr, 0, 0, 100, 100, WS_POPUP);
  Messages().clear();
  WXT_CHECK_EQ(::SendMessageW(window.hwnd(), WM_USER + 7, 3, 4), LRESULT{7});
  const std::vector<wxtest::Message> received = Received(WM_USER + 7);
  WXT_CHECK_EQ(received.size(), std::size_t{1});
  if (!received.empty()) {
    WXT_CHECK_EQ(received[0].thread, ::GetCurrentThreadId());
    WXT_CHECK(!received[0].inSend);
  }
}

WXT_TEST(SendMessageFromAnotherThreadRunsOnTheWindowsThread)
{
  RecordingWindow window(nullptr, 0, 0, 100, 100, WS_POPUP);
  Messages().clear();
  const HWND hwnd = window.hwnd();
  std::atomic<bool> done{false};
  LRESULT result = 0;
  std::thread sender([hwnd, &done, &result] {
    result = ::SendMessageW(hwnd, WM_USER + 7, 30, 12);
    done.store(true);
  });

  // The procedure runs when this thread looks at its queue, as on Windows.
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (!done.load() && std::chrono::steady_clock::now() < until) {
    MSG msg;
    (void)::PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  WXT_CHECK(done.load());
  if (!done.load()) {
    sender.detach();
    return;
  }
  sender.join();
  WXT_CHECK_EQ(result, LRESULT{42});
  const std::vector<wxtest::Message> received = Received(WM_USER + 7);
  WXT_CHECK_EQ(received.size(), std::size_t{1});
  if (!received.empty()) {
    WXT_CHECK_EQ(received[0].thread, ::GetCurrentThreadId());
    WXT_CHECK(received[0].inSend);
  }
}

WXT_TEST(QuitComesAfterThePostedMessages)
{
  RecordingWindow window(nullptr, 0, 0, 100, 100, WS_POPUP);
  wxtest::Pump();
  (void)::PostMessageW(window.hwnd(), WM_USER + 1, 0, 0);
  ::PostQuitMessage(7);

  MSG msg;
  WXT_CHECK_EQ(::GetMessageW(&msg, nullptr, 0, 0), TRUE);
  WXT_CHECK_EQ(msg.message, WM_USER + 1);
  // GetMessage would wait for ever without the quit: looked for first.
  WXT_CHECK(::PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE));
  if (msg.message == WM_QUIT) {
    WXT_CHECK_EQ(::GetMessageW(&msg, nullptr, 0, 0), FALSE);
    WXT_CHECK_EQ(msg.message, UINT{WM_QUIT});
    WXT_CHECK_EQ(msg.wParam, WPARAM{7});
  } else {
    WXT_CHECK_EQ(msg.message, UINT{WM_QUIT});
  }
}

WXT_TEST(PaintComesWhenTheQueueIsEmptyUntilValidated)
{
  RecordingWindow window(nullptr, 0, 0, 50, 50, kShownPopup);
  wxtest::Pump();
  (void)::PostMessageW(window.hwnd(), WM_USER + 1, 0, 0);
  WXT_CHECK(::InvalidateRect(window.hwnd(), nullptr, FALSE));

  MSG msg;
  WXT_CHECK(Take(&msg));
  WXT_CHECK_EQ(msg.message, WM_USER + 1);
  WXT_CHECK(Take(&msg));
  WXT_CHECK_EQ(msg.message, UINT{WM_PAINT});
  // Taking WM_PAINT does not remove it: the update region does, once validated.
  WXT_CHECK(Take(&msg));
  WXT_CHECK_EQ(msg.message, UINT{WM_PAINT});
  WXT_CHECK(::ValidateRect(window.hwnd(), nullptr));
  WXT_CHECK(!Take(&msg));
}

WXT_TEST(AButtonGoesToTheWindowUnderThePointerInItsCoordinates)
{
  RecordingWindow top(nullptr, 100, 100, 300, 200, kShownPopup);
  RecordingWindow child(top.hwnd(), 10, 10, 50, 50, WS_CHILD | WS_VISIBLE);
  wxtest::Pump();
  Messages().clear();

  Click(WIN32ANDROID_BUTTON_LEFT, 120, 115);
  wxtest::Pump();
  const std::vector<wxtest::Message> downs = Received(WM_LBUTTONDOWN);
  const std::vector<wxtest::Message> ups = Received(WM_LBUTTONUP);
  WXT_CHECK_EQ(downs.size(), std::size_t{1});
  WXT_CHECK_EQ(ups.size(), std::size_t{1});
  if (downs.size() == 1 && ups.size() == 1) {
    WXT_CHECK(downs[0].hwnd == child.hwnd());
    WXT_CHECK_EQ(downs[0].wParam, WPARAM{MK_LBUTTON});
    WXT_CHECK_EQ(downs[0].lParam, LPARAM{MAKELPARAM(10, 5)});
    WXT_CHECK(ups[0].hwnd == child.hwnd());
    WXT_CHECK_EQ(ups[0].wParam, WPARAM{0});
    WXT_CHECK_EQ(ups[0].lParam, LPARAM{MAKELPARAM(10, 5)});
  }

  // WM_SETCURSOR comes before the press, for the window under the pointer.
  const std::vector<UINT> order = Sequence({WM_SETCURSOR, WM_LBUTTONDOWN});
  WXT_CHECK(order.size() >= 2 && order[0] == WM_SETCURSOR);
  const std::vector<wxtest::Message> cursors = Received(WM_SETCURSOR);
  if (!cursors.empty()) {
    WXT_CHECK(cursors[0].hwnd == child.hwnd());
    WXT_CHECK(cursors[0].wParam == reinterpret_cast<WPARAM>(child.hwnd()));
    WXT_CHECK_EQ(cursors[0].lParam, LPARAM{MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)});
  }
}

WXT_TEST(TheCaptureTakesTheMouseEverywhere)
{
  RecordingWindow top(nullptr, 100, 100, 300, 200, kShownPopup);
  RecordingWindow child(top.hwnd(), 10, 10, 50, 50, WS_CHILD | WS_VISIBLE);
  wxtest::Pump();
  (void)::SetCapture(child.hwnd());
  WXT_CHECK(::GetCapture() == child.hwnd());
  Messages().clear();

  Click(WIN32ANDROID_BUTTON_LEFT, 400, 300);
  wxtest::Pump();
  const std::vector<wxtest::Message> downs = Received(WM_LBUTTONDOWN);
  WXT_CHECK_EQ(downs.size(), std::size_t{1});
  if (!downs.empty()) {
    WXT_CHECK(downs[0].hwnd == child.hwnd());
    WXT_CHECK_EQ(downs[0].lParam, LPARAM{MAKELPARAM(290, 190)});
  }
  // No WM_SETCURSOR while a window has the capture.
  WXT_CHECK(Received(WM_SETCURSOR).empty());
  WXT_CHECK(::ReleaseCapture());
}

WXT_TEST(ASecondClickIsADoubleClickWhereTheClassWantsOne)
{
  RecordingWindow doubleClicks(nullptr, 0, 0, 100, 100, kShownPopup, true);
  RecordingWindow plain(nullptr, 200, 0, 100, 100, kShownPopup, false);
  wxtest::Pump();
  Messages().clear();

  Click(WIN32ANDROID_BUTTON_LEFT, 50, 50);
  Click(WIN32ANDROID_BUTTON_LEFT, 50, 50);
  wxtest::Pump();
  const std::vector<UINT> buttons{WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK};
  WXT_CHECK(Sequence(buttons) == (std::vector<UINT>{WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK, WM_LBUTTONUP}));

  Messages().clear();
  Click(WIN32ANDROID_BUTTON_LEFT, 250, 50);
  Click(WIN32ANDROID_BUTTON_LEFT, 250, 50);
  wxtest::Pump();
  WXT_CHECK(Sequence(buttons) == (std::vector<UINT>{WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDOWN, WM_LBUTTONUP}));
}

WXT_TEST(KeysGoToTheFocusWithWindowsParameters)
{
  RecordingWindow window(nullptr, 0, 0, 100, 100, kShownPopup);
  (void)::SetFocus(window.hwnd());
  wxtest::Pump();

  MSG msg;
  Win32Android_Key('A', 0x1E, TRUE, FALSE, L'a');
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_KEYDOWN});
  WXT_CHECK(msg.hwnd == window.hwnd());
  WXT_CHECK_EQ(msg.wParam, WPARAM{'A'});
  // Repeat count 1, scan code 0x1E, not extended, up before.
  WXT_CHECK_EQ(msg.lParam, LPARAM{0x001E0001});
  WXT_CHECK(::GetKeyState('A') < 0);

  // TranslateMessage puts the character next.
  WXT_CHECK(::TranslateMessage(&msg));
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_CHAR});
  WXT_CHECK_EQ(msg.wParam, WPARAM{L'a'});
  WXT_CHECK_EQ(msg.lParam, LPARAM{0x001E0001});

  // Held down: the previous state is down (bit 30).
  Win32Android_Key('A', 0x1E, TRUE, FALSE, L'a');
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.lParam, LPARAM{0x401E0001});

  // Released: bits 30 and 31.
  Win32Android_Key('A', 0x1E, FALSE, FALSE, 0);
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_KEYUP});
  WXT_CHECK_EQ(msg.lParam, LPARAM{0xC01E0001});
  WXT_CHECK(::GetKeyState('A') >= 0);

  // An extended key has bit 24.
  Win32Android_Key(VK_RIGHT, 0x4D, TRUE, TRUE, 0);
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.wParam, WPARAM{VK_RIGHT});
  WXT_CHECK_EQ(msg.lParam, LPARAM{0x014D0001});
  // No character: TranslateMessage posts none.
  (void)::TranslateMessage(&msg);
  Win32Android_Key(VK_RIGHT, 0x4D, FALSE, TRUE, 0);
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_KEYUP});
  WXT_CHECK_EQ(msg.lParam, LPARAM{0xC14D0001});
}

WXT_TEST(AltAndF10MakeSystemKeys)
{
  RecordingWindow window(nullptr, 0, 0, 100, 100, kShownPopup);
  (void)::SetFocus(window.hwnd());
  wxtest::Pump();

  MSG msg;
  // Alt itself, and a key while Alt is down: WM_SYSKEYDOWN with the context code (bit 29).
  Win32Android_Key(VK_MENU, 0x38, TRUE, FALSE, 0);
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_SYSKEYDOWN});
  WXT_CHECK_EQ(msg.lParam, LPARAM{0x20380001});
  Win32Android_Key('X', 0x2D, TRUE, FALSE, L'x');
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_SYSKEYDOWN});
  WXT_CHECK_EQ(msg.wParam, WPARAM{'X'});
  WXT_CHECK_EQ(msg.lParam, LPARAM{0x202D0001});
  Win32Android_Key('X', 0x2D, FALSE, FALSE, 0);
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_SYSKEYUP});
  WXT_CHECK_EQ(msg.lParam, LPARAM{0xE02D0001});
  Win32Android_Key(VK_MENU, 0x38, FALSE, FALSE, 0);
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.wParam, WPARAM{VK_MENU});

  // F10 alone is a system key, without Alt's context code.
  Win32Android_Key(VK_F10, 0x44, TRUE, FALSE, 0);
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_SYSKEYDOWN});
  WXT_CHECK_EQ(msg.lParam, LPARAM{0x00440001});
  Win32Android_Key(VK_F10, 0x44, FALSE, FALSE, 0);
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_SYSKEYUP});
}

namespace
{
  struct Hooked
  {
    WPARAM message;
    DWORD key;
    DWORD scanCode;
    DWORD flags;
  };
  std::vector<Hooked> gHooked;
  bool gSwallow = false;

  LRESULT CALLBACK KeyboardHook(const int code, const WPARAM wParam, const LPARAM lParam)
  {
    if (code == HC_ACTION) {
      const auto* const info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
      gHooked.push_back(Hooked{wParam, info->vkCode, info->scanCode, info->flags});
      if (gSwallow) {
        return 1;
      }
    }
    return ::CallNextHookEx(nullptr, code, wParam, lParam);
  }
} // namespace

WXT_TEST(ALowLevelHookSeesKeysFirstAndCanSwallowThem)
{
  RecordingWindow window(nullptr, 0, 0, 100, 100, kShownPopup);
  (void)::SetFocus(window.hwnd());
  wxtest::Pump();
  gHooked.clear();
  gSwallow = false;
  const HHOOK hook = ::SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardHook, nullptr, 0);
  WXT_CHECK(hook != nullptr);

  MSG msg;
  Win32Android_Key('Q', 0x10, TRUE, FALSE, L'q');
  WXT_CHECK_EQ(gHooked.size(), std::size_t{1});
  if (gHooked.size() == 1) {
    WXT_CHECK_EQ(gHooked[0].message, WPARAM{WM_KEYDOWN});
    WXT_CHECK_EQ(gHooked[0].key, DWORD{'Q'});
    WXT_CHECK_EQ(gHooked[0].scanCode, DWORD{0x10});
    WXT_CHECK_EQ(gHooked[0].flags, DWORD{0});
  }
  WXT_CHECK(Take(&msg, WM_KEYFIRST, WM_KEYLAST));
  WXT_CHECK_EQ(msg.message, UINT{WM_KEYDOWN});

  // What the hook swallows reaches no queue.
  gSwallow = true;
  Win32Android_Key('Q', 0x10, FALSE, FALSE, 0);
  WXT_CHECK_EQ(gHooked.size(), std::size_t{2});
  if (gHooked.size() == 2) {
    WXT_CHECK_EQ(gHooked[1].message, WPARAM{WM_KEYUP});
    WXT_CHECK_EQ(gHooked[1].flags, DWORD{LLKHF_UP});
  }
  WXT_CHECK(!Take(&msg, WM_KEYFIRST, WM_KEYLAST));

  gSwallow = false;
  WXT_CHECK(::UnhookWindowsHookEx(hook));
  // Released for the tests after this one.
  Win32Android_Key('Q', 0x10, FALSE, FALSE, 0);
  WXT_CHECK_EQ(gHooked.size(), std::size_t{2});
  wxtest::Pump();
}

WXT_TEST(TheForegroundActivatesTheApplicationAndComesBackToItsWindow)
{
  Win32Android_SetForeground(FALSE);
  wxtest::Pump();
  RecordingWindow window(nullptr, 0, 0, 100, 100, WS_POPUP);
  (void)::ShowWindow(window.hwnd(), SW_SHOW);
  wxtest::Pump();

  // The window is active already: coming to the front activates the application.
  Messages().clear();
  Win32Android_SetForeground(TRUE);
  wxtest::Pump();
  std::vector<wxtest::Message> apps = Received(WM_ACTIVATEAPP);
  WXT_CHECK_EQ(apps.size(), std::size_t{1});
  if (!apps.empty()) {
    WXT_CHECK_EQ(apps[0].wParam, WPARAM{TRUE});
  }

  // To the back: the application and its window go inactive.
  Messages().clear();
  Win32Android_SetForeground(FALSE);
  wxtest::Pump();
  apps = Received(WM_ACTIVATEAPP);
  const std::vector<wxtest::Message> activates = Received(WM_ACTIVATE);
  WXT_CHECK_EQ(apps.size(), std::size_t{1});
  WXT_CHECK_EQ(activates.size(), std::size_t{1});
  if (apps.size() == 1 && activates.size() == 1) {
    WXT_CHECK_EQ(apps[0].wParam, WPARAM{FALSE});
    WXT_CHECK_EQ(LOWORD(activates[0].wParam), WORD{WA_INACTIVE});
  }

  // Back to the front: the window that was active is activated again, the application first.
  Messages().clear();
  Win32Android_SetForeground(TRUE);
  wxtest::Pump();
  WXT_CHECK(Sequence({WM_ACTIVATEAPP, WM_ACTIVATE}) == (std::vector<UINT>{WM_ACTIVATEAPP, WM_ACTIVATE}));
  const std::vector<wxtest::Message> again = Received(WM_ACTIVATE);
  if (!again.empty()) {
    WXT_CHECK(again[0].hwnd == window.hwnd());
    WXT_CHECK_EQ(LOWORD(again[0].wParam), WORD{WA_ACTIVE});
  }
  WXT_CHECK(::GetActiveWindow() == window.hwnd());

  Win32Android_SetForeground(FALSE);
  wxtest::Pump();
}

WXT_TEST(MsgWaitWakesForNewMessagesAndOldOnesOnlyWhenAsked)
{
  RecordingWindow window(nullptr, 0, 0, 100, 100, WS_POPUP);
  wxtest::Pump();
  WXT_CHECK_EQ(::MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, 0), DWORD{WAIT_TIMEOUT});

  // A message from another thread wakes the wait.
  const HWND hwnd = window.hwnd();
  std::thread poster([hwnd] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    (void)::PostMessageW(hwnd, WM_USER + 2, 0, 0);
  });
  WXT_CHECK_EQ(::MsgWaitForMultipleObjectsEx(0, nullptr, 10000, QS_ALLINPUT, 0), DWORD{WAIT_OBJECT_0});
  poster.join();

  // Once the thread has looked, the message wakes a wait only with MWMO_INPUTAVAILABLE.
  MSG msg;
  WXT_CHECK(::PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE));
  WXT_CHECK_EQ(::MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, 0), DWORD{WAIT_TIMEOUT});
  WXT_CHECK_EQ(::MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE), DWORD{WAIT_OBJECT_0});

  // A signalled handle comes first; input is the count's own number.
  const HANDLE event = ::CreateEventW(nullptr, TRUE, TRUE, nullptr);
  WXT_CHECK_EQ(::MsgWaitForMultipleObjectsEx(1, &event, 20, QS_ALLINPUT, 0), DWORD{WAIT_OBJECT_0});
  (void)::ResetEvent(event);
  (void)::PostMessageW(hwnd, WM_USER + 3, 0, 0);
  WXT_CHECK_EQ(::MsgWaitForMultipleObjectsEx(1, &event, 20, QS_ALLINPUT, 0), DWORD{WAIT_OBJECT_0 + 1});
  (void)::CloseHandle(event);
  wxtest::Pump();
}

WXT_TEST(AnEndlessMsgWaitOnASilentQueueGetsAMessage)
{
  wxtest::Pump();
  WXT_CHECK_EQ(::MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE), DWORD{WAIT_OBJECT_0});
  MSG msg;
  WXT_CHECK(::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE));
  WXT_CHECK_EQ(msg.message, UINT{WM_NULL});
  WXT_CHECK(msg.hwnd == nullptr);
  WXT_CHECK(!::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE));
}

WXT_TEST(DestroyingAWindowDestroysItsChildren)
{
  RecordingWindow top(nullptr, 0, 0, 100, 100, WS_POPUP);
  RecordingWindow child(top.hwnd(), 0, 0, 10, 10, WS_CHILD);
  Messages().clear();
  WXT_CHECK(::DestroyWindow(top.hwnd()));
  WXT_CHECK(!::IsWindow(top.hwnd()));
  WXT_CHECK(!::IsWindow(child.hwnd()));

  // WM_DESTROY from the top, WM_NCDESTROY once the children are gone.
  std::vector<HWND> order;
  for (const wxtest::Message& message : Messages()) {
    if (message.message == WM_DESTROY || message.message == WM_NCDESTROY) {
      order.push_back(message.hwnd);
    }
  }
  WXT_CHECK(Sequence({WM_DESTROY, WM_NCDESTROY}) == (std::vector<UINT>{WM_DESTROY, WM_DESTROY, WM_NCDESTROY, WM_NCDESTROY}));
  WXT_CHECK(order == (std::vector<HWND>{top.hwnd(), child.hwnd(), child.hwnd(), top.hwnd()}));
}

WXT_TEST(TheScreensSurfaceIsEveryWindowsSurface)
{
  RecordingWindow top(nullptr, 0, 0, 100, 100, WS_POPUP);
  RecordingWindow child(top.hwnd(), 0, 0, 10, 10, WS_CHILD);
  int stand = 0;
  auto* const surface = reinterpret_cast<ANativeWindow*>(&stand);
  WXT_CHECK(Win32Android_GetWindowSurface(child.hwnd()) == nullptr);

  Win32Android_SetScreenSurface(surface);
  WXT_CHECK_EQ(wxtest::SurfaceReferences(surface), 1);
  WXT_CHECK(Win32Android_GetWindowSurface(top.hwnd()) == surface);
  WXT_CHECK(Win32Android_GetWindowSurface(child.hwnd()) == surface);
  WXT_CHECK(Win32Android_GetWindowSurface(nullptr) == nullptr);

  Win32Android_SetScreenSurface(nullptr);
  WXT_CHECK_EQ(wxtest::SurfaceReferences(surface), 0);
  WXT_CHECK(Win32Android_GetWindowSurface(top.hwnd()) == nullptr);
}

WXT_TEST(AMaximizedWindowFollowsTheScreen)
{
  RecordingWindow window(nullptr, 10, 10, 100, 100, WS_POPUP);
  (void)::ShowWindow(window.hwnd(), SW_MAXIMIZE);
  wxtest::Pump();
  RECT rect{};
  WXT_CHECK(::GetClientRect(window.hwnd(), &rect));
  WXT_CHECK_EQ(rect.right - rect.left, LONG{wxtest::kScreenWidth});
  WXT_CHECK_EQ(rect.bottom - rect.top, LONG{wxtest::kScreenHeight});

  Messages().clear();
  Win32Android_SetScreenSize(1024, 768);
  wxtest::Pump();
  WXT_CHECK(::GetClientRect(window.hwnd(), &rect));
  WXT_CHECK_EQ(rect.right - rect.left, LONG{1024});
  WXT_CHECK_EQ(rect.bottom - rect.top, LONG{768});
  const std::vector<wxtest::Message> sizes = Received(WM_SIZE);
  WXT_CHECK(!sizes.empty() && sizes.back().lParam == LPARAM{MAKELPARAM(1024, 768)});

  Win32Android_SetScreenSize(wxtest::kScreenWidth, wxtest::kScreenHeight);
  wxtest::Pump();
}
