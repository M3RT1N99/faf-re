// wx-lite (android/native/WX_LITE.md): the application of wxWindows 2.4.2 (src/common/appcmn.cpp,
// src/msw/app.cpp): wxAppBase, wxApp with its loop on the message layer's queue, wxEntry, the idle
// processing and the objects deleted in it.

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "Private.h"
#include "wx/app.h"
#include "wx/brush.h"
#include "wx/image.h"
#include "wx/toplevel.h"

wxApp* wxTheApp = nullptr;
std::vector<wxObject*> wxPendingDelete;
wxAppInitializerFunction wxAppBase::m_appInitFn = nullptr;

bool wxlite::gInOnIdle = false;

namespace
{
  /// The message the loop took last (s_currentMsg).
  MSG gCurrentMsg{};

  /// The thread whose queue the application's loop takes (the thread of wxApp::Initialize).
  std::atomic<DWORD> gLoopThread{0};

  // the background of wx-lite's window class: COLOR_APPWORKSPACE + 1, as wxWindows registers it
  constexpr std::uintptr_t kClassBackground = 12 + 1;
} // namespace

// --- wxAppBase ----------------------------------------------------------------------------------

wxAppBase::wxAppBase()
{
  wxTheApp = static_cast<wxApp*>(this);

  argc = 0;
  argv = nullptr;
  m_topWindow = nullptr;
  m_useBestVisual = false;
  m_isActive = true;

  // We don't want to exit the app if the user code shows a dialog from its OnInit() -- but this
  // is what would happen if we set m_exitOnFrameDelete to Yes here because this dialog would be
  // the last frame to be deleted and so we'd exit the main loop. So we're going to set it to
  // Yes later -- in OnRun()
  m_exitOnFrameDelete = Later;
}

wxAppBase::~wxAppBase()
{
  // this destructor is required for Darwin
}

bool wxAppBase::OnInit()
{
  // wxWindows 2.4.2 parses argc/argv here (wxCmdLineParser); wxEntry gives the application none
  return true;
}

bool wxAppBase::OnInitGui()
{
  return true;
}

int wxAppBase::OnRun()
{
  // see the comment in ctor: if the initial value hasn't been changed, use the default Yes from
  // now on
  if (m_exitOnFrameDelete == Later) {
    m_exitOnFrameDelete = Yes;
  }
  //else: it has been changed, assume the user knows what he is doing

  return MainLoop();
}

int wxAppBase::OnExit()
{
  return 0;
}

void wxAppBase::SetActive(const bool active, wxWindow* WXUNUSED(lastFocus))
{
  if (active == m_isActive) {
    return;
  }

  m_isActive = active;

  wxActivateEvent event(wxEVT_ACTIVATE_APP, active);
  event.SetEventObject(this);

  (void)ProcessEvent(event);
}

int wxAppBase::FilterEvent(wxEvent& WXUNUSED(event))
{
  // process the events normally by default
  return -1;
}

bool wxAppBase::CheckBuildOptions(const wxBuildOptions& opts)
{
  // the library and the program see one header: the options cannot differ, but are checked as
  // wxWindows checks them
  const wxBuildOptions library;
  return opts.m_verMajor == library.m_verMajor && opts.m_verMinor == library.m_verMinor
      && opts.m_isDebug == library.m_isDebug;
}

// --- wxApp --------------------------------------------------------------------------------------

BEGIN_EVENT_TABLE(wxApp, wxEvtHandler)
  EVT_IDLE(wxApp::OnIdle)
  EVT_END_SESSION(wxApp::OnEndSession)
  EVT_QUERY_END_SESSION(wxApp::OnQueryEndSession)
END_EVENT_TABLE()

wxApp::wxApp()
{
  argc = 0;
  argv = nullptr;
  m_showOnInit = true;
  m_printMode = 1; // wxPRINT_WINDOWS
  m_auto3D = true;
  m_nCmdShow = SW_SHOWNORMAL;
  m_keepGoing = false;
}

wxApp::~wxApp()
{
  // Delete command-line args
  for (int i = 0; i < argc; i++) {
    delete[] argv[i];
  }
  delete[] argv;
}

