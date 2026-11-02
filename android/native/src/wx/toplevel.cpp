// wx-lite (android/native/WX_LITE.md): the top-level windows of wxWindows 2.4.2:
// wxTopLevelWindowBase (src/common/toplvcmn.cpp), wxTopLevelWindowMSW (src/msw/toplevel.cpp),
// wxFrame (src/msw/frame.cpp), wxDialog (src/msw/dialog.cpp) and wxSplashScreen
// (src/generic/splash.cpp), as FAF's build of it has them: a dialog is made as a frame is.

#include <Windows.h>

#include <algorithm>

#include "Private.h"
#include "wx/app.h"
#include "wx/dcclient.h"
#include "wx/dialog.h"
#include "wx/frame.h"
#include "wx/splash.h"
#include "wx/toplevel.h"

wxWindowList wxTopLevelWindows;
wxWindowList wxModelessWindows;

namespace
{
  /// The dialogs being shown modally (wxModalDialogs).
  std::vector<wxDialog*>& ModalDialogs()
  {
    static std::vector<wxDialog*>* const dialogs = new std::vector<wxDialog*>;
    return *dialogs;
  }

  void Erase(wxWindowList& list, wxWindow* const window)
  {
    list.erase(std::remove(list.begin(), list.end(), window), list.end());
  }

  // the extra style of a dialog (wxTOPLEVEL_EX_DIALOG)
  constexpr long kTopLevelExDialog = 0x00000008;
} // namespace

// --- wxTopLevelWindowBase -------------------------------------------------------------------------

BEGIN_EVENT_TABLE(wxTopLevelWindowBase, wxWindow)
  EVT_CLOSE(wxTopLevelWindowBase::OnCloseWindow)
  EVT_SIZE(wxTopLevelWindowBase::OnSize)
END_EVENT_TABLE()

wxTopLevelWindowBase::wxTopLevelWindowBase() {}

wxTopLevelWindowBase::~wxTopLevelWindowBase()
{
  // don't let wxTheApp keep any stale pointers to us
  if (wxTheApp && wxTheApp->GetTopWindow() == this) {
    wxTheApp->SetTopWindow(nullptr);
  }

  const bool shouldExit = IsLastBeforeExit();

  Erase(wxTopLevelWindows, this);

  if (shouldExit) {
    // then do it
    wxTheApp->ExitMainLoop();
  }
}

bool wxTopLevelWindowBase::Destroy()
{
  // delayed destruction: the frame will be deleted during the next idle loop iteration
  if (std::find(wxPendingDelete.begin(), wxPendingDelete.end(), this) == wxPendingDelete.end()) {
    wxPendingDelete.push_back(this);
  }
  return true;
}

bool wxTopLevelWindowBase::IsLastBeforeExit() const
{
  // we exit the application if there are no more top level windows and wxApp::SetExitOnFrameDelete()
  // had been called with TRUE
  return wxTopLevelWindows.size() == 1 && wxTopLevelWindows.front() == static_cast<const wxWindow*>(this)
      && wxTheApp && wxTheApp->GetExitOnFrameDelete();
}

wxSize wxTopLevelWindowBase::GetMaxSize() const
{
  wxSize size(GetMaxWidth(), GetMaxHeight());
  const int w = ::GetSystemMetrics(SM_CXSCREEN);
  const int h = ::GetSystemMetrics(SM_CYSCREEN);
  if (size.GetWidth() == -1) {
    size.SetWidth(w);
  }
  if (size.GetHeight() == -1) {
    size.SetHeight(h);
  }
  return size;
}

void wxTopLevelWindowBase::DoScreenToClient(int* const x, int* const y) const
{
  wxWindow::DoScreenToClient(x, y);

  // translate the wxWindow client coords to our client coords
  const wxPoint pt(GetClientAreaOrigin());
  if (x) {
    *x -= pt.x;
  }
  if (y) {
    *y -= pt.y;
  }
}

