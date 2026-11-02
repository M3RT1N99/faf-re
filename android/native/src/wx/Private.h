#pragma once

// wx-lite's own helpers (android/native/WX_LITE.md), as wxWindows 2.4.2 keeps them in
// include/wx/msw/private.h: the handle conversions, the window class, the window procedure and
// the association of HWNDs with wx windows, and the text metrics.

#include <Windows.h>

#include <cstdint>

#include "wx/window.h"

namespace wxlite
{
  inline HWND ToHwnd(const WXHWND handle)
  {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle));
  }

  inline WXHWND ToWx(const HWND handle)
  {
    return static_cast<WXHWND>(reinterpret_cast<std::uintptr_t>(handle));
  }

  inline HWND GetHwndOf(const wxWindow* const window)
  {
    return window != nullptr ? ToHwnd(window->GetHWND()) : nullptr;
  }

  /// The metrics of the text in wx-lite's windows. A Windows build measures the default GUI font
  /// (8-point MS Sans Serif at 96 dpi: 13 pixels high, 5 wide on average, which makes a standard
  /// button 75x23); Android has no GDI font for wx, and nothing wx draws is shown, so the windows
  /// are measured with that font's numbers: sizers lay dialogs out in the sizes Windows gives
  /// them.
  constexpr int kCharHeight = 13;
  constexpr int kCharWidth = 5;

  /// The size of `text` in those metrics: the longest line, and a line each.
  wxSize TextExtent(const wxString& text);

  // The class of wx-lite's windows (wxApp::RegisterWindowClasses), and the one of windows with
  // wxNO_FULL_REPAINT_ON_RESIZE.
  extern const wxChar* const kCanvasClassName;
  extern const wxChar* const kCanvasClassNameNR;

  LRESULT WindowProcedure(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

  void AssociateWinWithHandle(HWND hWnd, wxWindow* win);
  void RemoveHandleAssociation(wxWindow* win);

  /// The window being created: its messages from CreateWindowEx on reach it before the handle is
  /// known (wxWindowCreationHook).
  class WindowCreationHook
  {
  public:
    explicit WindowCreationHook(wxWindow* winBeingCreated);
    ~WindowCreationHook();
  };

  inline bool IsShiftDown()
  {
    return ::GetKeyState(VK_SHIFT) < 0;
  }

  inline bool IsCtrlDown()
  {
    return ::GetKeyState(VK_CONTROL) < 0;
  }

  /// The wxEVT_IDLE handling is running (wxApp::OnIdle, wxIsInOnIdleFlag).
  extern bool gInOnIdle;
} // namespace wxlite
