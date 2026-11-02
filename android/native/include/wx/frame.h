#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxFrameBase (include/wx/frame.h) and the
// MSW port's wxFrame (include/wx/msw/frame.h), for what the engine uses: a top-level window
// without menu, tool or status bar (the engine's frames have none).

#include "wx/toplevel.h"

class wxFrameBase : public wxTopLevelWindow
{
public:
  wxFrameBase();
  ~wxFrameBase() override;

  /// The origin of the area the children may use: (0, 0) without tool bar.
  wxPoint GetClientAreaOrigin() const override;
};

class wxFrame : public wxFrameBase
{
public:
  wxFrame()
  {
    Init();
  }
  wxFrame(
    wxWindow* parent,
    wxWindowID id,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_FRAME_STYLE,
    const wxString& name = wxFrameNameStr
  )
  {
    Init();
    Create(parent, id, title, pos, size, style, name);
  }

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxString& title,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = wxDEFAULT_FRAME_STYLE,
    const wxString& name = wxFrameNameStr
  );

  ~wxFrame() override;

  void Raise() override;

  /// WM_CLOSE, WM_SIZE, WM_COMMAND and WM_PAINT, as wxFrame::MSWWindowProc; the rest as any window.
  long MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;

  bool HandleSize(int x, int y, WXUINT flag);
  bool HandleCommand(WXWORD id, WXWORD cmd, WXHWND control);
  bool HandlePaint();

protected:
  void Init();

  void DoGetClientSize(int* width, int* height) const override;
  void DoSetClientSize(int width, int height) override;

  DECLARE_EVENT_TABLE()
};
