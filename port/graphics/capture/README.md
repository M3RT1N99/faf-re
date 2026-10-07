# port/graphics/capture — the D3D9 reference frame harness (M6a step 0)

`main.exe` built with the graphics option renders FAF's main menu **deterministically** and writes
chosen frames, read back through `gpg::gal`, to disk. It is the 1:1 reference the Diligent backend
is compared against (steps 1 and 3 of the plan in [docs/port/renderer.md](../../../docs/port/renderer.md)),
and it is built to run **while someone uses the desktop**: no window is ever shown or activated, no
input reaches it, and it changes nothing outside its own process and scratch directory.

Nothing here is in the default `main.exe`. The sources compile only with the FafPortGraphics MSBuild
option (`port/graphics/port_graphics.props` globs `capture\**\*.cpp` and defines `FAF_PORT_GRAPHICS`),
and the two engine hooks are inside `#if defined(FAF_PORT_GRAPHICS)`. Without `/galharness` on the
command line a graphics build behaves exactly like the default one.

| File | What it is |
|---|---|
| `GalCapture.h` | The two calls the engine makes: `HarnessFrame` (CScApp::Main) and `HarnessSceneEnded` (WRenViewport::Render) |
| `GalCapture.cpp` | Frame driving, the gal readback, the BMP writer, `harness.json` |
| `HarnessSandbox.cpp` | Static-initialiser bootstrap, options, the log, the import-table redirects, the thread hooks, the visibility monitor, the stall reporter |
| `HarnessPins.cpp` | The virtual Lua clock and the fixed random seed |
| `HarnessInternal.h` | Shared declarations, exit codes |

## Running it

`scripts/port/gfx_capture.py` does everything around a run (scratch prefs and init file, the GUI
lock, priority, the outside monitor, the comparison):

```bash
msbuild src\sdk\main.vcxproj /p:Configuration=Debug /p:Platform=Win32 /p:FafPortGraphics=true
python scripts/port/gfx_capture.py --runs 3 --lock <shared lock file>     # gate 0
python scripts/port/gfx_capture.py --gal diligent:d3d11 --frames none --exit-frame 900
python scripts/port/gfx_capture.py compare <run dir A> <run dir B>        # e.g. D3D9 against Diligent
```

From Git Bash prefix `MSYS_NO_PATHCONV=1`. The script refuses to start when a `FAF_*` variable is set
(the committed render-path probes arm themselves from `FAF_TOGGLE_DIR`, `FAF_DUMP_FRAMES`, ...), or
when the exe does not contain the harness (the options would be ignored and the game window would
open). The engine command line it builds:

```
main.exe /galharness <run> /galframes 60,300,900 /galpace 30 /galseed 0x6D366100
         /windowed 1280 720 /framerate 30 /nomovie /nosound /nomusic /nobugreport
         /init <run>\init_harness.lua /prefs <run>\Game.prefs (relative to the prefs folder)
         /cachedir <run>\cache /log <run>\engine.log
```

| Option | Meaning |
|---|---|
| `/galharness <dir>` | Turns the harness on; output directory |
| `/galframes <list>` | Frames to capture, comma list (default `60,300,900`), or `none` |
| `/galexitframe <n>` | Exit after frame n (default: the last capture frame) |
| `/galpace <fps>` | Real-time cap, default 30; `0` runs as fast as the engine can. It does not change the frames (below) |
| `/galseed <n>` | Seed of the global random stream (default 0x6D366100) |
| `/galnopins` | Wall-clock Lua time and the engine's time-based seed: only to show what the pins fix |
| `/framerate <n>` | Required: the engine's fixed frame delta (CScApp.cpp:297-315) |

Exit codes: 0 every frame captured; 10 refused (bad options, a `FAF_*` variable, a pin could not be
installed); 11 a window of the process became visible or foreground; 12 the engine tried to open a
message box or modal dialog (an engine error: the text is in the summary); 13 a capture frame was not
rendered or the readback failed. The launcher kills the process on its own findings too (visible
window, foreground, priority change, timeout).

## Output

- `frame_<N>.bmp`: the head-0 back buffer, 32-bit uncompressed BMP, bottom-up, the bytes as the
  A8R8G8B8 surface holds them (B, G, R, A). Viewers show RGB; the alpha byte is kept.
