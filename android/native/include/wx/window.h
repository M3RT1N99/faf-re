#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxWindowBase (include/wx/window.h) and
// the MSW port's wxWindow (include/wx/msw/window.h), for what the engine uses. A wxWindow is a
// window of the Win32 message layer (include/win32/Windows.h): an HWND with wx-lite's window
// procedure, which hands each message to MSWWindowProc. MSWWindowProc turns the messages into wx
// events as wxWindowMSW does (src/msw/window.cpp); the engine's windows override it.

#include <vector>

#include "wx/colour.h"
#include "wx/event.h"
#include "wx/gdicmn.h"
#include "wx/string.h"
#include "wx/validate.h"

class wxSizer;
class wxWindow;
class wxWindowBase;

/// A window's children, in creation order (wxWindows 2.4.2 keeps a wxList; nothing in the engine
/// walks it).
using wxWindowList = std::vector<wxWindow*>;

/// The top-level windows, in creation order (wxTopLevelWindows).
extern wxWindowList wxTopLevelWindows;

extern const wxChar* wxPanelNameStr;

class wxWindowBase : public wxEvtHandler
{
public:
  wxWindowBase()
  {
    InitBase();
  }

  bool CreateBase(
    wxWindowBase* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxValidator& validator = wxDefaultValidator,
    const wxString& name = wxPanelNameStr
  );

  ~wxWindowBase() override;

  // deleting the window
  // -------------------

  /// Asks the window to close: a wxEVT_CLOSE_WINDOW event, which may veto unless `force`.
  bool Close(bool force = false);
  /// Deletes the window and its children; a top-level window is deleted in the next idle time.
  virtual bool Destroy();
  bool DestroyChildren();
  bool IsBeingDeleted() const
  {
    return m_isBeingDeleted;
  }

  // window attributes
  // -----------------

  virtual void SetTitle(const wxString& WXUNUSED(title)) {}
  virtual wxString GetTitle() const
  {
    return wxEmptyString;
  }
  virtual void SetLabel(const wxString& label)
  {
    SetTitle(label);
  }
  virtual wxString GetLabel() const
  {
    return GetTitle();
  }
  virtual void SetName(const wxString& name)
  {
    m_windowName = name;
  }
  virtual wxString GetName() const
  {
    return m_windowName;
  }
  void SetId(wxWindowID id)
  {
    m_windowId = id;
  }
  wxWindowID GetId() const
  {
    return m_windowId;
  }
  /// The ids of the windows created with id -1: -200, -201, ... (wxWindows 2.4.2).
  static int NewControlId()
  {
    return --ms_lastControlId;
  }

  // moving/resizing
  // ---------------