void wxTopLevelWindowBase::DoClientToScreen(int* const x, int* const y) const
{
  // our client area origin (0, 0) may be really something like (0, 30) for wxWindow if we have a
  // toolbar, account for it before translating
  const wxPoint pt(GetClientAreaOrigin());
  if (x) {
    *x += pt.x;
  }
  if (y) {
    *y += pt.y;
  }
  wxWindow::DoClientToScreen(x, y);
}

// default resizing behaviour - if only ONE subwindow, resize to fill the whole client area
void wxTopLevelWindowBase::OnSize(wxSizeEvent& WXUNUSED(event))
{
  // if we're using constraints or sizers - do use them
  if (GetAutoLayout()) {
    Layout();
    return;
  }

  // do we have _exactly_ one child?
  wxWindow* child = nullptr;
  for (wxWindow* const win : GetChildren()) {
    // exclude top level and managed windows (status bar isn't currently in the children list
    // except under wxMac anyhow, but it makes no harm to test for it)
    if (!win->IsTopLevel()) {
      if (child) {
        return; // it's our second subwindow - nothing to do
      }
      child = win;
    }
  }

  // do we have any children at all?
  if (child) {
    // exactly one child - set it's size to fill the whole frame
    int clientW, clientH;
    DoGetClientSize(&clientW, &clientH);
    child->SetSize(0, 0, clientW, clientH);
  }
}

// The default implementation for the close window event.
void wxTopLevelWindowBase::OnCloseWindow(wxCloseEvent& WXUNUSED(event))
{
  Destroy();
}

bool wxTopLevelWindowBase::SendIconizeEvent(const bool iconized)
{
  wxIconizeEvent event(GetId(), iconized);
  event.SetEventObject(this);
  return GetEventHandler()->ProcessEvent(event);
}

// --- wxTopLevelWindowMSW ----------------------------------------------------------------------------

BEGIN_EVENT_TABLE(wxTopLevelWindowMSW, wxTopLevelWindowBase)
  EVT_ACTIVATE(wxTopLevelWindowMSW::OnActivate)
END_EVENT_TABLE()

void wxTopLevelWindowMSW::Init()
{
  m_iconized = m_maximizeOnShow = false;

  // unlike (almost?) all other windows, frames are created hidden
  m_isShown = false;

  // Data to save/restore when calling ShowFullScreen
  m_fsStyle = 0;
  m_fsOldWindowStyle = 0;
  m_fsIsMaximized = false;
  m_fsIsShowing = false;

  m_winLastFocused = nullptr;
}

WXDWORD wxTopLevelWindowMSW::MSWGetStyle(const long style, WXDWORD* const exflags) const
{
  // let the base class deal with the common styles but fix the ones which don't make sense for
  // us (we also deal with the borders ourselves)
  WXDWORD msflags = wxWindow::MSWGetStyle((style & ~wxBORDER_MASK) | wxBORDER_NONE, exflags)
                  & ~static_cast<WXDWORD>(WS_CHILD) & ~static_cast<WXDWORD>(WS_VISIBLE);

  // first select the kind of window being created: tool windows are popups, the others overlapped
  if (style & wxFRAME_TOOL_WINDOW) {
    msflags |= WS_POPUP;
  } else {
    msflags |= WS_OVERLAPPED;
  }

  // border and caption styles
  if (style & wxRESIZE_BORDER) {
    msflags |= WS_THICKFRAME;
  } else if (exflags && ((style & wxBORDER_DOUBLE) || (style & wxBORDER_RAISED))) {
    *exflags |= WS_EX_DLGMODALFRAME;
  } else if (!(style & wxBORDER_NONE)) {
    msflags |= WS_BORDER;
  } else {
    msflags |= WS_POPUP;
  }

  if (style & wxCAPTION) {
    msflags |= WS_CAPTION;
  } else {
    msflags |= WS_POPUP;
  }

  // next translate the individual flags
  if (style & wxMINIMIZE_BOX) {
    msflags |= WS_MINIMIZEBOX;
  }
  if (style & wxMAXIMIZE_BOX) {
    msflags |= WS_MAXIMIZEBOX;
  }
  if (style & wxSYSTEM_MENU) {
    msflags |= WS_SYSMENU;
  }
  if (style & wxMINIMIZE) {
    msflags |= WS_MINIMIZE;
  }
  if (style & wxMAXIMIZE) {
    msflags |= WS_MAXIMIZE;
  }

  // Keep this here because it saves recoding this function in wxTinyFrame
  if (style & (wxTINY_CAPTION_VERT | wxTINY_CAPTION_HORIZ)) {
    msflags |= WS_CAPTION;
  }

  if (exflags) {
    // there is no taskbar on Android, so wxFRAME_NO_TASKBAR and WS_EX_APPWINDOW change nothing
    if (!(GetExtraStyle() & kTopLevelExDialog)) {
      if (style & wxFRAME_TOOL_WINDOW) {
        *exflags |= WS_EX_TOOLWINDOW;
      }
    }
    if (style & wxSTAY_ON_TOP) {
      *exflags |= WS_EX_TOPMOST;
    }
  }
  return msflags;
}

