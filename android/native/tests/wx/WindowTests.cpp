// The translation of the message layer's messages into wx events (src/wx/window.cpp,
// MSWWindowProc) against wxWindows 2.4.2's src/msw/window.cpp: the mouse, the wheel, the keys,
// size, focus and close, as the engine's windows get them from the host's input.

#include <Windows.h>
#include <Win32AndroidHost.h>

#include <cstdint>
#include <vector>

#include "Check.h"
#include "Support.h"
#include "wx/frame.h"
#include "wx/window.h"

namespace
{
  HWND Hwnd(const wxWindow* const window)
  {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window->GetHWND()));
  }

  /// A window that writes down the wx events it gets, and skips them as the engine's handlers
  /// skip what they do not take.
  class EventWindow : public wxWindow
  {
  public:
    struct Mouse
    {
      wxEventType type;
      int x;
      int y;
      bool leftIsDown;
      bool shift;
      int wheel;
      int delta;
      int lines;
    };
    struct Key
    {
      wxEventType type;
      long code;
      bool shift;
    };

    EventWindow(wxWindow* const parent, const wxPoint& position, const wxSize& size)
      : wxWindow(parent, wxID_ANY, position, size)
    {}

    std::vector<Mouse> mouse;
    std::vector<Key> keys;
    std::vector<wxSize> sizes;
    std::vector<wxEventType> focus;
    bool takeKeyDown = false;

    void OnMouse(wxMouseEvent& event)
    {
      mouse.push_back(Mouse{
        event.GetEventType(), event.GetX(), event.GetY(), event.LeftIsDown(), event.ShiftDown(), event.GetWheelRotation(),
        event.GetWheelDelta(), event.GetLinesPerAction()
      });
      event.Skip();
    }
    void OnKeyDown(wxKeyEvent& event)
    {
      keys.push_back(Key{event.GetEventType(), event.GetKeyCode(), event.ShiftDown()});
      if (!takeKeyDown) {
        event.Skip();
      }
    }
    void OnKey(wxKeyEvent& event)
    {
      keys.push_back(Key{event.GetEventType(), event.GetKeyCode(), event.ShiftDown()});
      event.Skip();
    }
    void OnSize(wxSizeEvent& event)
    {
      sizes.push_back(event.GetSize());
      event.Skip();
    }
    void OnFocus(wxFocusEvent& event)
    {
      focus.push_back(event.GetEventType());
      event.Skip();
    }

    void Forget()
    {
      mouse.clear();
      keys.clear();
      sizes.clear();
      focus.clear();
    }

    DECLARE_EVENT_TABLE()
  };

  BEGIN_EVENT_TABLE(EventWindow, wxWindow)
    EVT_MOUSE_EVENTS(EventWindow::OnMouse)
    EVT_KEY_DOWN(EventWindow::OnKeyDown)
    EVT_KEY_UP(EventWindow::OnKey)
    EVT_CHAR(EventWindow::OnKey)
    EVT_SIZE(EventWindow::OnSize)
    EVT_SET_FOCUS(EventWindow::OnFocus)
    EVT_KILL_FOCUS(EventWindow::OnFocus)
  END_EVENT_TABLE()

  /// A frame that writes down its close events and, when told to, vetoes them.
  class CloseFrame : public wxFrame
  {
  public:
    CloseFrame()
      : wxFrame(nullptr, wxID_ANY, wxT("window test"), wxPoint(0, 0), wxSize(400, 300))
    {}

    int closes = 0;
    bool couldVeto = false;

    void OnClose(wxCloseEvent& event)
    {
      ++closes;
      couldVeto = event.CanVeto();
      event.Veto();
    }

    DECLARE_EVENT_TABLE()
  };

  BEGIN_EVENT_TABLE(CloseFrame, wxFrame)
    EVT_CLOSE(CloseFrame::OnClose)
  END_EVENT_TABLE()

  /// A shown frame at the screen's origin with an event window inside; gone with the object.
  struct Scene
  {
    CloseFrame* frame;
    EventWindow* window;

    Scene()
      : frame(new CloseFrame)
      , window(new EventWindow(frame, wxPoint(10, 20), wxSize(100, 80)))
    {
      frame->Show(true);
      wxtest::Pump();
      window->Forget();
    }
    ~Scene()
    {
      delete frame;
      wxtest::Pump();
    }
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    /// A point of the event window, in the screen's pixels.
    wxPoint Screen(const int x, const int y) const
    {
      return window->ClientToScreen(wxPoint(x, y));
    }

    void Key(const UINT virtualKey, const UINT scanCode, const wchar_t character)
    {
      Win32Android_Key(virtualKey, scanCode, TRUE, FALSE, character);
      Win32Android_Key(virtualKey, scanCode, FALSE, FALSE, 0);
      wxtest::Pump();
    }
  };

  std::vector<wxEventType> Types(const std::vector<EventWindow::Mouse>& events)
  {
    std::vector<wxEventType> out;
    for (const EventWindow::Mouse& event : events) {
      out.push_back(event.type);
    }
    return out;
  }

  std::vector<wxEventType> Types(const std::vector<EventWindow::Key>& events)
  {
    std::vector<wxEventType> out;
    for (const EventWindow::Key& event : events) {
      out.push_back(event.type);
    }
    return out;
  }
} // namespace

