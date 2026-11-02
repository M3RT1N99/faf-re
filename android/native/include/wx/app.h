#pragma once

// wx-lite (android/native/WX_LITE.md): wxWindows 2.4.2's wxAppBase (include/wx/app.h) and the MSW
// port's wxApp (include/wx/msw/app.h), with wxEntry and the application macros, for what the
// engine uses. The loop runs on the message layer's queue of the thread that called wxEntry: the
// engine's own loop (moho::WIN_AppExecute) takes the messages and calls Pending, Dispatch,
// DoMessage and ProcessIdle, as on Windows.

#include "wx/build.h"
#include "wx/event.h"
#include "wx/icon.h"
#include "wx/window.h"

class wxApp;

typedef wxApp* (*wxAppInitializerFunction)();

class wxAppBase : public wxEvtHandler
{
  DECLARE_NO_COPY_CLASS(wxAppBase)

public:
  wxAppBase();
  ~wxAppBase() override;

  // the virtual functions which may/must be overridden in the derived class
  // -----------------------------------------------------------------------

  // called during the program initialization, returning FALSE from here prevents the program
  // from continuing - it's a good place to create the top level program window and return TRUE.
  virtual bool OnInit();

  // a platform-dependent version of OnInit(): the code here is likely to depend on the toolkit.
  virtual bool OnInitGui();

  // called to start program execution - the default version just enters the main GUI loop in
  // which events are received and processed until the last window is not deleted (if
  // GetExitOnFrameDelete) or ExitMainLoop() is called.
  virtual int OnRun();

  // called after the main loop termination. This is a good place for cleaning up (it may be
  // too late in dtor) and is also useful if you want to return some custom exit code (this
  // function is only called if OnInit() succeeded and OnRun() was called)
  virtual int OnExit();

  // called when a fatal exception occurs, this function should take care not to do anything
  // which might provoke a nested exception!
  virtual void OnFatalException() {}

  // the worker functions - usually not used directly by the user code
  // -----------------------------------------------------------------

  // execute the main GUI loop, the function returns when the loop ends
  virtual int MainLoop() = 0;

  // exit the main GUI loop during the next iteration (i.e. it does not stop the program
  // immediately!)
  virtual void ExitMainLoop() = 0;

  // returns TRUE if the program is initialized
  virtual bool Initialized() = 0;

  // returns TRUE if there are unprocessed events in the event queue
  virtual bool Pending() = 0;

  // process the first event in the event queue (blocks until an event apperas if there are
  // none currently)
  virtual void Dispatch() = 0;

  // process all currently pending events right now
  virtual bool Yield(bool onlyIfNeeded = false) = 0;

  // send idle event to window and all subwindows
  virtual bool ProcessIdle() = 0;

  // application info: name, description, vendor
  // --------------------------------------------

  wxString GetAppName() const
  {
    if (m_appName.IsEmpty()) {
      return m_className;
    }
    return m_appName;
  }
  void SetAppName(const wxString& name)
  {
    m_appName = name;
  }
  wxString GetClassName() const
  {
    return m_className;
  }
  void SetClassName(const wxString& name)
  {
    m_className = name;
  }
  const wxString& GetVendorName() const
  {
    return m_vendorName;
  }
  void SetVendorName(const wxString& name)
  {
    m_vendorName = name;
  }

  // top level window functions
  // --------------------------

  // return TRUE if our app has focus
  virtual bool IsActive() const
  {
    return m_isActive;
  }

  // set the "main" top level window
  void SetTopWindow(wxWindow* win)
  {
    m_topWindow = win;
  }

  // return the "main" top level window (if it hadn't been set previously with SetTopWindow(),
  // will return just some top level window and, if there are none, will return NULL)
  virtual wxWindow* GetTopWindow() const
  {
    if (m_topWindow) {
      return m_topWindow;
    }
    if (!wxTopLevelWindows.empty()) {
      return wxTopLevelWindows.front();
    }
    return nullptr;
  }

  // control the exit behaviour: by default, the program will exit the main loop (and so,
  // usually, terminate) when the last top-level program window is deleted. Beware that if you
  // disable this (with SetExitOnFrameDelete(FALSE)), you'll have to call ExitMainLoop()
  // explicitly from somewhere.
  void SetExitOnFrameDelete(bool flag)
  {
    m_exitOnFrameDelete = flag ? Yes : No;
  }
  bool GetExitOnFrameDelete() const
  {
    return m_exitOnFrameDelete == Yes;
  }

  // set the active state of the application (the top-level window that had the focus last)
  virtual void SetActive(bool isActive, wxWindow* lastFocus);

  // this method allows to filter all the events processed by the program, so you should try to
  // return quickly from it to avoid slowing down the program to the crawl
  //
  // return value should be -1 to continue with the normal event processing, or TRUE or FALSE
  // to stop further processing and pretend that the event had been already processed or won't
  // be processed at all, respectively
  virtual int FilterEvent(wxEvent& event);

  // check that the wxBuildOptions object (constructed in the application itself, usually the
  // one from IMPLEMENT_APP() macro) matches the build options of the library and abort if it
  // doesn't
  static bool CheckBuildOptions(const wxBuildOptions& buildOptions);