WXHWND wxTopLevelWindowMSW::MSWGetParent() const
{
  // for the frames without wxFRAME_FLOAT_ON_PARENT style we should use NULL parent HWND or it
  // would be always on top of its parent which is not what we usually want (in fact, we only
  // want it for frames with the wxFRAME_FLOAT_ON_PARENT flag)
  WXHWND hwndParent = 0;
  if (HasFlag(wxFRAME_FLOAT_ON_PARENT)) {
    const wxWindow* const parent = GetParent();
    if (parent) {
      hwndParent = parent->GetHWND();
    }
  }
  // wxFRAME_NO_TASKBAR: wxWindows 2.4.2 parents such a frame to a hidden window, to keep it off
  // the task bar. There is no task bar here.
  return hwndParent;
}

bool wxTopLevelWindowMSW::CreateFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
{
  WXDWORD exflags;
  const WXDWORD flags = MSWGetCreateWindowFlags(&exflags);
  return MSWCreate(wxlite::kCanvasClassName, title.c_str(), pos, size, flags, exflags);
}

bool wxTopLevelWindowMSW::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxString& title,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxString& name
)
{
  // init our fields
  Init();

  m_windowStyle = style;

  SetName(name);

  m_windowId = id == -1 ? NewControlId() : id;

  wxTopLevelWindows.push_back(this);

  if (parent) {
    parent->AddChild(this);
  }

  // A dialog is made as a frame, as FAF's wxWindows makes it (CreateDialog creates the frame).
  return CreateFrame(title, pos, size);
}

wxTopLevelWindowMSW::~wxTopLevelWindowMSW()
{
  Erase(wxModelessWindows, this);

  // after destroying an owned window, Windows activates the next top level window in Z order but
  // it may be different from our owner (to reproduce this simply Alt-TAB to another application
  // and back before closing the owned frame) whereas we always want to yield activation to our
  // parent
  if (HasFlag(wxFRAME_FLOAT_ON_PARENT)) {
    wxWindow* const parent = GetParent();
    if (parent) {
      ::BringWindowToTop(wxlite::GetHwndOf(parent));
    }
  }
}

void wxTopLevelWindowMSW::DoShowWindow(const int nShowCmd)
{
  ::ShowWindow(wxlite::ToHwnd(GetHWND()), nShowCmd);
  m_iconized = nShowCmd == SW_MINIMIZE;
}