  void SetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO)
  {
    DoSetSize(x, y, width, height, sizeFlags);
  }
  void SetSize(int width, int height)
  {
    DoSetSize(-1, -1, width, height, wxSIZE_USE_EXISTING);
  }
  void SetSize(const wxSize& size)
  {
    SetSize(size.x, size.y);
  }
  void SetSize(const wxRect& rect, int sizeFlags = wxSIZE_AUTO)
  {
    DoSetSize(rect.x, rect.y, rect.width, rect.height, sizeFlags);
  }
  void Move(int x, int y, int flags = wxSIZE_USE_EXISTING)
  {
    DoSetSize(x, y, -1, -1, flags);
  }
  void Move(const wxPoint& pt, int flags = wxSIZE_USE_EXISTING)
  {
    Move(pt.x, pt.y, flags);
  }

  virtual void Raise() = 0;
  virtual void Lower() = 0;

  void SetClientSize(int width, int height)
  {
    DoSetClientSize(width, height);
  }
  void SetClientSize(const wxSize& size)
  {
    DoSetClientSize(size.x, size.y);
  }
  void SetClientSize(const wxRect& rect)
  {
    SetClientSize(rect.width, rect.height);
  }

  void GetPosition(int* x, int* y) const
  {
    DoGetPosition(x, y);
  }
  wxPoint GetPosition() const
  {
    int w, h;
    DoGetPosition(&w, &h);
    return wxPoint(w, h);
  }
  void GetSize(int* w, int* h) const
  {
    DoGetSize(w, h);
  }
  wxSize GetSize() const
  {
    int w, h;
    DoGetSize(&w, &h);
    return wxSize(w, h);
  }
  wxRect GetRect() const
  {
    int x, y, w, h;
    GetPosition(&x, &y);
    GetSize(&w, &h);
    return wxRect(x, y, w, h);
  }
  void GetClientSize(int* w, int* h) const
  {
    DoGetClientSize(w, h);
  }
  wxSize GetClientSize() const
  {
    int w, h;
    DoGetClientSize(&w, &h);
    return wxSize(w, h);
  }
  virtual wxPoint GetClientAreaOrigin() const;
  wxRect GetClientRect() const
  {
    return wxRect(GetClientAreaOrigin(), GetClientSize());
  }
  wxSize GetBestSize() const
  {
    return DoGetBestSize();
  }
  /// The best size, at least the minimum (SetSizeHints).
  wxSize GetAdjustedBestSize() const
  {
    const wxSize s(DoGetBestSize());
    return wxSize(s.x > GetMinWidth() ? s.x : GetMinWidth(), s.y > GetMinHeight() ? s.y : GetMinHeight());
  }

  void Centre(int direction = wxBOTH);
  void Center(int direction = wxBOTH)
  {
    Centre(direction);
  }
  void CentreOnScreen(int dir = wxBOTH)
  {
    Centre(dir | wxCENTER_ON_SCREEN);
  }
  void CentreOnParent(int dir = wxBOTH)
  {
    Centre(dir | wxCENTER_FRAME);
  }

  /// The window's size to fit its children (its sizer's minimum when it has one).
  virtual void Fit();

  virtual void SetSizeHints(int minW, int minH, int maxW = -1, int maxH = -1, int incW = -1, int incH = -1);
  virtual int GetMinWidth() const
  {
    return m_minWidth;
  }
  virtual int GetMinHeight() const
  {
    return m_minHeight;
  }
  int GetMaxWidth() const
  {
    return m_maxWidth;
  }
  int GetMaxHeight() const
  {
    return m_maxHeight;
  }
  virtual wxSize GetMaxSize() const
  {
    return wxSize(m_maxWidth, m_maxHeight);
  }

  // window state
  // ------------

  /// Shows or hides the window: true when that changed something.
  virtual bool Show(bool show = true);
  bool Hide()
  {
    return Show(false);
  }
  virtual bool Enable(bool enable = true);
  bool Disable()
  {
    return Enable(false);
  }
  bool IsShown() const
  {
    return m_isShown;
  }
  bool IsEnabled() const
  {
    return m_isEnabled;
  }

  virtual void SetWindowStyleFlag(long style)
  {
    m_windowStyle = style;
  }
  virtual long GetWindowStyleFlag() const
  {
    return m_windowStyle;
  }
  void SetWindowStyle(long style)
  {
    SetWindowStyleFlag(style);
  }
  long GetWindowStyle() const
  {
    return GetWindowStyleFlag();
  }
  bool HasFlag(int flag) const
  {
    return (m_windowStyle & flag) != 0;
  }
  virtual void SetExtraStyle(long exStyle)
  {
    m_exStyle = exStyle;
  }
  long GetExtraStyle() const
  {
    return m_exStyle;
  }

  // focus and keyboard handling
  // ---------------------------

  virtual void SetFocus() = 0;
  virtual void SetFocusFromKbd()
  {
    SetFocus();
  }
  /// The window with the keyboard focus, if it is a wx window.
  static wxWindow* FindFocus();
  virtual bool AcceptsFocus() const
  {
    return IsShown() && IsEnabled();
  }

  // parent/children relations
  // -------------------------

  const wxWindowList& GetChildren() const
  {
    return m_children;
  }
  wxWindowList& GetChildren()
  {
    return m_children;
  }
  wxWindow* GetParent() const
  {
    return m_parent;
  }
  inline wxWindow* GetGrandParent() const;
  virtual bool IsTopLevel() const;
  void SetParent(wxWindowBase* parent);
  virtual bool Reparent(wxWindowBase* newParent);
  virtual void AddChild(wxWindowBase* child);
  virtual void RemoveChild(wxWindowBase* child);

  wxWindow* FindWindow(long id);
  wxWindow* FindWindow(const wxString& name);
  static wxWindow* FindWindowById(long id, const wxWindow* parent = nullptr);
  static wxWindow* FindWindowByName(const wxString& name, const wxWindow* parent = nullptr);

  // event handler stuff
  // -------------------

  /// The handler the window's events go to: the window itself, or the last one pushed.
  wxEvtHandler* GetEventHandler() const
  {
    return m_eventHandler;
  }
  void SetEventHandler(wxEvtHandler* handler)
  {
    m_eventHandler = handler;
  }
  /// Puts `handler` in front of the window's handlers: it sees each event first.
  void PushEventHandler(wxEvtHandler* handler);
  /// Takes the front handler off again (deleting it if `deleteHandler`) and returns it, or null.
  wxEvtHandler* PopEventHandler(bool deleteHandler = false);
  bool RemoveEventHandler(wxEvtHandler* handler);

  // validators
  // ----------

  virtual void SetValidator(const wxValidator& validator);
  virtual wxValidator* GetValidator()
  {
    return m_windowValidator;
  }
  virtual bool Validate();
  virtual bool TransferDataToWindow();
  virtual bool TransferDataFromWindow();
  virtual void InitDialog();

  // mouse functions
  // ---------------

  virtual void WarpPointer(int x, int y) = 0;
  void CaptureMouse();
  void ReleaseMouse();
  static wxWindow* GetCapture();
  virtual bool HasCapture() const;

  // painting the window
  // -------------------

  /// Marks the window (or `rect` of it) for a WM_PAINT, erasing the background first if asked.
  virtual void Refresh(bool eraseBackground = true, const wxRect* rect = nullptr) = 0;
  void RefreshRect(const wxRect& rect)
  {
    Refresh(true, &rect);
  }
  virtual void Update() {}
  virtual void Clear() = 0;
  virtual void Freeze() {}
  virtual void Thaw() {}

  virtual bool SetBackgroundColour(const wxColour& colour);
  virtual bool SetForegroundColour(const wxColour& colour);
  wxColour GetBackgroundColour() const
  {
    return m_backgroundColour;
  }
  wxColour GetForegroundColour() const
  {
    return m_foregroundColour;
  }

  // the text metrics of the window's font
  virtual int GetCharHeight() const = 0;
  virtual int GetCharWidth() const = 0;

  // client <-> screen coords
  // ------------------------

  void ClientToScreen(int* x, int* y) const
  {
    DoClientToScreen(x, y);
  }
  void ScreenToClient(int* x, int* y) const
  {
    DoScreenToClient(x, y);
  }
  wxPoint ClientToScreen(const wxPoint& pt) const
  {
    int x = pt.x, y = pt.y;
    DoClientToScreen(&x, &y);
    return wxPoint(x, y);
  }
  wxPoint ScreenToClient(const wxPoint& pt) const
  {
    int x = pt.x, y = pt.y;
    DoScreenToClient(&x, &y);
    return wxPoint(x, y);
  }

  // scrollbars
  // ----------

  virtual bool ScrollLines(int WXUNUSED(lines))
  {
    return false;
  }
  virtual bool ScrollPages(int WXUNUSED(pages))
  {
    return false;
  }

  // sizers
  // ------

  void SetAutoLayout(bool autoLayout)
  {
    m_autoLayout = autoLayout;
  }
  bool GetAutoLayout() const
  {
    return m_autoLayout;
  }
  /// Lays the children out by the window's sizer, if it has one.
  virtual bool Layout();
  /// The window's sizer, which it owns from here on (the old one is deleted if `deleteOld`).
  void SetSizer(wxSizer* sizer, bool deleteOld = true);
  void SetSizerAndFit(wxSizer* sizer, bool deleteOld = true);
  wxSizer* GetSizer() const
  {
    return m_windowSizer;
  }
  void SetContainingSizer(wxSizer* sizer)
  {
    m_containingSizer = sizer;
  }
  wxSizer* GetContainingSizer() const
  {
    return m_containingSizer;
  }

  // native window
  // -------------

  virtual WXWidget GetHandle() const = 0;

  // event handlers of the base class
  // --------------------------------

  void OnInitDialog(wxInitDialogEvent& event);