bool wxApp::Initialize()
{
  // wxWindows 2.4.2 also sets up the colour database, the resource system, OLE, CTL3D and its
  // modules here; wx-lite has none of them. What it does set up: the stock objects and the
  // window classes.
  // the thread's message queue, made now, as Windows makes one when a thread first peeks: the
  // wake-ups of wxWakeUpIdle can reach it from the start
  MSG msg;
  (void)::PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
  gLoopThread.store(::GetCurrentThreadId());

  wxBLACK_BRUSH = new wxBrush(wxColour(0, 0, 0), wxSOLID);
  wxWHITE_BRUSH = new wxBrush(wxColour(0xFF, 0xFF, 0xFF), wxSOLID);
  wxTRANSPARENT_BRUSH = new wxBrush(wxColour(0, 0, 0), wxTRANSPARENT);

  return RegisterWindowClasses();
}

bool wxApp::RegisterWindowClasses()
{
  WNDCLASSW wndclass{};

  // the fields which are common to all classes
  wndclass.lpfnWndProc = &wxlite::WindowProcedure;
  wndclass.hbrBackground = reinterpret_cast<HBRUSH>(kClassBackground);

  // Register the frame window class.
  wndclass.lpszClassName = wxlite::kCanvasClassName;
  wndclass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  if (!::RegisterClassW(&wndclass)) {
    return false;
  }

  // "no redraw" frame
  wndclass.lpszClassName = wxlite::kCanvasClassNameNR;
  wndclass.style = CS_DBLCLKS;
  return ::RegisterClassW(&wndclass) != 0;
}

bool wxApp::UnregisterWindowClasses()
{
  bool retval = true;
  if (!::UnregisterClassW(wxlite::kCanvasClassName, nullptr)) {
    retval = false;
  }
  if (!::UnregisterClassW(wxlite::kCanvasClassNameNR, nullptr)) {
    retval = false;
  }
  return retval;
}

void wxApp::CleanUp()
{
  // flush the logged messages if any and install a 'safer' log target: wx-lite has no wxLog

  // One last chance for pending objects to be cleaned up
  wxTheApp->DeletePendingObjects();

  delete wxBLACK_BRUSH;
  delete wxWHITE_BRUSH;
  delete wxTRANSPARENT_BRUSH;
  wxBLACK_BRUSH = wxWHITE_BRUSH = wxTRANSPARENT_BRUSH = nullptr;

  // the image module's clean up (wxImageModule::OnExit)
  wxImage::CleanUpHandlers();

  UnregisterWindowClasses();

  delete wxTheApp;
  wxTheApp = nullptr;
}

bool wxApp::Initialized()
{
  return GetTopWindow() != nullptr;
}

// Get and process a message, returning FALSE if WM_QUIT received (and also set the flag telling
// the app to exit the main loop)
bool wxApp::DoMessage()
{
  const BOOL rc = ::GetMessageW(&gCurrentMsg, nullptr, 0, 0);
  if (rc == 0) {
    // got WM_QUIT
    m_keepGoing = false;
    return false;
  }
  if (rc == -1) {
    // should never happen, but let's test for it nevertheless
    return true;
  }

  // Process the message
  DoMessage(reinterpret_cast<WXMSG*>(&gCurrentMsg));
  return true;
}

void wxApp::DoMessage(WXMSG* const pMsg)
{
  if (!ProcessMessage(pMsg)) {
    ::TranslateMessage(reinterpret_cast<MSG*>(pMsg));
    ::DispatchMessageW(reinterpret_cast<MSG*>(pMsg));
  }
}

// Keep trying to process messages until WM_QUIT received.
//
// If there are messages to be processed, they will all be processed and OnIdle will not be
// called. When there are no more messages, OnIdle is called. If OnIdle requests more time, it
// will be repeatedly called so long as there are no pending messages. A 'feature' of this is that
// once OnIdle has decided that no more processing is required, then it won't get processing time
// until further messages are processed (it'll sit in DoMessage).
int wxApp::MainLoop()
{
  m_keepGoing = true;

  while (m_keepGoing) {
    while (!Pending() && ProcessIdle()) {
      ;
    }

    // a message came or no more idle processing to do
    DoMessage();
  }

  return static_cast<int>(gCurrentMsg.wParam);
}

// Returns TRUE if more time is needed.
bool wxApp::ProcessIdle()
{
  wxIdleEvent event;
  event.SetEventObject(this);
  ProcessEvent(event);
  return event.MoreRequested();
}