WXT_TEST(AClickBecomesLeftDownAndUpInTheWindowsCoordinates)
{
  Scene scene;
  const wxPoint point = scene.Screen(5, 6);
  // Into the window first, so that only the click's own events follow.
  Win32Android_MouseMove(point.x, point.y);
  wxtest::Pump();
  scene.window->Forget();

  Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, TRUE, point.x, point.y);
  Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, FALSE, point.x, point.y);
  wxtest::Pump();
  const std::vector<EventWindow::Mouse>& mouse = scene.window->mouse;
  WXT_CHECK(Types(mouse) == (std::vector<wxEventType>{wxEVT_LEFT_DOWN, wxEVT_LEFT_UP}));
  if (mouse.size() == 2) {
    WXT_CHECK_EQ(mouse[0].x, 5);
    WXT_CHECK_EQ(mouse[0].y, 6);
    WXT_CHECK(mouse[0].leftIsDown);
    WXT_CHECK(!mouse[1].leftIsDown);
  }
}

WXT_TEST(AClickGivesTheWindowTheFocus)
{
  Scene scene;
  // Another window than the one the frame's activation may have focused.
  auto* const other = new EventWindow(scene.frame, wxPoint(200, 20), wxSize(50, 50));
  wxtest::Pump();
  other->Forget();

  const wxPoint point = other->ClientToScreen(wxPoint(10, 10));
  Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, TRUE, point.x, point.y);
  Win32Android_MouseButton(WIN32ANDROID_BUTTON_LEFT, FALSE, point.x, point.y);
  wxtest::Pump();
  WXT_CHECK(other->focus == std::vector<wxEventType>{wxEVT_SET_FOCUS});
  WXT_CHECK(wxWindow::FindFocus() == other);
}

WXT_TEST(TheFirstMoveIntoTheWindowEntersIt)
{
  Scene scene;
  const wxPoint point = scene.Screen(30, 31);
  Win32Android_MouseMove(point.x, point.y);
  wxtest::Pump();
  const std::vector<EventWindow::Mouse>& mouse = scene.window->mouse;
  WXT_CHECK(Types(mouse) == (std::vector<wxEventType>{wxEVT_ENTER_WINDOW, wxEVT_MOTION}));
  if (mouse.size() == 2) {
    WXT_CHECK_EQ(mouse[1].x, 30);
    WXT_CHECK_EQ(mouse[1].y, 31);
  }

  // Moving on inside: motion alone.
  scene.window->Forget();
  Win32Android_MouseMove(point.x + 1, point.y);
  wxtest::Pump();
  WXT_CHECK(Types(scene.window->mouse) == std::vector<wxEventType>{wxEVT_MOTION});
}

WXT_TEST(TheWheelBecomesMouseWheelAtTheScreensPosition)
{
  Scene scene;
  scene.window->SetFocus();
  wxtest::Pump();
  scene.window->Forget();

  const wxPoint point = scene.Screen(40, 30);
  Win32Android_MouseWheel(-240, point.x, point.y);
  wxtest::Pump();
  const std::vector<EventWindow::Mouse>& mouse = scene.window->mouse;
  WXT_CHECK(Types(mouse) == std::vector<wxEventType>{wxEVT_MOUSEWHEEL});
  if (mouse.size() == 1) {
    WXT_CHECK_EQ(mouse[0].wheel, -240);
    WXT_CHECK_EQ(mouse[0].delta, 120);
    WXT_CHECK_EQ(mouse[0].lines, 3);
    // wxWindows 2.4.2 leaves WM_MOUSEWHEEL's screen position as it is.
    WXT_CHECK_EQ(mouse[0].x, point.x);
    WXT_CHECK_EQ(mouse[0].y, point.y);
  }
}