bool wxTopLevelWindowMSW::Show(const bool show)
{
  // don't use wxWindow version as we want to call DoShowWindow() ourselves
  if (!wxWindowBase::Show(show)) {
    return false;
  }

  int nShowCmd;
  if (show) {
    if (m_maximizeOnShow) {
      // show and maximize
      nShowCmd = SW_MAXIMIZE;
      m_maximizeOnShow = false;
    } else { // just show
      nShowCmd = SW_SHOW;
    }
  } else { // hide
    nShowCmd = SW_HIDE;
  }

  DoShowWindow(nShowCmd);

  if (show) {
    ::BringWindowToTop(wxlite::ToHwnd(GetHWND()));

    wxActivateEvent event(wxEVT_ACTIVATE, true, m_windowId);
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
  } else { // hide
    // Try to highlight the correct window (the parent)
    if (GetParent()) {
      const HWND hWndParent = wxlite::GetHwndOf(GetParent());
      if (hWndParent) {
        ::BringWindowToTop(hWndParent);
      }
    }
  }
  return true;
}

void wxTopLevelWindowMSW::Maximize(const bool maximize)
{
  if (IsShown()) {
    // just maximize it directly
    DoShowWindow(maximize ? SW_MAXIMIZE : SW_RESTORE);
  } else { // hidden
    // we can't maximize the hidden frame because it shows it as well, so just remember that we
    // should do it later in this case
    m_maximizeOnShow = maximize;
  }
}

bool wxTopLevelWindowMSW::IsMaximized() const
{
  return ::IsZoomed(wxlite::ToHwnd(GetHWND())) != 0;
}

void wxTopLevelWindowMSW::Iconize(const bool iconize)
{
  DoShowWindow(iconize ? SW_MINIMIZE : SW_RESTORE);
}

bool wxTopLevelWindowMSW::IsIconized() const
{
  // also update the current state
  const_cast<wxTopLevelWindowMSW*>(this)->m_iconized = ::IsIconic(wxlite::ToHwnd(GetHWND())) != 0;
  return m_iconized;
}

void wxTopLevelWindowMSW::Restore()
{
  DoShowWindow(SW_RESTORE);
}

bool wxTopLevelWindowMSW::ShowFullScreen(const bool show, const long style)
{
  const HWND hWnd = wxlite::ToHwnd(GetHWND());
  if (show) {
    if (IsFullScreen()) {
      return false;
    }

    m_fsIsShowing = true;
    m_fsStyle = style;

    // zap the frame borders

    // save the 'normal' window style
    m_fsOldWindowStyle = ::GetWindowLongW(hWnd, GWL_STYLE);

    // save the old position, width & height, maximize state
    m_fsOldSize = GetRect();
    m_fsIsMaximized = IsMaximized();

    // decide which window style flags to turn off
    LONG newStyle = static_cast<LONG>(m_fsOldWindowStyle);
    LONG offFlags = 0;

    if (style & wxFULLSCREEN_NOBORDER) {
      offFlags |= WS_BORDER | WS_THICKFRAME;
    }
    if (style & wxFULLSCREEN_NOCAPTION) {
      offFlags |= (WS_CAPTION | WS_SYSMENU);
    }
    newStyle &= ~offFlags;

    // change our window style to be compatible with full-screen mode
    ::SetWindowLongW(hWnd, GWL_STYLE, newStyle);

    // resize to the size of the desktop
    const int width = ::GetSystemMetrics(SM_CXSCREEN);
    const int height = ::GetSystemMetrics(SM_CYSCREEN);

    SetSize(width, height);

    // now flush the window style cache and actually go full-screen
    ::SetWindowPos(hWnd, HWND_TOP, 0, 0, width, height, SWP_FRAMECHANGED);

    wxSizeEvent event(wxSize(width, height), GetId());
    GetEventHandler()->ProcessEvent(event);
    return true;
  }

  if (!IsFullScreen()) {
    return false;
  }

  m_fsIsShowing = false;

  Maximize(m_fsIsMaximized);
  ::SetWindowLongW(hWnd, GWL_STYLE, static_cast<LONG>(m_fsOldWindowStyle));
  ::SetWindowPos(hWnd, HWND_TOP, m_fsOldSize.x, m_fsOldSize.y, m_fsOldSize.width, m_fsOldSize.height, SWP_FRAMECHANGED);
  return true;
}