void wxApp::ExitMainLoop()
{
  // this will set m_keepGoing to FALSE a bit later
  ::PostQuitMessage(0);
}

bool wxApp::Pending()
{
  return ::PeekMessageW(&gCurrentMsg, nullptr, 0, 0, PM_NOREMOVE) != 0;
}

void wxApp::Dispatch()
{
  DoMessage();
}

// Give all windows a chance to preprocess the message. Some may have accelerator tables, or have
// MDI complications.
bool wxApp::ProcessMessage(WXMSG* const wxmsg)
{
  MSG* const msg = reinterpret_cast<MSG*>(wxmsg);
  wxWindow* const wndThis = wxGetWindowFromHWND(wxlite::ToWx(msg->hwnd));

  // this may happen if the event occured in a standard modeless dialog (the only example of which
  // I know of is the find/replace dialog): wxWindows 2.4.2 calls IsDialogMessage() then. There
  // are no standard dialogs here.
  if (!wndThis) {
    return false;
  }

  // allow the window to prevent certain messages from being translated/processed (this is
  // currently used by wxTextCtrl to always grab Ctrl-C/V/X, even if they are also accelerators in
  // some parent)
  if (!wndThis->MSWShouldPreProcessMessage(wxmsg)) {
    return false;
  }

  // try translations first: the accelerators override everything
  wxWindow* wnd;
  for (wnd = wndThis; wnd; wnd = wnd->GetParent()) {
    if (wnd->MSWTranslateMessage(wxmsg)) {
      return true;
    }

    // stop at first top level window, i.e. don't try to process the key strokes originating in a
    // dialog using the accelerators of the parent frame - this doesn't make much sense
    if (wnd->IsTopLevel()) {
      break;
    }
  }

  // now try the other hooks (kbd navigation is handled here): we start from wndThis->GetParent()
  // because wndThis->MSWProcessMessage() was already called above
  for (wnd = wndThis->GetParent(); wnd; wnd = wnd->GetParent()) {
    if (wnd->MSWProcessMessage(wxmsg)) {
      return true;
    }
  }

  // no special preprocessing for this message, dispatch it normally
  return false;
}

void wxApp::OnIdle(wxIdleEvent& event)
{
  // Avoid recursion (via ProcessEvent default case)
  if (wxlite::gInOnIdle) {
    return;
  }

  wxlite::gInOnIdle = true;

  // If there are pending events, we must process them: pending events are either events to the
  // threads other than main or events posted with wxPostEvent() functions
  // GRG: I have moved this here so that all pending events are processed before starting to
  //   delete any objects. This behaves better (in particular, wrt wxPostEvent) and is coherent
  //   with wxGTK's current behaviour. Changed Feb/2000 before 2.1.14
  ProcessPendingEvents();

  // 'Garbage' collection of windows deleted with Close().
  DeletePendingObjects();

  // Send OnIdle events to all windows
  if (SendIdleEvents()) {
    // SendIdleEvents() returns TRUE if at least one window requested more idle events
    event.RequestMore(true);
  }

  wxlite::gInOnIdle = false;
}

// Send idle event to all top-level windows
bool wxApp::SendIdleEvents()
{
  bool needMore = false;

  // a window may go away in its idle handler: the walk is over a copy
  const wxWindowList windows = wxTopLevelWindows;
  for (wxWindow* const win : windows) {
    if (std::find(wxTopLevelWindows.begin(), wxTopLevelWindows.end(), win) != wxTopLevelWindows.end() && SendIdleEvents(win)) {
      needMore = true;
    }
  }
  return needMore;
}

// Send idle event to window and all subwindows
bool wxApp::SendIdleEvents(wxWindow* const win)
{
  bool needMore = false;

  wxIdleEvent event;
  event.SetEventObject(win);
  win->GetEventHandler()->ProcessEvent(event);

  if (event.MoreRequested()) {
    needMore = true;
  }

  const wxWindowList children = win->GetChildren();
  for (wxWindow* const child : children) {
    if (SendIdleEvents(child)) {
      needMore = true;
    }
  }
  return needMore;
}

