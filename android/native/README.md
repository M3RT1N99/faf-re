# The Android native build

The engine for Android, and the game client on top of it, without changing a file of the engine.
The engine's own tree (`src/`, `cmake/`, the root `CMakeLists.txt`) stays as the repository has
it, and so does its Windows build. Everything Android needs beyond it is in this folder.

The client is the engine's own POSIX client, the one `cmake/FafLinuxClient.cmake` builds for Linux:
the window, the input and the sound on SDL3 (`src/platform/sdl`, `src/platform/posix`), the main loop
on the application shell (`src/platform/shell`), the renderer on Vulkan. This build makes the same
files with the NDK, with the same dependencies at the same pins, and adds what Android needs and
the Linux client does not.

```sh
cd android/native
cmake --preset android-arm64          # or android-x64 for the emulator
cmake --build --preset android-arm64  # headless, faf_sim_math_tests, fafgame, faf_dxc and the tests (Tests)
```

The outputs land in `build/android-<abi>/bin`, where the app (`android/app`) takes them from.
The engine's dependencies come first, once, as for any engine build:
`cmake -DFAF_DEPENDENCIES_ONLY="WildMagic3p8;zlib-1.2.3;vulkan;ffmpeg" -P cmake/FafDependencies.cmake`
(from the repository root). The client's own (SDL3, FAudio, FreeType, msdfgen, DirectX-Headers,
DirectXTex, DirectXMath, stb, Dear ImGui) come with the configure, from the same script.

## How it fits together

`CMakeLists.txt` adds the repository's root project as a subproject. With an Android toolchain
that project builds the headless simulation and the sim-math tests (`cmake/FafLinux.cmake`), as
the repository's own `android-*-release` presets do. On top of it come the client's targets:

| Target | What |
|---|---|
| `faf_sim_patched` | The simulation's files that `patches/` changes, compiled from their patched copies with `faf_sim`'s settings; `libfafgame.so` takes `faf_sim`'s objects but those |
| `faf_client` | The client's files as `cmake/FafLinuxClient.cmake` chooses them, the POSIX client's platform files (`Win32WindowPosix`, `XactOverFact`, `WindowSdlPosix`, `SdlMovieSink`, `GdiOverFreeType`) and its entry (`ClientMainPosix`), with this folder's `src` |
| `faf_client_dxc` | `platform/posix/dxc/DxcGlue.cpp`, the one file on DXC's own headers |
| `faf_client_imaging`, `faf_directxtex_android` | The engine's texture codec on DirectXTex's CPU code and stb, as the Linux client builds them |
| `faf_movie_player` | The engine's movie player, whose decoder is on FFmpeg's headers |
| `SDL3-static`, `FAudio-static`, `faf_imgui_sdl`, `faf_freetype`, `faf_msdfgen` | The client's libraries, by their recipes (`dependencies/SDL3`, `dependencies/FAudio`, `cmake/deps`) |
| `FFmpeg::FFmpeg` | FFmpeg's libraries (`ffmpeg/`, see Sound and movies) |
| `faf_dxc` | `libdxcompiler.so`, DXC built from source (see The shader compiler) |
| `fafgame` | `libfafgame.so`: all of the above, which the app's game activity loads (card 5 of the app) |
| `faf_movie_tests` | The movie player and decoder on the game's movies (`tests/movie`) |
| `faf_griff_tests` | The touch scheme Griff's rules (`tests/griff`), linked statically |
| `faf_net_tests` | The engine's network transport on `platform::net` (`tests/net`), with the simulation's objects, linked statically |
| `faf_shell_tests` | The patched shell's rule for a key-down a handler took (`tests/shell`), linked statically |