  // set/get the app initializer function
  static void SetInitializerFunction(wxAppInitializerFunction fn)
  {
    m_appInitFn = fn;
  }
  static wxAppInitializerFunction GetInitializerFunction()
  {
    return m_appInitFn;
  }

  // process all events in the wxPendingEvents list
  virtual void ProcessPendingEvents();

  // access to the command line arguments
  int argc;
  wxChar** argv;

protected:
  // function used for dynamic wxApp creation
  static wxAppInitializerFunction m_appInitFn;

  // application info (must be set from the user code)
  wxString m_vendorName, m_appName, m_className;

  // the main top level window - may be NULL
  wxWindow* m_topWindow;

  // if Yes, exit the main loop when the last top level window is deleted, if No don't do it and
  // if Later -- only do it once we reach our OnRun()
  enum
  {
    Later = -1,
    No,
    Yes
  } m_exitOnFrameDelete;

  // TRUE if the application wants to get debug output
  bool m_useBestVisual;

  // does any of our windows has focus?
  bool m_isActive;
};

class wxApp : public wxAppBase
{
public:
  wxApp();
  ~wxApp() override;

  // override base class (pure) virtuals
  int MainLoop() override;
  /// Posts WM_QUIT; the loop that takes it stops (wxApp::DoMessage clears m_keepGoing).
  void ExitMainLoop() override;
  bool Initialized() override;
  /// True when the thread's queue holds a message (PeekMessageW, PM_NOREMOVE).
  bool Pending() override;
  /// Takes the next message, waiting for one (GetMessageW), and dispatches it.
  void Dispatch() override;
  bool Yield(bool onlyIfNeeded = false) override;
  /// Sends wxEVT_IDLE to the application, whose handler processes the pending events, deletes
  /// the windows due for it and sends wxEVT_IDLE to every window. True when more is wanted.
  bool ProcessIdle() override;

  void OnIdle(wxIdleEvent& event);
  void OnEndSession(wxCloseEvent& event);
  void OnQueryEndSession(wxCloseEvent& event);

  // Send idle event to all top-level windows. Returns TRUE if more idle time is requested.
  bool SendIdleEvents();

  // Send idle event to window and all subwindows Returns TRUE if more idle time is requested.
  bool SendIdleEvents(wxWindow* win);

protected:
  bool m_showOnInit;
  int m_printMode; // wxPRINT_WINDOWS, wxPRINT_POSTSCRIPT
  bool m_auto3D; // Always use 3D controls, except where overriden

  /* Windows-specific wxApp definitions */

public:
  // Implementation
  static bool Initialize();
  static void CleanUp();

  static bool RegisterWindowClasses();
  static bool UnregisterWindowClasses();

  // message processing
  // ------------------

  // process the given message
  virtual void DoMessage(WXMSG* pMsg);

  // retrieve the next message from the queue and process it
  virtual bool DoMessage();

  // preprocess the message
  virtual bool ProcessMessage(WXMSG* pMsg);

  // idle processing
  // ---------------

  void DeletePendingObjects();

public:
  int m_nCmdShow;

protected:
  bool m_keepGoing;

  DECLARE_EVENT_TABLE()
};

/// The application object: wxEntry makes it.
extern wxApp* wxTheApp;

/// The objects (windows) to delete in the next idle time (wxApp::DeletePendingObjects).
extern std::vector<wxObject*> wxPendingDelete;

/// Initializes wx and makes the application (the IMPLEMENT_APP initializer), calls its OnInit, and
/// runs its loop if `enterLoop`; without, it returns 1 after OnInit, the application staying up.
int wxEntry(WXHINSTANCE hInstance, WXHINSTANCE hPrevInstance, char* lpszCmdLine, int nCmdShow, bool enterLoop = true);

// Force an exit from main loop
void wxExit();

// Yield to other apps/messages
bool wxYield();

// Yield to other apps/messages
void wxWakeUpIdle();

// Post a message to the given eventhandler which will be processed during the next event loop
// iteration
inline void wxPostEvent(wxEvtHandler* dest, wxEvent& event)
{
  wxCHECK_RET(dest, wxT("need an object to post event to in wxPostEvent"));
  dest->AddPendingEvent(event);
}

// this class is used to set the app initializer function from a static constructor
class wxAppInitializer
{
public:
  wxAppInitializer(wxAppInitializerFunction fn)
  {
    wxApp::SetInitializerFunction(fn);
  }
};

#define IMPLEMENT_APP_NO_MAIN(appname)                                           \
  wxApp* wxCreateApp()                                                           \
  {                                                                              \
    wxApp::CheckBuildOptions(wxBuildOptions());                                  \
    return new appname;                                                          \
  }                                                                              \
  wxAppInitializer wxTheAppInitializer((wxAppInitializerFunction)wxCreateApp);   \
  appname& wxGetApp()                                                            \
  {                                                                              \
    return *(appname*)wxTheApp;                                                  \
  }

// this macro can be used multiple times and just allows you to use wxGetApp() function
#define DECLARE_APP(appname) extern appname& wxGetApp();
