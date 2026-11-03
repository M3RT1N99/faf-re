# wx-lite: wxWindows for the Android client

The engine's application layer is built on wxWindows 2.4.2 (MSW port, `dependencies/wxWindows-2.4.2`).
Android has no wxWindows port. wx-lite is the part of wxWindows 2.4.2 that the engine uses,
with the same declarations and behaving as wxMSW does. It runs on a small Win32 message layer
that the Android host feeds. The engine's files compile and run unchanged, as everywhere in this
build ([README.md](README.md)).

*Design of 2026-11-02, from the 48 client files that stop at wx.*

## What the engine uses

- **The application.**
  - `MohoApp : wxApp` comes from `IMPLEMENT_APP_NO_MAIN`, built by `wxEntry(..., enterLoop = false)`.
  - `WIN_AppExecute` (`moho/app/WinApp.cpp`) runs a loop of its own around it. It takes messages
    with `PeekMessageW` and hands them to `wxApp::DoMessage` (`WM_QUIT` ends the loop). It calls
    `wxApp::Pending`, `Dispatch` and `ProcessIdle`. Between frames it waits in
    `MsgWaitForMultipleObjectsEx` for its handles or a message.
  - `WinMain` (`moho/app/WinMain.cpp`, not built here) creates `CScApp` and calls
    `WIN_AppExecute`.
- **Windows.**
  - `WSupComFrame : wxFrame` is the main window, and `WD3DViewport : wxWindow` is the one the
    renderer draws into.
  - Both override `MSWWindowProc`. They handle `WM_SETCURSOR`, the side mouse buttons,
    `WM_SYSCOMMAND`, `WM_SIZE`, `WM_EXITSIZEMOVE`, `WM_ERASEBKGND` and `WM_ACTIVATEAPP`
    themselves. Every other message goes to wx's `MSWWindowProc`, which turns it into wx events.
- **Input.** `CUIKeyHandler` and `CMauiWxEventMapper` are `wxEvtHandler`s with event tables for
  keys (`EVT_KEY_DOWN`, `EVT_KEY_UP`, `EVT_CHAR`) and the mouse (`EVT_MOUSE_EVENTS`). They are
  pushed onto the windows.
- **Dialogs and tool windows:**
  - the log window (`WWinLogWindow : WWinManagedDialog : wxDialog`);
  - `WWxInputBox`, `WBitmapPanel`, `WBitmapCheckBox`;
  - the splash screen and message boxes.
- **Other classes** that `moho/app/WxRuntimeTypes.h` names: files and streams (`wxFile`,
  `wxFFile`, `wxFileName`), images (`wxImage`, `wxBitmap`), `wxColourDatabase`.
- **Links:** `OpenURL` (`moho/app/WxUrl.cpp`) parses the link with `wxURL` and opens it with
  `ShellExecuteW` when its protocol is allowed.
- **Tool windows for development:** the script debugger, the emitter editor
  (`EFX_CreateEmitterWindow`), the reflection editor (`SC_CreateEntityDialog`) and the directory
  dialogs of the frame dumps.

## The design

### 1. A Win32 message layer (`include/win32`, `src/win32`)

The Win32 USER functions that the engine and wx-lite need:
- a message queue per thread: `PostMessageW`, `PeekMessageW`, `GetMessageW`, `PostQuitMessage`,
  `DispatchMessageW` and `SendMessageW`;
- windows as objects that carry a window procedure, a size and a parent (an `HWND`), and the
  window functions the engine calls.

`MsgWaitForMultipleObjectsEx` also wakes when a message arrives, as on Windows. The activity's
surface (`ANativeWindow`) is the screen every window is shown on, as a Windows window is part of
the desktop, and the Vulkan surface is made from it (`src/gal/VulkanSurfaceAndroid.cpp`).

### 2. wx-lite proper (`include/wx`, `src/wx`)

The classes and functions the engine uses, declared as in wxWindows 2.4.2:
- **Basics:**
  - `wxObject`, the class the event tables' member pointers are cast to. Nothing in the engine
    asks for run-time class information, so there is no `wxClassInfo`;
  - `wxString` (the Unicode build: `wxChar` is `wchar_t`);
  - `wxPoint`, `wxSize`, `wxRect`, `wxArrayString`.
- **Events:**
  - `wxEvent` and the event types;
  - `wxEvtHandler` with static event tables (`DECLARE_EVENT_TABLE`, `BEGIN_EVENT_TABLE`, the
    `EVT_` macros) and `Connect`;
  - `ProcessEvent` in wx's order: the handlers pushed with `PushEventHandler` first, then the
    class's tables up its hierarchy, then the dynamic handlers. Command events go on to the
    parent, and `Skip` lets the next handler run.
- **Windows:**
  - `wxWindow`, whose `MSWWindowProc` turns messages into wx events as `wxWindowMSW` does:
    the mouse, keys with `wxCharCodeMSWToWX`, size, move, paint, erase, focus, activation, close
    and the cursor;
  - `wxTopLevelWindow`, `wxFrame`, `wxDialog` and `wxPanel`.