The client's objects are compiled with the simulation's settings (`faf_sim_settings`) and the engine's
directory-wide definitions (`FAF_RETAIL_LAYOUT`, which decides the legacy containers' layout), as they
link with the engine's own objects.

`libfafgame.so` exports what Java and SDL's activity look up in it (`exports.map`): `JNI_OnLoad`, which
registers SDL's native methods, SDL's `Java_*` methods, the engine's `SDL_main` and the app's
`FafGameMain`. The rest stays local.

### What Android adds

| File | What |
|---|---|
| `src/app/GameMainAndroid.cpp` | The app's entry, `FafGameMain`, which the game's activity names to SDL: SDL's hints for a game on a phone (the program goes on in the back; landscape; no screen keyboard by itself), the program's path as `argv[0]`, the least display mode for a screen below 1024 x 768, the engine's main thread with the Windows program's 4 MB stack, and the process's end with the program; the report's lines (`FAF_GAME_REPORT`), with the engine's lines a tester needs: the GPU, each swap chain, how BC textures are sampled, the sound's output or its failure to start |
| `src/app/ScreenKeyboardAndroid.cpp` | The phone's keyboard for FAF's text fields: SDL's, shown when a finger's press lands on a text field (the lobby's chat, a profile's name, the console's line) or FAF's Menu tab asks for it; it asks for plain keys (`TYPE_NULL`), which a keyboard that sends keys for them types as a keyboard does (one that commits text instead, as AOSP's does for most characters, still closes the console that its grave accent opens: `patches/README.md`, Shell.cpp), Back hides it before Back is Esc again, Enter goes to the game and then hides it. It watches SDL's events through the window layer's hook for the developer windows, which Android, with its one window, never makes |
| `src/app/SoundAndroid.cpp` | The game's sound follows the activity and Android's audio focus, which the activity asks for: SDL's own pause of its output (`AAUDIO_PauseDevices`, `OPENSLES_PauseDevices`: the stream under the game's sound and a movie's, and with their patch the streams SDL builds while it lasts) in the back and while another app has the focus, on SDL's main thread; the report says when it pauses and when it plays again |
| `src/app/GameJniAndroid.cpp` | The natives of the app's `Game` class on SDL's activity: the activity's front and its audio focus; for `NetworkGameService` whether a UDP connector has its socket open (`NET_OpenUDPConnectors`, the patched `CNetUDPConnector.cpp`) and whether a game is playing (`UI_GamePlaying`); the app's lines in the report |
| `src/app/UiPackageAndroid.cpp` | The UI package's globals (`../TOUCH_UI.md`) on SDL's activity until the touch schemes come back: the page, the fingers' owner and the HUD's factor go nowhere yet, the Menu tab's Keyboard brings the keyboard, and Griff is off (`FafreGriffWanted`), so its Lua leaves FAF's UI as it is; `src/host` registers these on the earlier host |
| `src/gal/VulkanSurfaceAndroid.cpp` | The Vulkan backend's platform unit (`VulkanSurface.h`) on SDL, in place of `VulkanSurface.cpp`: the surface from `SDL_Vulkan_CreateSurface`, and for the patched `DeviceVulkan.cpp` the surface's coming and going (see The surface) |
| `src/gal/BlockCompressionAndroid.cpp` | BC1-3 on the CPU, with WARP's decode, for GPUs without BC (`patches/README.md`), and an encoder after D3DX's |
| `src/net/NetlinkPeersAndroid.cpp` | The in-engine peer links and native networking, off: netlink's libraries (libjuice, libdatachannel and theirs) are not built here yet |
| `include/DirectXTexAndroid.h` | `aligned_alloc` for DirectXTex below Android 9: this build is for API 26 (`cmake/toolchains/android-pins.cmake`) |

`src/host` (`GriffAndroid.cpp`, `MenuFingers.cpp`) is the touch scheme Griff's half in the engine on the
earlier host (the Win32 message layer of release 0.14). It is not built: it comes back on SDL's input, and
`src/app/UiPackageAndroid.cpp` goes then.

### The program's start

SDL's activity (`org.libsdl.app.SDLActivity`, whose Java sources the app takes from the same SDL release)
loads `libfafgame.so` and calls the main function the app's activity names, `FafGameMain`, on a thread of
its own, with the activity's arguments. That runs the engine's entry, `SDL_main`: `ClientMainPosix.cpp`'s
`main` with SDL's `SDL_main.h` (`patches/`), on a thread with the Windows program's stack. When the program
returns, the process ends, as a Windows program's does.

The command line is the activity's: the program's path in the game files' `bin` folder (the engine takes
its install's folder from it), the game's options, and `/fullscreen <width> <height>` with the size of the
surface's buffers, the picture's. HOME and TMPDIR are the activity's too: the engine's folders
(`SHGetFolderPath`) are under HOME.

Android destroys a stopped activity when it wants the memory, and at once under the developer option *Don't keep
activities*. The program goes on then, as a Windows program does without its window, and the next activity shows it:
`GameActivity.onDestroy` skips SDL's quit while the program runs, its `onCreate` gives SDL back the program's thread and
the cursors the program made, which SDL forgets for each new activity, the program sets
`SDL_HINT_ANDROID_ALLOW_RECREATE_ACTIVITY`, without which SDL's activity ends the process, and the patched
`SDL_android.c` keeps the lifecycle's mutex and semaphore that the program's thread waits on (`patches/README.md`).
Without a network game's service Android freezes the process, now a cached one, until the next activity comes. A
launcher's start while the program runs ends at once (`RUNNING`, `../README.md`): SDL's `onCreate` would take SDL's
statics for its activity, the surface the program draws on among them, so `GameActivity.loadLibraries` stops it at that
first step, which another SDL release must still have, and its `onDestroy` lets go of nothing of the activity that shows
the program, its HID manager included.

### The surface

The engine's window is SDL's; on Android it is the activity's surface. SDL takes a new `ANativeWindow` when
the activity's surface comes (`SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER`) and lets go of it when the surface
goes, as the activity goes to the back. With `SDL_HINT_ANDROID_BLOCK_ON_PAUSE` off the program goes on in
the back: SDL reports the window minimized, and the engine draws nothing. The patched `DeviceVulkan.cpp`
lets go of a Vulkan surface that is no longer current (another `ANativeWindow`, or the same at another
size) and takes the next once there is one. Between the two, Android's own swap chain (libvulkan's, the
same on every device) answers `VK_ERROR_SURFACE_LOST_KHR` for a surface whose window has gone.

The surface's buffers have the picture's size, which the app chooses (`PictureSize`, `GameSurface`) and the
compositor scales to the screen: SDL's window and the engine's display mode have that size, and the activity
hands SDL the fingers and the mouse in the picture's pixels.

While the activity is in the back, SDL's event pump waited up to 100 ms for the activity's events each time
it was asked (`Android_PumpEvents`), and the engine's loop asks at each pass and every 4 ms of its waits: the
skirmish's simulation ran at half speed there (30 s of 61 lost). The patched `SDL_androidevents.c` waits only
as long as it is asked to; the loop paces itself in the back as on the PC, waiting for the next beat, and the
simulation keeps real time there (on the emulator nothing lost in 61 s, the process at 8 to 12 % of a core).
Outside a game nothing paces the loop in the back but its waits, which look at the window system every 4 ms: about
470 wake-ups a second of the main thread, and some 300 more of FACT's threads, which a paused output does not stop.
On the emulator that cost 2.8 % of a core in the main menu (the previous app, which Android had not frozen) and
1.4 % in a LAN lobby (with the network service), against 9 to 19 % in a LAN game, so the pump takes no wait of its
own while paused. What the wake-ups cost a phone's battery, in a lobby under the service's wake lock for up to 30
minutes, is still to be measured on a phone (`dumpsys batterystats`).

### The shader compiler

The Vulkan backend compiles the effects with DXC, through the POSIX client's `DxcGlue.cpp`, which loads
`libdxcompiler.so` from the library path (the app's native libraries) and preprocesses the effects with
DXC's preprocessor, as on Linux. Microsoft builds DXC for Linux x86_64 only. `cmake/FafAndroidDxc.cmake`
builds `libdxcompiler.so` from the sources of the release the engine downloads (v1.9.2609), pinned by
SHA256 and cached in `dependencies/.downloads`: first its table generators for the build host, then the
library for the ABI (target `faf_dxc`, a few minutes the first time). The app packages it with the engine's
libraries and its licences in the assets (`licenses/`).

### Sound and movies

The XACT engine is the POSIX client's: FACT, FAudio's reimplementation of XACT 3, behind the engine's
interfaces (`platform/posix/XactOverFact.cpp`), FAudio 26.10 with the engine's patch over SDL3's audio,
which is AAudio on Android. Movie sound is an SDL3 audio stream (`platform/posix/SdlMovieSink.cpp`). Both
pause in the back and while another app has the audio focus (`src/app/SoundAndroid.cpp`): SDL pauses its output
on Android only where it holds the program in the back, which this build turns off.

FFmpeg is the engine's submodule, unchanged, built by `ffmpeg/CMakeLists.txt` with the NDK's clang
from what its configure writes for each ABI, checked in under `ffmpeg/<abi>/`. `ffmpeg/generate.sh`
writes those (Git Bash, minutes per ABI) with the components of `dependencies/ffmpeg/faf/generate.sh`;
when upstream moves the submodule, the configure stops and says to run it. This configuration is LGPL
2.1 or later and linked statically: the sources are the submodule's, this repository builds the
library anew, and the licence's text goes into the app's assets. The Linux client runs FFmpeg's own
configure and make, which an Android cross build on Windows cannot.

### The network

The game's own lobby, its games and the discovery of LAN games run on the engine's UDP transport on
`platform::net` (`src/platform/posix/NetPosix.cpp`), as on Linux; `patches/README.md` has the lines of the
transport that change. The app holds a `MulticastLock` while the activity is in front, without which
Wi-Fi hands a phone no broadcasts. The in-engine peer links are off (`src/net`). While a UDP connector has its socket
open, a network lobby's or game's, the app's `NetworkGameService` keeps the process's network in the back
(`../README.md`, Network games): the patched `CNetUDPConnector.cpp` counts those connectors from its thread's start to
its deletion (`NET_OpenUDPConnectors`), which the service asks through `Game.networkInUse`.

GPGNet, the game's line to its launcher, is the engine's TCP stream on `platform::net` (`CNetTCPBuf`), built as on
Linux. A launcher in the app (`../README.md`, Game) listens on the loopback and starts the game with
`/gpgnet 127.0.0.1:<port>`; the game connects to it, says `GameState Idle`, and opens FAF's lobby on the launcher's
`CreateLobby`. The other way, in which the game listens and launches its GPGNet side through `/bin/sh`, fails at once,
as an app starts no programs (`patches/`). A game started with `/gpgnet` alone whose launcher cannot be reached, or
closes the connection before its `CreateLobby`, says so in a box and ends, where it stayed on a black screen before
(`patches/README.md`, `CGpgNetInterface.cpp` and `PlatformPosix.cpp`). Upstream's stream counts too few bytes for a
read of 2 KB or more that it takes from the socket itself, so an argument from the launcher with 2 KB or more left
after what the stream's buffer holds of it ends the connection (`CommunicationError`, `tests/net`): FAF's messages to
the game are shorter, an ICE server list or a peer's ICE message for the game's own links (`/nativenet`) need not be.

## Tests

`faf_griff_tests` (`tests/griff`) checks the rules of the touch scheme Griff (`include/Griff*.h`: the
draft, the pins, the handle's place, the reach of a tap, the undo), the choice of the small control a
finger's tap meant (`include/TapTarget.h`) and the sharing out of a finger's drag between a list and the
page (`include/ListDrag.h`). It needs nothing of the engine and is linked statically, so the same program
runs on the emulator and on a Linux host; the app's workflow runs the arm64 build on an arm64 runner.

```sh
adb push build/android-x64/bin/faf_griff_tests /data/local/tmp/
adb shell "chmod 755 /data/local/tmp/faf_griff_tests && /data/local/tmp/faf_griff_tests"
```

`faf_movie_tests [--frame N out.raw] movie.sfd ...` (`tests/movie`) decodes real movies with the
engine's decoder and prints one line for each: size, frames, samples, a CRC over all pictures and one
over all audio, the time and the peak memory. The audio's CRC is the same on both ABIs; the pictures'
differ by the rounding of NEON's and C's IDCT (at most 3 levels, about 45 dB). With `--play SECONDS` it
runs the engine's player by the clock with a reader that stalls now and then: it fails when the
player gives up, and prints how long the reader had to wait.

`faf_net_tests` (`tests/net`) runs the engine's socket events (`platform::net`, `src/platform/posix/NetPosix.cpp`) on
real sockets over the loopback, with bionic, and the engine's datagram socket and UDP connector on them: the patched
transport's end, its count of open connectors (what the app's `NetworkGameService` goes by), a network that goes and
comes back, what the protocol puts on the wire and what ending a game or a replay leaves. GPGNet's TCP socket too: the
game's connect to its launcher's listener, what goes either way, the launcher's close, a connect with no listener, and
the read that upstream's stream counts short (The network). It links the simulation's objects as the game does,
statically, and the app's workflow runs the arm64 build on an arm64 runner. Where
`platform::net` keeps Winsock's behaviour less strictly than the earlier host's layer did, the test says so: it does not
refuse to select a descriptor that is no socket, and a closed socket's port is free once the poll thread has let go of
it, a few milliseconds after `CloseSocket` (on the emulator at most 24 ms in three runs), not at once.

```sh
adb push build/android-x64/bin/faf_net_tests /data/local/tmp/
adb shell "chmod 755 /data/local/tmp/faf_net_tests && /data/local/tmp/faf_net_tests"
```

`../tools/gpgnet_host.py` is a launcher's GPGNet side on the PC, for the game's connect (The network): it listens on
127.0.0.1, prints what the game sends, answers its `GameState Idle` with `CreateLobby`, and its exit code says whether
the game reached its lobby. `adb reverse` takes the game's loopback port to it. With the port reversed and the script
not running, adb takes the game's connection and closes it, the case of a launcher that goes before its lobby; without
the reverse nobody listens, and the connect is refused.

```sh
adb reverse tcp:7237 tcp:7237
python -I android/tools/gpgnet_host.py --port 7237
adb shell am start -S -n io.github.m3rt1n99.fafre/.GameActivity --esa io.github.m3rt1n99.fafre.extra.LAUNCHER_ARGUMENTS /gpgnet,127.0.0.1:7237
```

`faf_shell_tests` (`tests/shell`) pins the patched shell's rule for a key-down a handler took (`patches/README.md`,
`Shell.cpp`): it gets no char, neither the shell's own for Enter nor the window system's text that comes before the key's
key-up; a skipped key-down gets both; a key-up ends the rule. It runs `ShellApp::Dispatch` on the patched copy with a
window layer of the test's own (`platform/Window.h`) in place of SDL's, and is linked statically.

## Patches

`patches/<path>.patch` is a `git diff` of the engine file `<path>`, or of a dependency's file under
`dependencies/` (SDL's, which the configure has fetched by then into `FAF_DEPENDENCIES_DIR`, `dependencies/`
unless a cache variable says otherwise). At configure time
`cmake/FafAndroidOverlay.cmake` copies the file to `build/android-<abi>/overlay/<path>` and
applies the patch to the copy. A patched source replaces the original in the targets, the
dependency's in its own target, and a patch of a dependency's that replaces none of that target's sources stops
the configure; a patched header is found first through the overlay folder. A copy
finds the original's neighbours for its quoted includes. The repository's file is never touched.

Each patch is in `patches/README.md` with the reason. When the engine changes a patched file so
that its patch no longer applies, the configure stops and names the file. To make the patch again,
change a copy of the engine's file (the overlay's copy is the patched one) and diff it against the
file as the repository has it, with LF line ends:

```sh
git show HEAD:<path> > a/<path>         # the engine's file, as the repository keeps it
cp <changed copy> b/<path>
git diff --no-index --no-prefix a/<path> b/<path> > android/native/patches/<path>.patch
```

Keep a patch to what Android cannot do another way: what the engine lacks on Android belongs in
`src/` and `include/` here, a patch only where the engine's own code must behave differently.