void wxApp::DeletePendingObjects()
{
  while (!wxPendingDelete.empty()) {
    wxObject* const obj = wxPendingDelete.front();

    delete obj;

    // the window's destructor took it out of the list; anything else is taken out here
    const auto found = std::find(wxPendingDelete.begin(), wxPendingDelete.end(), obj);
    if (found != wxPendingDelete.end()) {
      wxPendingDelete.erase(found);
    }

    // Deleting one object may have deleted other pending objects, so start from beginning of
    // list again.
  }
}

void wxApp::OnEndSession(wxCloseEvent& WXUNUSED(event))
{
  if (GetTopWindow()) {
    GetTopWindow()->Close(true);
  }
}

// Default behaviour: close the application with prompts. The user can veto the close, and
// therefore the end session.
void wxApp::OnQueryEndSession(wxCloseEvent& event)
{
  if (GetTopWindow()) {
    if (!GetTopWindow()->Close(!event.CanVeto())) {
      event.Veto(true);
    }
  }
}

// Yield to incoming messages
bool wxApp::Yield(const bool onlyIfNeeded)
{
  static bool s_inYield = false;

  if (s_inYield) {
    if (!onlyIfNeeded) {
      wxFAIL_MSG(wxT("wxYield called recursively"));
    }
    return false;
  }

  s_inYield = true;

  // we don't want to process WM_QUIT from here - it should be processed in the main event loop
  // in order to stop it
  MSG msg;
  while (::PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE) && msg.message != WM_QUIT) {
    if (!wxTheApp->DoMessage()) {
      break;
    }
  }

  // if there are pending events, we must process them.
  ProcessPendingEvents();

  s_inYield = false;
  return true;
}

// --- Free functions -----------------------------------------------------------------------------

int wxEntry(
  const WXHINSTANCE WXUNUSED(hInstance), WXHINSTANCE WXUNUSED(hPrevInstance), char* const lpCmdLine, const int nCmdShow,
  const bool enterLoop
)
{
  if (!wxApp::Initialize()) {
    return 0;
  }

  // create the application object or ensure that one already exists
  if (!wxTheApp) {
    // The app may have declared a global application object, but we recommend the IMPLEMENT_APP
    // macro is used instead, which sets an initializer function for delayed, dynamic app object
    // construction.
    wxCHECK_MSG(wxApp::GetInitializerFunction(), 0, wxT("No initializer - use IMPLEMENT_APP macro."));
    wxTheApp = (*wxApp::GetInitializerFunction())();
  }

  wxCHECK_MSG(wxTheApp, 0, wxT("You have to define an instance of wxApp!"));

  // save the WinMain() parameters (the command line: wxWindows splits it into argv; the engine
  // passes none)
  (void)lpCmdLine;
  wxTheApp->m_nCmdShow = nCmdShow;

  // init the app
  int retValue = wxTheApp->OnInitGui() && wxTheApp->OnInit() ? 0 : -1;

  if (retValue == 0) {
    if (enterLoop) {
      // run the main loop
      wxTheApp->OnRun();
    } else {
      // we want to initialize, but not run or exit immediately.
      return 1;
    }
  }
  //else: app initialization failed, so we skipped OnRun()

  wxWindow* const topWindow = wxTheApp->GetTopWindow();
  if (topWindow) {
    // Forcibly delete the window.
    if (topWindow->IsTopLevel()) {
      topWindow->Close(true);
      wxTheApp->DeletePendingObjects();
    } else {
      delete topWindow;
      wxTheApp->SetTopWindow(nullptr);
    }
  }

  retValue = wxTheApp->OnExit();

  wxApp::CleanUp();

  return retValue;
}

void wxExit()
{
  if (wxTheApp) {
    wxTheApp->OnExit();
  }
  wxApp::CleanUp();
  std::exit(0);
}

bool wxYield()
{
  return wxTheApp && wxTheApp->Yield();
}

void wxWakeUpIdle()
{
  // A dummy message so idle handler processing will start up again. wxWindows 2.4.2 posts it to
  // the top window; wx-lite posts it to the loop's thread, which wakes the same queue without
  // reading the windows, which belong to that thread, from another one (AddPendingEvent runs on
  // any thread).
  const DWORD thread = gLoopThread.load();
  if (thread != 0) {
    (void)::PostThreadMessageW(thread, WM_NULL, 0, 0);
  }
}
