#pragma once

// What the wx-lite tests share: a message pump as the engine's loop has one, a Win32 window that
// records what its procedure receives, and the application object.

#include <Windows.h>

#include <vector>

#include "wx/app.h"
#include "wx/event.h"

struct ANativeWindow;

namespace wxtest
{
  /// Takes and dispatches every message the thread has (with TranslateMessage, as the engine's
  /// loop does), and so what other threads sent and the host's tasks.
  void Pump();

  /// A message a recording window's procedure received.
  struct Message
  {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD thread;  ///< the thread the procedure ran on
    BOOL inSend;   ///< InSendMessage() in the procedure
  };

  /// Every message the recording windows received, in order.
  std::vector<Message>& Messages();

  /// Those of one kind.
  std::vector<Message> Received(UINT message);

  /// The kinds received, in order, of those among `kinds`.
  std::vector<UINT> Sequence(const std::vector<UINT>& kinds);

  /// A window of a class whose procedure records every message and otherwise answers as
  /// DefWindowProcW; WM_USER + 7 returns wParam + lParam. `doubleClicks` gives the class
  /// CS_DBLCLKS. The window is destroyed with the object.
  class RecordingWindow
  {
  public:
    RecordingWindow(HWND parent, int x, int y, int width, int height, DWORD style, bool doubleClicks = true);
    ~RecordingWindow();
    RecordingWindow(const RecordingWindow&) = delete;
    RecordingWindow& operator=(const RecordingWindow&) = delete;

    HWND hwnd() const
    {
      return mWindow;
    }

  private:
    HWND mWindow;
  };

  /// The screen the tests run on (Win32Android_SetScreenSize).
  constexpr int kScreenWidth = 800;
  constexpr int kScreenHeight = 600;

  /// The references the layer holds on a surface: ANativeWindow_acquire less release
  /// (NdkDoubles.cpp).
  int SurfaceReferences(ANativeWindow* window);
} // namespace wxtest

DECLARE_EVENT_TYPE(wxEVT_TEST_APP, -1)

/// The application object (wxTheApp), as the engine has MohoApp: it records the test events and
/// idle events that reach it.
class TestApp : public wxApp
{
public:
  std::vector<int> reached; ///< the ids of the wxEVT_TEST_APP events that reached the application
  int idles = 0;            ///< the idle events that reached it

  void OnTestEvent(wxCommandEvent& event);
  void OnIdleEvent(wxIdleEvent& event);

  static TestApp& Get()
  {
    return *static_cast<TestApp*>(wxTheApp);
  }

  DECLARE_EVENT_TABLE()
};