- `harness.json`: status, configuration, device (`api`, head size, the `d3d9.dll` that was loaded),
  frame and paint counts, `main_menu_module_loaded` (FAF's `__modules` has `/lua/ui/menus/main.lua`),
  per capture the FNV-1a 64 hash of the RGB bytes and of the alpha bytes and the clock, the pins
  (binders swapped, clock reads, seed) and the sandbox report (every intercepted call, counted, with
  samples; blocked activations; dropped input messages; top-level windows created; violations).
- `harness.log`: the same as it happens, plus the teardown's intercepted calls.

## Frames and the capture point

Frame N is the paint that follows the N-th `CScApp::Main`. A visible run gets it from WM_PAINT:
Main ends with `CD3DDevice::Refresh` (CScApp.cpp:1098-1105), wx invalidates the viewport, the loop's
`MsgWaitEx` wakes and wx dispatches WM_PAINT to `WD3DViewport::OnPaint` → `CD3DDevice::Paint`
(WinApp.cpp:2777-2808, WxRuntimeTypes.cpp:238-251). A hidden window gets no WM_PAINT, so
`HarnessFrame` posts a message to a message-only window instead, dispatched by the same wx `Dispatch`
(or, on frame 1, by the nested pump that FAF's `FlushEvents` runs while the front end starts,
UiRuntimeTypes.cpp:17633-17694; harness.log logs that stack). Its handler calls the same `CD3DDevice::Paint`:
Present of frame N-1, then `WRenViewport::Render` of frame N. Because nothing waits for a WM_PAINT
any more, `HarnessFrame` also sets a zero wake-up time so the loop never sleeps in `MsgWaitEx`.

The capture runs in `WRenViewport::Render` right after `device->EndScene()` (WxRuntimeTypes.cpp, the
`HarnessSceneEnded` call), through gal only, so any backend that implements these three calls can be
read back the same way:

1. `Device::GetHeadOutputContext(head)->surface` and its `RenderTargetContext` size;
2. `Device::CreateTexture` with `TextureContext{source_ = 2 (empty), usage_ = 3 (system memory; D3D9
   D3DPOOL_SYSTEMMEM, D3D9Interfaces.cpp:2569-2577), format_ = 2 (A8R8G8B8), mipmapLevels_ = 1}`, then
   `Device::GetRenderTargetData(surface, texture)`;
3. `Texture::Lock(0, empty RECT, 2 /* read-only */)`: rows of B, G, R, A at `pitch`, then
   `Unlock(lock)`.

A capture frame that was not rendered, or a gal error, ends the run with exit code 13, never with a
shifted frame.

## What makes it deterministic

| Source of variation | Where | Pin |
|---|---|---|
| Frame delta | CScApp::Main measures wall time per frame | `/framerate 30`: Main hands out exactly 1/30 s from frame 2 on (CScApp.cpp:1039-1059); the harness refuses to run without it |
| Lua wall clock | `CurrentTime`, `GetSystemTimeSeconds`, `GetSystemTime` read `gpg::time::GetSystemTimer` (Sim.cpp:14387, 18411, 18495). FAF's `WaitSeconds` is built on `CurrentTime` (lua.nx2 lua/userInit.lua:52-67), and the menu starts its first animation with `WaitSeconds(.2)` (lua/ui/menus/main.lua:678-680) | The three binders' function pointers are replaced before any Lua state exists (HarnessPins.cpp); they read the sum of Main's frame deltas. The menu reads it 7 times |
| Random numbers | `math_GlobalRandomStream` is seeded from `time(0) ^ GetTickCount()` (MathReflection.cpp:259) and backs user `Random` (Sim.cpp:14794) | Seeded with `/galseed` at static init and again on frame 1 |
| Real mouse and keyboard | Hover and clicks come from WM_MOUSE*/WM_KEY*; `CUIManager::GetControlAtCursor` polls the global cursor (CUIManager.cpp:545) | The window is hidden and placed beyond the virtual screen, never activated, and a WH_GETMESSAGE hook turns any input message into WM_NULL (0 seen so far) |
| Background movie, sound | `mainmenu_bgmovie` defaults to on; music and ambience via PlaySound | `/nomovie /nosound /nomusic`, `mainmenu_bgmovie = false`, `movie.nologo` |
| Preferences | The user's Game.prefs (profile, skin, options) | A scratch Game.prefs via `/prefs`: profile `harness`, `MenuTutorialPrompt = true`, `antialiasing 0` (GetRenderTargetData cannot read a multisampled surface), `vsync 0`, `bloom_render 0`, `ui_scale 1.0`, language `us`. main.lua forces the `uef` skin (CreateUI), so the random-skin path is not taken |
| Mounted content | init_faf.lua mounts the user's vault and LOCALAPPDATA, and reads fa_path.lua, which the FAF client rewrites per game | The launcher's init copy: no vault, LOCALAPPDATA in scratch, a per-invocation snapshot of fa_path.lua |
| Committed probes | `FAF_TOGGLE_DIR`, `FAF_DUMP_FRAMES` and other `FAF_*` switches | Refused at start, by the launcher and in the process |
| Heap addresses | Lua hashes objects by pointer (m6u-CRIT, LuaObject.cpp:3563-3578); ASLR moves the heap per run | Nothing to pin on the menu: three runs, and runs at different paces, give the same bytes |

Evidence (2026-10-07, Debug|Win32, the system `d3d9.dll`):

- Gate 0: three consecutive runs at 1280x720 give RGB hashes 60 `9ab312ba8320f018`, 300
  `a901156c8b13d860`, 900 `fea3146c72876a69` in each; the BMP files are byte-identical (sha256 60
  `9d9cd244...`, 300 `06ae5c2f...`, 900 `f46d94d7...`). No window was shown, no activation attempted,
  no input message arrived, and the foreground window was the same before and after every run.
- Pacing does not leak in: `/galpace 0` (900 frames in 15 s) and `/galpace 12` (84 s) give the same three hashes.
- What the clock pin fixes: frames 10-30 (the brackets dropping in) with `/galnopins` differ between
  pace 30 and pace 0 (up to 58711 pixels, max delta 255), with the pins they are identical; from frame
  45 the menu has settled and both agree, so frames 60/300/900 alone would not show it.

A note on the window size: with `/windowed 1280 720` the client area is 1280x729, not 720, because
WSupComFrame sets a minimum window size of 1024x768 (`SetSizeHints(wnd_MinDragWidth,
wnd_MinDragHeight)`, WxRuntimeTypes.cpp:1237); the engine logs "Unable to set requested size". The
back buffer is the head size, 1280x720, and the UI is laid out for the 729-row client
(CUIManager.cpp:263-271), so its bottom rows fall outside the capture. That is the engine's own
behaviour for this window size; a height of 768 or more gives a client equal to the back buffer.

## The sandbox

Installed from a static initialiser, before WinMain, because the first offenders run early. All of
it is in-process; nothing is installed system-wide.

| Mechanism | What it stops | Why |
|---|---|---|
| Import redirect `ShowWindow` (top-level, not SW_HIDE) | Showing the frame | wx's `Show(true)` is ShowWindow + BringWindowToTop (CScApp.cpp:1427, 1480) |
| `BringWindowToTop`, `SetForegroundWindow`, `SetFocus` | Taking the foreground | |
| `SetWindowPos`, `MoveWindow` (top-level) | Showing (`SWP_SHOWWINDOW`), activation, on-screen placement | Always `SWP_NOACTIVATE`; position beyond the virtual screen |
| `ClipCursor` | Freeing or confining the desktop's cursor | `SC_ToggleCursorClip 0` runs at every start (options.lua), CScApp::Destroy releases the clip (CScApp.cpp:1166) |
| `SetCursorPos`, `SetCapture`, `keybd_event` | Moving the cursor, grabbing the mouse, injecting keys | |
| `SetWindowsHookExW` (global or low-level) | The WH_KEYBOARD_LL hook of WIN_AppExecute (WinApp.cpp:2735) | It would run in this process for every key pressed on the machine |
| `SystemParametersInfoW` (SPI_SET*) | Screensaver flag (CScApp.cpp:979, 1143), sticky/toggle/filter-key hotkeys (WinMain.cpp:264-285) | System-wide settings |
| `MessageBoxW/A`, `DialogBoxParamW` | Error boxes (WIN_OkBox is MB_TOPMOST, WinApp.cpp:3570), the crash dialog (gpg::Die) | The text goes to the summary and the run ends (exit 12) |
| `CreateProcessW/A`, `ShellExecuteW/ExW`, `ExitWindowsEx`, `MessageBeep` | Launching anything, logging off, sounds | |
| `SetThreadPriority` above normal, `GetProcAddress("SetPriorityClass")` | Raising priority | init_faf.lua:31-41 asks for HIGH_PRIORITY_CLASS; TIME_CRITICAL threads would outrank a game |
| WH_CBT (UI thread) | Every activation of our windows; top-level windows are created without WS_VISIBLE and off-screen | |
| WH_GETMESSAGE (UI thread) | Keyboard, mouse, touch, pointer, hotkey messages (to WM_NULL) | |
| Monitor after every frame | Any visible window or the foreground window belonging to the process | Hides it, writes the summary, exit 11 |
| `SetErrorMode`, debug-CRT report mode | WER and CRT assert windows | |
| Priority | Below-normal CPU class, below-normal GPU scheduling class (`D3DKMTSetProcessSchedulingPriorityClass`) | A game in the foreground wins both |
| Stall reporter | A hung run with nothing to show | Every 15 s without a frame it logs the UI thread's stack |

The launcher adds: `STARTUPINFO` with SW_HIDE and no busy cursor, below-normal priority at creation,
a job object that kills the process with the script, a timeout, and its own 50 ms monitor (visible
windows, foreground, priority class). It also records how many distinct cursor positions it saw
during the run, as evidence that pointer input cannot reach the harness.