void wxTopLevelWindowMSW::SetIcon(const wxIcon& icon)
{
  // the window keeps it; Android shows no window icons, so no WM_SETICON follows
  wxTopLevelWindowBase::SetIcon(icon);
}

void wxTopLevelWindowMSW::OnActivate(wxActivateEvent& event)
{
  if (event.GetActive()) {
    // restore focus to the child which was last focused
    wxWindow* parent = m_winLastFocused ? m_winLastFocused->GetParent() : nullptr;
    if (!parent) {
      parent = this;
    }

    // wxSetFocusToChild
    if (m_winLastFocused && m_winLastFocused->GetParent() == parent) {
      m_winLastFocused->SetFocus();
    } else {
      m_winLastFocused = nullptr;
      for (wxWindow* const child : parent->GetChildren()) {
        if (child->AcceptsFocus() && !child->IsTopLevel()) {
          child->SetFocusFromKbd();
          break;
        }
      }
    }
  } else { // deactivating
    // remember the last focused child if it is our child
    m_winLastFocused = FindFocus();

    // so we NULL it out if it's a child from some other frame
    wxWindow* win = m_winLastFocused;
    while (win) {
      if (win->IsTopLevel()) {
        if (win != this) {
          m_winLastFocused = nullptr;
        }
        break;
      }
      win = win->GetParent();
    }

    event.Skip();
  }
}

// --- wxFrame --------------------------------------------------------------------------------------

wxFrameBase::wxFrameBase() {}

wxFrameBase::~wxFrameBase() {}

wxPoint wxFrameBase::GetClientAreaOrigin() const
{
  // no tool bar
  return wxPoint(0, 0);
}

BEGIN_EVENT_TABLE(wxFrame, wxFrameBase)
END_EVENT_TABLE()

void wxFrame::Init() {}

bool wxFrame::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxString& title,
  const wxPoint& pos,
  const wxSize& size,
  const long style,
  const wxString& name
)
{
  if (!wxTopLevelWindow::Create(parent, id, title, pos, size, style, name)) {
    return false;
  }
  // the application workspace's colour, as wxSystemSettings gives it on Windows
  SetBackgroundColour(wxColour(0x80, 0x80, 0x80));
  wxModelessWindows.push_back(this);
  return true;
}

wxFrame::~wxFrame()
{
  m_isBeingDeleted = true;
}

void wxFrame::DoSetClientSize(const int width, const int height)
{
  // no status bar and no tool bar to make room for
  const wxPoint pt = GetClientAreaOrigin();
  wxTopLevelWindow::DoSetClientSize(width + pt.x, height + pt.y);
}

void wxFrame::DoGetClientSize(int* const x, int* const y) const
{
  wxTopLevelWindow::DoGetClientSize(x, y);

  // account for the possible toolbar
  const wxPoint pt = GetClientAreaOrigin();
  if (x) {
    *x -= pt.x;
  }
  if (y) {
    *y -= pt.y;
  }
}

void wxFrame::Raise()
{
  wxFrameBase::Raise();
  ::SetForegroundWindow(wxlite::ToHwnd(GetHWND()));
}

bool wxFrame::HandlePaint()
{
  RECT rect;
  if (::GetUpdateRect(wxlite::ToHwnd(GetHWND()), &rect, FALSE)) {
    if (m_iconized) {
      // an iconized frame draws its icon; nothing is drawn here
      PAINTSTRUCT ps;
      (void)::BeginPaint(wxlite::ToHwnd(GetHWND()), &ps);
      ::EndPaint(wxlite::ToHwnd(GetHWND()), &ps);
      return true;
    }
    return wxWindow::HandlePaint();
  }
  // nothing to paint - processed
  return true;
}