WXT_TEST(KeysBecomeKeyDownCharAndKeyUp)
{
  Scene scene;
  scene.window->SetFocus();
  wxtest::Pump();
  scene.window->Forget();

  scene.Key('A', 0x1E, L'a');
  std::vector<EventWindow::Key>& keys = scene.window->keys;
  WXT_CHECK(Types(keys) == (std::vector<wxEventType>{wxEVT_KEY_DOWN, wxEVT_CHAR, wxEVT_KEY_UP}));
  if (keys.size() == 3) {
    WXT_CHECK_EQ(keys[0].code, long{'A'});
    WXT_CHECK_EQ(keys[1].code, long{'a'});
    WXT_CHECK_EQ(keys[2].code, long{'A'});
  }

  // A key without a character gets wx's code, and its char event comes from the key down.
  keys.clear();
  scene.Key(VK_F1, 0x3B, 0);
  WXT_CHECK(Types(keys) == (std::vector<wxEventType>{wxEVT_KEY_DOWN, wxEVT_CHAR, wxEVT_KEY_UP}));
  for (const EventWindow::Key& key : keys) {
    WXT_CHECK_EQ(key.code, long{WXK_F1});
  }
}

WXT_TEST(AKeyDownHandlerThatTakesTheKeyStopsItsChar)
{
  Scene scene;
  scene.window->SetFocus();
  wxtest::Pump();
  scene.window->Forget();
  scene.window->takeKeyDown = true;

  scene.Key('A', 0x1E, L'a');
  WXT_CHECK(Types(scene.window->keys) == (std::vector<wxEventType>{wxEVT_KEY_DOWN, wxEVT_KEY_UP}));
}

WXT_TEST(ShiftComesWithTheKeysWhileDown)
{
  Scene scene;
  scene.window->SetFocus();
  wxtest::Pump();
  scene.window->Forget();

  Win32Android_Key(VK_SHIFT, 0x2A, TRUE, FALSE, 0);
  scene.Key('A', 0x1E, L'A');
  Win32Android_Key(VK_SHIFT, 0x2A, FALSE, FALSE, 0);
  wxtest::Pump();

  bool sawA = false;
  for (const EventWindow::Key& key : scene.window->keys) {
    if (key.code == 'A' && key.type == wxEVT_CHAR) {
      sawA = true;
      WXT_CHECK(key.shift);
    }
  }
  WXT_CHECK(sawA);
  WXT_CHECK(::GetKeyState(VK_SHIFT) >= 0);
}

WXT_TEST(ASizeEventCarriesTheWindowsSize)
{
  Scene scene;
  scene.window->SetSize(10, 20, 123, 45);
  wxtest::Pump();
  const std::vector<wxSize>& sizes = scene.window->sizes;
  WXT_CHECK(!sizes.empty());
  if (!sizes.empty()) {
    WXT_CHECK_EQ(sizes.back().GetWidth(), 123);
    WXT_CHECK_EQ(sizes.back().GetHeight(), 45);
  }
}

WXT_TEST(TheFocusMovesWithSetFocus)
{
  Scene scene;
  auto* const other = new EventWindow(scene.frame, wxPoint(200, 20), wxSize(50, 50));
  wxtest::Pump();
  scene.window->SetFocus();
  wxtest::Pump();
  scene.window->Forget();
  other->Forget();

  other->SetFocus();
  wxtest::Pump();
  WXT_CHECK(scene.window->focus == std::vector<wxEventType>{wxEVT_KILL_FOCUS});
  WXT_CHECK(other->focus == std::vector<wxEventType>{wxEVT_SET_FOCUS});
  WXT_CHECK(wxWindow::FindFocus() == other);
}

WXT_TEST(WmCloseAsksTheFrameWhichMayVeto)
{
  Scene scene;
  (void)::SendMessageW(Hwnd(scene.frame), WM_CLOSE, 0, 0);
  wxtest::Pump();
  WXT_CHECK_EQ(scene.frame->closes, 1);
  WXT_CHECK(scene.frame->couldVeto);
  // Vetoed: the frame stays.
  WXT_CHECK(::IsWindow(Hwnd(scene.frame)));
}
