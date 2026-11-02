#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxTopLevelWindowBase
// (include/wx/toplevel.h) and the MSW port's wxTopLevelWindowMSW (include/wx/msw/toplevel.h),
// for what the engine uses. A top-level window is a popup window of the message layer; the
// Android host shows the one the engine renders to in the activity (src/host).

#include "wx/icon.h"
#include "wx/window.h"

extern const wxChar* wxFrameNameStr;

enum
{
  wxFULLSCREEN_NOMENUBAR = 0x0001,
  wxFULLSCREEN_NOTOOLBAR = 0x0002,
  wxFULLSCREEN_NOSTATUSBAR = 0x0004,
  wxFULLSCREEN_NOBORDER = 0x0008,
  wxFULLSCREEN_NOCAPTION = 0x0010,
  wxFULLSCREEN_ALL = wxFULLSCREEN_NOMENUBAR | wxFULLSCREEN_NOTOOLBAR | wxFULLSCREEN_NOSTATUSBAR
                   | wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION
};

class wxTopLevelWindowBase : public wxWindow
{
public:
  wxTopLevelWindowBase();
  ~wxTopLevelWindowBase() override;

  virtual void Maximize(bool maximize = true) = 0;
  virtual void Restore() = 0;
  virtual void Iconize(bool iconize = true) = 0;
  virtual bool IsMaximized() const = 0;
  virtual bool IsIconized() const = 0;

  const wxIcon& GetIcon() const
  {
    return m_icon;
  }
  virtual void SetIcon(const wxIcon& icon)
  {
    m_icon = icon;
  }

  virtual bool ShowFullScreen(bool show, long style = wxFULLSCREEN_ALL) = 0;
  virtual bool IsFullScreen() const = 0;

  /// Deleted in the next idle time (wxPendingDelete), not now: messages for it may be queued.
  bool Destroy() override;
  bool IsTopLevel() const override
  {
    return true;
  }
  wxSize GetMaxSize() const override;

  void OnCloseWindow(wxCloseEvent& event);
  /// Lays the only child over the client area, or lays out by the sizer with SetAutoLayout.
  void OnSize(wxSizeEvent& event);
  void OnActivate(wxActivateEvent& WXUNUSED(event)) {}

protected:
  void DoClientToScreen(int* x, int* y) const override;
  void DoScreenToClient(int* x, int* y) const override;

  /// True when this is the last top-level window and the application ends with it.
  bool IsLastBeforeExit() const;
  bool SendIconizeEvent(bool iconized = true);

  // wxWindows 2.4.2 keeps an icon bundle; the engine sets one icon
  wxIcon m_icon;

  DECLARE_EVENT_TABLE()
};

class wxTopLevelWindowMSW : public wxTopLevelWindowBase
{
public:
  wxTopLevelWindowMSW()
  {
    Init();
  }
  wxTopLevelWindowMSW(
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
    (void)Create(parent, id, title, pos, size, style, name);
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

  ~wxTopLevelWindowMSW() override;

  void Maximize(bool maximize = true) override;
  bool IsMaximized() const override;
  void Iconize(bool iconize = true) override;
  bool IsIconized() const override;
  void SetIcon(const wxIcon& icon) override;
  void Restore() override;

  bool Show(bool show = true) override;

  bool ShowFullScreen(bool show, long style = wxFULLSCREEN_ALL) override;
  bool IsFullScreen() const override
  {
    return m_fsIsShowing;
  }

  void OnActivate(wxActivateEvent& event);

  void SetLastFocus(wxWindow* win)
  {
    m_winLastFocused = win;
  }
  wxWindow* GetLastFocus() const
  {
    return m_winLastFocused;
  }

protected:
  void Init();

  bool CreateFrame(const wxString& title, const wxPoint& pos, const wxSize& size);

  /// ShowWindow(nShowCmd), noting the iconized state.
  void DoShowWindow(int nShowCmd);

  WXDWORD MSWGetStyle(long flags, WXDWORD* exstyle) const override;
  WXHWND MSWGetParent() const override;

  bool m_iconized;
  bool m_maximizeOnShow;

  // ShowFullScreen's saved state
  long m_fsStyle;
  wxRect m_fsOldSize;
  long m_fsOldWindowStyle;
  bool m_fsIsMaximized;
  bool m_fsIsShowing;

  wxWindow* m_winLastFocused;

  DECLARE_EVENT_TABLE()
};

class wxTopLevelWindow : public wxTopLevelWindowMSW
{
public:
  wxTopLevelWindow()
  {
    Init();
  }
  wxTopLevelWindow(
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
};

/// The windows that are not modal and not children (a top-level window is one when it shows).
extern wxWindowList wxModelessWindows;