protected:
  wxWindowID m_windowId;
  wxWindow* m_parent;
  wxWindowList m_children;

  // constraints on the window's size
  int m_minWidth, m_minHeight, m_maxWidth, m_maxHeight;

  // the window's event handler (the window itself unless handlers were pushed)
  wxEvtHandler* m_eventHandler;

  // associated validator (owned by the window)
  wxValidator* m_windowValidator;

  wxColour m_backgroundColour, m_foregroundColour;

  // the window's sizer, and the sizer it is in
  wxSizer* m_windowSizer;
  wxSizer* m_containingSizer;

  bool m_autoLayout : 1;
  bool m_isShown : 1;
  bool m_isEnabled : 1;
  bool m_isBeingDeleted : 1;
  bool m_hasBgCol : 1;
  bool m_hasFgCol : 1;

  long m_windowStyle, m_exStyle;
  wxString m_windowName;

  void InitBase();

  static int WidthDefault(int w)
  {
    return w == -1 ? 20 : w;
  }
  static int HeightDefault(int h)
  {
    return h == -1 ? 20 : h;
  }

  virtual void DoClientToScreen(int* x, int* y) const = 0;
  virtual void DoScreenToClient(int* x, int* y) const = 0;
  virtual void DoCaptureMouse() = 0;
  virtual void DoReleaseMouse() = 0;
  virtual void DoGetPosition(int* x, int* y) const = 0;
  virtual void DoGetSize(int* width, int* height) const = 0;
  virtual void DoGetClientSize(int* width, int* height) const = 0;
  virtual wxSize DoGetBestSize() const;
  virtual void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) = 0;
  virtual void DoSetClientSize(int width, int height) = 0;
  virtual void DoMoveWindow(int x, int y, int width, int height) = 0;