- **The application:**
  - `wxApp` with `OnInit`, `OnExit`, `Pending`, `Dispatch`, `DoMessage`, `ProcessIdle` (idle
    events to the windows), `ExitMainLoop`, `SetExitOnFrameDelete` and `SetTopWindow`;
  - `wxTheApp`, `wxEntry` and `IMPLEMENT_APP_NO_MAIN`.
- **The rest of what `WxRuntimeTypes.h` names:**
  - Controls and dialogs exist as windows without a picture. A modal dialog ends at once with
    `wxID_CANCEL`, and a message box goes to the log.
  - The headers of the file and stream classes and of the list, tree and notebook controls are
    empty: `WxRuntimeTypes.h` includes them, and no client file uses their classes. Its size check
    of `wxColourDatabase` is one of the x86 layout checks that 64-bit builds leave out
    (`src/platform/X64LayoutAsserts.h`), so there is no colour database either.
  - Images and bitmaps hold the pixels they are given but draw nothing. The PNG handler loads
    nothing, so the splash screen does not show.
- **Links:** `wxURL` parses as wxWindows 2.4.2 does, with its protocols http, ftp and file (no
  https; the names compare with case). It opens no connections. `ShellExecuteW` hands the link to
  the host, which shows it with what Android has for it (`include/win32/Win32AndroidHost.h`).
- **The tool windows are left out.** wx-lite has none of their controls (trees, tree lists,
  notebooks, sliders). Their files are not built, and the two console commands that open an editor
  say in the log that the build has none: `SC_CreateEntityDialog` (`src/app`) and
  `EFX_CreateEmitterWindow` (a build-time patch, `patches/README.md`).

wx-lite takes its behaviour from wxWindows 2.4.2's own sources in `dependencies/wxWindows-2.4.2`
(`src/common/event.cpp` for event processing, `src/msw/window.cpp` for message translation). Where
it differs, it says so at the place.

### 3. The Android host (`src/host`)

The activity's native half (`src/host/GameHost.cpp`, the app's `GameActivity`):
- It starts the engine thread, which runs the program as `WinMain` does: the diagnostic hooks,
  the die handler and the startup switches, then `CScApp` and `WIN_AppExecute`, with `WinMain`'s
  error handling. The thread has the x64 program's stack (4 MB). Left out, as Windows' own: the
  crash tests, the probes on the debug registers, AQtime, the CRT's allocation log, the
  accessibility keys and Media Center.
- The command line goes to the CRT's `__argc` and `__argv`, which the engine reads. `argv[0]` is
  the program's path in its install (`<game files>/bin/ForgedAlliance.exe`, as FAF's client starts
  it): the engine takes its install's folder from it. HOME and TMPDIR are the app's folders, and
  the host makes the folders a Windows user profile always has (`SHGetFolderPath`'s), which the
  preferences and the documents go into.
- The activity's surface is the screen (`Win32Android_SetScreenSurface`), with its size.
- It turns Android's input and lifecycle into Win32 messages for that thread:
  - touch into the left button where the first finger is, a mouse's buttons into `WM_*BUTTON*`,
    its moves into `WM_MOUSEMOVE` and its wheel into `WM_MOUSEWHEEL`;
  - keys into `WM_KEYDOWN`, `WM_KEYUP` and `WM_CHAR`, as a PC keyboard's (`src/host/KeysAndroid.cpp`:
    virtual keys, set-1 scan codes, the extended keys); Back is Esc;
  - surface changes into `WM_SIZE` of the maximized windows;
  - pause and resume into the application's activation (`WM_ACTIVATEAPP`, `WM_ACTIVATE`).
- When the program returns, its process ends with the exit code, the streams flushed. The engine
  ends the process itself when it cannot start (`TerminateProcess`), as on Windows; the app reads
  how it ended from Android (`ApplicationExitInfo`).

## Checks

- A test program for the event system and the message layer: handler order, `Skip`, propagation,
  and the translation of each message. It runs on the emulator, and on the GitHub arm64 runner as
  the sim-math tests do.
- On the emulator, then the phone: the engine starts under the host, its log shows `CScApp`'s
  initialisation, the loop runs, and input arrives.

## Order

1. The headers and the basics, until the 48 files compile. *Done.*
2. The message layer and the translation. *Done.*
3. `libfafgame.so` links (the vertex formatters and whatever else the linker names). *Done
   2026-11-03, for arm64 and x86_64 ([../CLIENT_PLAN.md](../CLIENT_PLAN.md), Progress).*
4. The host: the engine starts on the emulator. *Started 2026-11-03, for x86_64 and for arm64
   under the emulator's ARM translation: `CScApp`'s initialisation runs (the archives, the
   preferences, the user Lua, the fonts, the UI) up to `CreateDevice`. The Vulkan renderer stops
   there without its shader compiler (C2), so the engine ends with exit code 1, as a PC without a
   usable renderer does. The loop and the input come once the renderer does.*