bool wxFrame::HandleSize(const int x, const int y, const WXUINT id)
{
  bool processed = false;

  switch (id) {
  case SIZE_RESTORED:
    // only do it it if we were iconized before, otherwise resizing the parent frame has a
    // curious side effect of bringing it under it's children
    if (!m_iconized) {
      break;
    }
    // restore all child frames too
    (void)SendIconizeEvent(false);
    // fall through
    [[fallthrough]];
  case SIZE_MAXIMIZED:
    m_iconized = false;
    break;

  case SIZE_MINIMIZED:
    (void)SendIconizeEvent();
    m_iconized = true;
    break;

  default:
    break;
  }

  if (!m_iconized) {
    processed = wxWindow::HandleSize(x, y, id);
  }
  return processed;
}

bool wxFrame::HandleCommand(const WXWORD id, const WXWORD cmd, const WXHWND control)
{
  return wxWindow::HandleCommand(id, cmd, control);
}

long wxFrame::MSWWindowProc(const WXUINT message, const WXWPARAM wParam, const WXLPARAM lParam)
{
  long rc = 0;
  bool processed = false;

  switch (message) {
  case WM_CLOSE:
    // if we can't close, tell the system that we processed the message - otherwise it would
    // close us
    processed = !Close();
    break;

  case WM_SIZE:
    processed = HandleSize(LOWORD(lParam), HIWORD(lParam), static_cast<WXUINT>(wParam));
    break;

  case WM_COMMAND:
    processed = HandleCommand(LOWORD(wParam), HIWORD(wParam), static_cast<WXHWND>(lParam));
    break;

  case WM_PAINT:
    processed = HandlePaint();
    break;

  default:
    break;
  }

  if (!processed) {
    rc = wxFrameBase::MSWWindowProc(message, wParam, lParam);
  }
  return rc;
}

// --- wxDialog -------------------------------------------------------------------------------------

void wxDialogBase::Init()
{
  m_returnCode = 0;
}

BEGIN_EVENT_TABLE(wxDialog, wxDialogBase)
  EVT_BUTTON(wxID_OK, wxDialog::OnOK)
  EVT_BUTTON(wxID_APPLY, wxDialog::OnApply)
  EVT_BUTTON(wxID_CANCEL, wxDialog::OnCancel)
  EVT_CHAR_HOOK(wxDialog::OnCharHook)
  EVT_CLOSE(wxDialog::OnCloseWindow)
END_EVENT_TABLE()

void wxDialog::Init()
{
  m_oldFocus = nullptr;
  m_isShown = false;
  // the dialog face's colour, as wxSystemSettings gives it on Windows
  SetBackgroundColour(wxColour(0xD4, 0xD0, 0xC8));
}

bool wxDialog::Create(
  wxWindow* const parent,
  const wxWindowID id,
  const wxString& title,
  const wxPoint& pos,
  const wxSize& size,
  long style,
  const wxString& name
)
{
  Init();

  SetExtraStyle(GetExtraStyle() | kTopLevelExDialog);

  // save focus before doing anything which can potentially change it
  m_oldFocus = FindFocus();

  // All dialogs should really have this style
  style |= wxTAB_TRAVERSAL;

  return wxTopLevelWindow::Create(parent, id, title, pos, size, style, name);
}

void wxDialog::SetModal(const bool flag)
{
  if (flag) {
    m_windowStyle |= wxDIALOG_MODAL;
    Erase(wxModelessWindows, this);
  } else {
    m_windowStyle &= ~wxDIALOG_MODAL;
    wxModelessWindows.push_back(this);
  }
}

wxDialog::~wxDialog()
{
  m_isBeingDeleted = true;

  // this will also reenable all the other windows for a modal dialog
  Show(false);
}

void wxDialog::OnCharHook(wxKeyEvent& event)
{
  if (GetHWND()) {
    // "Esc" works as an accelerator for the "Cancel" button, but it shouldn't close the dialog
    // which doesn't have any cancel button
    if ((event.m_keyCode == WXK_ESCAPE) && FindWindow(wxID_CANCEL)) {
      wxCommandEvent cancelEvent(wxEVT_COMMAND_BUTTON_CLICKED, wxID_CANCEL);
      cancelEvent.SetEventObject(this);
      GetEventHandler()->ProcessEvent(cancelEvent);

      // ensure that there is another message for this window so the ShowModal loop will exit
      // and won't get stuck in GetMessage().
      ::PostMessageW(wxlite::ToHwnd(GetHWND()), WM_NULL, 0, 0);
      return;
    }
  }
  // We didn't process this event.
  event.Skip();
}