private:
  static int ms_lastControlId;

  DECLARE_NO_COPY_CLASS(wxWindowBase)
  DECLARE_EVENT_TABLE()
};

enum
{
  wxKEY_SHIFT = 1,
  wxKEY_CTRL = 2
};

class wxWindow : public wxWindowBase
{
public:
  wxWindow()
  {
    Init();
  }
  wxWindow(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxPanelNameStr
  )
  {
    Init();
    Create(parent, id, pos, size, style, name);
  }

  ~wxWindow() override;

  bool Create(
    wxWindow* parent,
    wxWindowID id,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    long style = 0,
    const wxString& name = wxPanelNameStr
  );

  // implement base class pure virtuals
  void SetTitle(const wxString& title) override;
  wxString GetTitle() const override;

  void Raise() override;
  void Lower() override;

  bool Show(bool show = true) override;
  bool Enable(bool enable = true) override;

  void SetFocus() override;
  void SetFocusFromKbd() override;

  bool Reparent(wxWindowBase* newParent) override;

  void WarpPointer(int x, int y) override;

  void Refresh(bool eraseBackground = true, const wxRect* rect = nullptr) override;
  void Update() override;
  void Clear() override;

  void SetWindowStyleFlag(long style) override;

  /// The default GUI font's metrics (wx-lite has no fonts: src/wx/Private.h).
  int GetCharHeight() const override;
  int GetCharWidth() const override;

  bool ScrollLines(int lines) override;
  bool ScrollPages(int pages) override;

  // the native window: the HWND of the message layer
  WXHWND GetHWND() const
  {
    return m_hWnd;
  }
  void SetHWND(WXHWND hWnd)
  {
    m_hWnd = hWnd;
  }
  WXWidget GetHandle() const override
  {
    return GetHWND();
  }

  // event handlers
  void OnEraseBackground(wxEraseEvent& event);
  void OnIdle(wxIdleEvent& event);
  void OnPaint(wxPaintEvent& event);

public:
  // the window class's window procedure finds the window by its HWND
  void SubclassWin(WXHWND hWnd);
  void UnsubclassWin();

  wxWindow* FindItemByHWND(WXHWND hWnd, bool controlOnly = false) const;

  /// The Win32 style bits for the wx `flags` (WS_ and WS_EX_), as wxWindowMSW::MSWGetStyle.
  virtual WXDWORD MSWGetStyle(long flags, WXDWORD* exstyle = nullptr) const;
  WXDWORD MSWGetCreateWindowFlags(WXDWORD* exflags = nullptr) const
  {
    return MSWGetStyle(GetWindowStyle(), exflags);
  }
  virtual WXHWND MSWGetParent() const;