bool wxDialog::IsModal() const
{
  return (GetWindowStyleFlag() & wxDIALOG_MODAL) != 0;
}

bool wxDialog::IsModalShowing() const
{
  const std::vector<wxDialog*>& dialogs = ModalDialogs();
  return std::find(dialogs.begin(), dialogs.end(), this) != dialogs.end();
}

bool wxDialog::Show(const bool show)
{
  if (!wxDialogBase::Show(show) && !(show && IsModal())) {
    // nothing to do
    return false;
  }

  if (show) {
    // usually will result in TransferDataToWindow() being called
    Layout();
    InitDialog();
  }

  if (IsModal()) {
    if (show) {
      // wxWindows 2.4.2 runs the application's loop here until EndModal: the user answers the
      // dialog. Android shows no wx window and nobody can answer, so the dialog ends at once as
      // its close box ends it (OnCloseWindow): with the wxID_CANCEL button's command, whose
      // handler ends it.
      ModalDialogs().push_back(this);
      wxCommandEvent cancelEvent(wxEVT_COMMAND_BUTTON_CLICKED, wxID_CANCEL);
      cancelEvent.SetEventObject(this);
      GetEventHandler()->ProcessEvent(cancelEvent);
      if (IsModalShowing()) {
        // a cancel handler that did not end it: it ends here all the same
        EndModal(wxID_CANCEL);
      }
    } else { // end of modal dialog
      // this will cause IsModalShowing() return FALSE and our local message loop will terminate
      std::vector<wxDialog*>& dialogs = ModalDialogs();
      dialogs.erase(std::remove(dialogs.begin(), dialogs.end(), this), dialogs.end());
    }
  }
  return true;
}

int wxDialog::ShowModal()
{
  if (!IsModal()) {
    SetModal(true);
  }
  Show(true);
  return GetReturnCode();
}

void wxDialog::EndModal(const int retCode)
{
  SetReturnCode(retCode);
  Show(false);
}

// Standard buttons
void wxDialog::OnOK(wxCommandEvent& WXUNUSED(event))
{
  if (Validate() && TransferDataFromWindow()) {
    EndModal(wxID_OK);
  }
}

void wxDialog::OnApply(wxCommandEvent& WXUNUSED(event))
{
  if (Validate()) {
    TransferDataFromWindow();
  }
  // TODO probably need to disable the Apply button until things change again
}

void wxDialog::OnCancel(wxCommandEvent& WXUNUSED(event))
{
  EndModal(wxID_CANCEL);
}

void wxDialog::OnCloseWindow(wxCloseEvent& WXUNUSED(event))
{
  // We'll send a Cancel message by default, which may close the dialog. Check for looping if the
  // Cancel event handler calls Close().

  // Note that if a cancel button and handler aren't present in the dialog, nothing will happen
  // when you close the dialog via the window manager, or via Close(). We wouldn't want to
  // destroy the dialog by default, since the dialog may have been created on the stack. However,
  // this does mean that calling dialog->Close() won't delete the dialog unless the handler for
  // wxID_CANCEL does so. So use Destroy() if you want to be sure to destroy the dialog. The
  // default OnCancel (above) simply ends a modal dialog, and hides a modeless dialog.

  // VZ: this is horrible and MT-unsafe. Can't we reuse some of these global variables like
  //     wxTopLevelWindows?
  static std::vector<wxDialog*> closing;
  if (std::find(closing.begin(), closing.end(), this) != closing.end()) {
    return;
  }

  closing.push_back(this);

  wxCommandEvent cancelEvent(wxEVT_COMMAND_BUTTON_CLICKED, wxID_CANCEL);
  cancelEvent.SetEventObject(this);
  GetEventHandler()->ProcessEvent(cancelEvent); // This may close the dialog

  closing.erase(std::remove(closing.begin(), closing.end(), this), closing.end());
}

long wxDialog::MSWWindowProc(const WXUINT message, const WXWPARAM wParam, const WXLPARAM lParam)
{
  long rc = 0;
  bool processed = false;

  switch (message) {
  case WM_CLOSE:
    // if we can't close, tell the system that we processed the message - otherwise it would
    // close us
    processed = !Close();
    break;

  case WM_SIZE:
    // the Windows dialogs unfortunately are not meant to be resizeable at all and their
    // standard class doesn't include CS_[VH]REDRAW styles which means that the window is not
    // repainted at all when it is shrunk - so we redraw it ourselves
    rc = wxWindow::MSWWindowProc(message, wParam, lParam);
    processed = true;
    if (!HasFlag(wxNO_FULL_REPAINT_ON_RESIZE)) {
      ::InvalidateRect(wxlite::ToHwnd(GetHWND()), nullptr, FALSE /* erase bg */);
    }
    break;

  default:
    break;
  }

  if (!processed) {
    rc = wxWindow::MSWWindowProc(message, wParam, lParam);
  }
  return rc;
}

// --- wxSplashScreen -------------------------------------------------------------------------------

BEGIN_EVENT_TABLE(wxSplashScreen, wxFrame)
  EVT_CLOSE(wxSplashScreen::OnCloseWindow)
END_EVENT_TABLE()

wxSplashScreen::wxSplashScreen(
  const wxBitmap& bitmap,
  const long splashStyle,
  const int milliseconds,
  wxWindow* const parent,
  const wxWindowID id,
  const wxPoint& pos,
  const wxSize& size,
  const long style
)
  : wxFrame(parent, id, wxEmptyString, wxPoint(0, 0), wxSize(100, 100), style)
{
  m_window = nullptr;
  m_splashStyle = splashStyle;
  m_milliseconds = milliseconds;

  m_window = new wxSplashScreenWindow(bitmap, this, -1, pos, size, wxNO_BORDER);

  SetClientSize(bitmap.GetWidth(), bitmap.GetHeight());

  if (m_splashStyle & wxSPLASH_CENTRE_ON_PARENT) {
    CentreOnParent();
  } else if (m_splashStyle & wxSPLASH_CENTRE_ON_SCREEN) {
    CentreOnScreen();
  }

  // wxSPLASH_TIMEOUT closes the screen after `milliseconds` with a wxTimer in wxWindows 2.4.2;
  // wx-lite has no timers, and the engine shows its splash without a timeout.
  Show(true);
  m_window->SetFocus();
  Update(); // Without this, you see a blank screen for an instant
}

wxSplashScreen::~wxSplashScreen() {}

void wxSplashScreen::OnCloseWindow(wxCloseEvent& WXUNUSED(event))
{
  this->Destroy();
}

BEGIN_EVENT_TABLE(wxSplashScreenWindow, wxWindow)
  EVT_PAINT(wxSplashScreenWindow::OnPaint)
  EVT_ERASE_BACKGROUND(wxSplashScreenWindow::OnEraseBackground)
END_EVENT_TABLE()

wxSplashScreenWindow::wxSplashScreenWindow(
  const wxBitmap& bitmap, wxWindow* const parent, const wxWindowID id, const wxPoint& pos, const wxSize& size, const long style
)
  : wxWindow(parent, id, pos, size, style)
{
  m_bitmap = bitmap;
}

void wxSplashScreenWindow::OnPaint(wxPaintEvent& WXUNUSED(event))
{
  wxPaintDC dc(this);
  if (m_bitmap.Ok()) {
    dc.DrawBitmap(m_bitmap, 0, 0);
  }
}

void wxSplashScreenWindow::OnEraseBackground(wxEraseEvent& event)
{
  wxDC* const dc = event.GetDC();
  if (dc && m_bitmap.Ok()) {
    dc->DrawBitmap(m_bitmap, 0, 0);
  }
}