  /// Creates the native window of class `wclass` (CreateWindowExW).
  bool MSWCreate(
    const wxChar* wclass,
    const wxChar* title = nullptr,
    const wxPoint& pos = wxDefaultPosition,
    const wxSize& size = wxDefaultSize,
    WXDWORD style = 0,
    WXDWORD exendedStyle = 0
  );

  // the message handlers, as wxWindowMSW has them: true when the message was processed
  bool HandleCreate(void* cs, bool* mayCreate);
  bool HandleDestroy();
  bool HandlePaint();
  bool HandleEraseBkgnd(WXHDC pDC);
  bool HandleMinimize();
  bool HandleMaximize();
  bool HandleSize(int x, int y, WXUINT flag);
  bool HandleShow(bool show, int status);
  bool HandleActivate(int flag, bool minimized, WXHWND activate);
  bool HandleCommand(WXWORD id, WXWORD cmd, WXHWND control);
  bool HandleSetFocus(WXHWND wnd);
  bool HandleKillFocus(WXHWND wnd);
  bool HandleMouseEvent(WXUINT msg, int x, int y, WXUINT flags);
  bool HandleMouseMove(int x, int y, WXUINT flags);
  bool HandleMouseWheel(WXWPARAM wParam, WXLPARAM lParam);
  bool HandleChar(WXWPARAM wParam, WXLPARAM lParam, bool isASCII = false);
  bool HandleKeyDown(WXWPARAM wParam, WXLPARAM lParam);
  bool HandleKeyUp(WXWPARAM wParam, WXLPARAM lParam);
  bool HandleSetCursor(WXHWND hWnd, short nHitTest, int mouseMsg);

  /// The window procedure: turns `nMsg` into wx events, or hands it to MSWDefWindowProc.
  virtual long MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam);
  /// What the window does with a message nothing handled: DefWindowProcW.
  virtual long MSWDefWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam);

  // the hooks wxApp::ProcessMessage gives each window before a message is dispatched
  virtual bool MSWShouldPreProcessMessage(WXMSG* pMsg);
  virtual bool MSWProcessMessage(WXMSG* pMsg);
  virtual bool MSWTranslateMessage(WXMSG* pMsg);
  virtual void MSWDestroyWindow();

  void InitMouseEvent(wxMouseEvent& event, int x, int y, WXUINT flags);
  bool IsMouseInWindow() const;

protected:
  // the window handle
  WXHWND m_hWnd;

  bool m_mouseInWindow : 1;
  bool m_lastKeydownProcessed : 1;

  void DoClientToScreen(int* x, int* y) const override;
  void DoScreenToClient(int* x, int* y) const override;
  void DoGetPosition(int* x, int* y) const override;
  void DoGetSize(int* width, int* height) const override;
  void DoGetClientSize(int* width, int* height) const override;
  void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override;
  void DoSetClientSize(int width, int height) override;
  void DoCaptureMouse() override;
  void DoReleaseMouse() override;
  void DoMoveWindow(int x, int y, int width, int height) override;

  wxKeyEvent CreateKeyEvent(wxEventType evType, int id, WXLPARAM lParam = 0, WXWPARAM wParam = 0) const;

private:
  void Init();
  bool HandleMove(int x, int y);

  // the (non-top-level) children of this window which were disabled when it was disabled
  // itself, and stay so when it is enabled again
  wxWindowList* m_childrenDisabled;

  DECLARE_NO_COPY_CLASS(wxWindow)
  DECLARE_EVENT_TABLE()
};

inline wxWindow* wxWindowBase::GetGrandParent() const
{
  return m_parent ? m_parent->GetParent() : nullptr;
}

/// The wx window of `hWnd`, if it is one.
wxWindow* wxFindWinFromHandle(WXHWND hWnd);
wxWindow* wxGetWindowFromHWND(WXHWND hwnd);

/// The wx key code for the Windows virtual key `keySym`, or 0 for a key that is a character.
int wxCharCodeMSWToWX(int keySym);
/// The Windows virtual key for the wx key code `id`; `*IsVirtual` is false when it is a character.
int wxCharCodeWXToMSW(int id, bool* IsVirtual);

/// The window under the pointer, and the pointer's position.
wxWindow* wxFindWindowAtPointer(wxPoint& pt);
