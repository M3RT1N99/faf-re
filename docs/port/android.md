# Android

One APK, `io.github.m3rt1n99.fafre` ("FAF (faf-re)"), arm64-v8a, Android 8.0 (API 26) or newer:

- **Launcher** (Java, `port/android/src/java`): checks and imports the game data, holds the launch
  options, starts the game, shows the result of the last run and the logs.
- **Native runtime** (`libfaf_android.so`, `port/android/src`, built on `port/native`): runs FAF's
  unmodified `init_faf.lua`, mounts the game data the way the engine does and, for milestone M1,
  shows a main-menu background from the user's data through Diligent (Vulkan, OpenGL ES fallback).
  It is not the game yet; see the [roadmap](android-roadmap.md).
- **Headless replay runner** (`libfafrunner.so` + `libfafengine.so` + `libfafarenaprobe.so`,
  [port/engine/runner](../../port/engine/runner/README.md)): the engine's sim, which the launcher's
  [replay test](#replay-test) starts as a process of its own to play a FAF replay to its end
  (milestone M3c on the phone, without a PC). Since 0.4.1 also an `-O2` build of the pair
  (`libfafrunner_o2.so` + `libfafengine_o2.so`) for the speed experiment.
- **Device probe** (`libfafdeviceprobe.so`, [port/deviceprobe](../../port/deviceprobe)): reports what
  the GPU drivers offer the graphics port (release 0.4.1, [Device probe](#device-probe)).
- **Menu replay** (release 0.5.0, [Menu replay](#menu-replay)): FAF's main menu with its opening
  animation, drawn by the port's own Diligent `gpg::gal` backend over Vulkan in GameActivity, from a gal
  call stream recorded on the PC (`assets/galplay/menu.galtrace`, which holds references to game files,
  not their contents) and the user's own game data.

No game data is in the APK or the repository. The user's own *Supreme Commander: Forged Alliance*
files and FAF's files are put on the device separately; [gamedata.md](gamedata.md) lists exactly
which ones.

## Prerequisites (Windows host)

| Tool | Version | Notes |
| --- | --- | --- |
| Android SDK | platform `android-35`, build-tools `35.0.1`, NDK `29.0.14206865`, platform-tools | Default `C:\Android\sdk`; otherwise set `ANDROID_SDK_ROOT` or pass `-AndroidSdk`. `sdkmanager "platforms;android-35" "build-tools;35.0.1" "ndk;29.0.14206865" platform-tools` |
| JDK | 17 or newer (the oldest one found is used) | Found through `JAVA_HOME` or the usual install folders (Temurin, Microsoft, Zulu, Android Studio's `jbr`); `-Jdk` to choose. |
| CMake | 3.22 or newer | e.g. `python -m pip install cmake`; `-CMake` to choose. |
| Ninja | any | Ships with Visual Studio 2022 ("C++ CMake tools") and the SDK's cmake package; `-Ninja` to choose. |
| Python | 3.x | Writes the uncompressed APK entries. |
| git | any | Bootstrap, and the APK's versionCode. |
| `dependencies\LuaPlus_Build1081` | vendored | The same LuaPlus archive the desktop build uses (see `dependencies/patches/luaplus_build1081_faf_required.md`). |

Both Windows PowerShell 5.1 and PowerShell 7 run the scripts. From a plain prompt, use
`powershell -ExecutionPolicy Bypass -File <script>`.

## Bootstrap

```powershell
powershell -ExecutionPolicy Bypass -File scripts/port/bootstrap_android.ps1
```

Checks out DiligentCore at the pinned revision `1436d1fea00763178ae835fd36b065efb31d96e5` into
`dependencies\DiligentCore` with the submodules the Android renderer needs, and pre-fetches the
abseil-cpp commit Diligent pins (read from `ThirdParty/abseil-cpp/CMakeLists.txt`) into
`dependencies\abseil-cpp`, so configuring needs no network. Running it again only does what is
missing. `-Check` verifies the state without network access; the build calls it.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File scripts/port/build_android.ps1
```

Result: `output\android\faf-re-android-<versionName>-arm64-v8a.apk`. The script prints the APK
path, size, the signing certificate's and the APK's SHA-256 and the build ids of the libraries.

| Option | Effect |
| --- | --- |
| `-Configuration Release\|Debug` | Release (default) or Debug: unoptimized native code, debuggable APK. |
| `-SkipNative` | Launcher only, no native build: `faf-re-android-<versionName>-nonative.apk`. For UI work; the game cannot start from it. |
| `-Clean` | Deletes the build directories first. Needed after switching NDKs. |
| `-AndroidSdk`, `-Ndk`, `-Jdk`, `-CMake`, `-Ninja`, `-Python` | Tool locations, when the defaults do not find them. |
| `-Keystore <file>` | Signing key (default below). |
| `-NativeLibrary <libfaf_android.so>` | Package a library built elsewhere (CI) instead of building `port/android`. |
| `-RunnerDirectory <dir>` | Package the prebuilt, unstripped `faf_headless_runner`, `libfafengine.so` and `libfafarenaprobe.so` of this directory (for a release: the binaries the reference runs were measured with, see [Replay test](#replay-test)) instead of building them. Its `libfafdeviceprobe.so`, if present, is the device probe. |
| `-RunnerO2Directory <dir>` | Package the prebuilt, unstripped `-O2` `faf_headless_runner` and `libfafengine.so` of this directory (`build_runner.py --opt O2`) as `libfafrunner_o2.so` and `libfafengine_o2.so`. A build that builds its own runner builds this pair too; a Release build with `-RunnerDirectory` but without this option stops. |
| `-SkipOptimizedRunner` | Leave the `-O2` pair out; the launcher then greys out *Optimised engine (-O2 build)*. |
| `-DeviceProbe <file>` | Package this unstripped `libfafdeviceprobe.so` (`build_runner.py --deviceprobe`) instead of the one in `-RunnerDirectory` or the runner build. |
| `-SkipDeviceProbe` | Leave the device probe out; the launcher then greys out **Device probe**. A Release build without a probe stops unless this is given. |
| `-MenuTrace <file>` | The galtrace the [menu replay](#menu-replay) plays (release 0.5.0). Default: the one `*.galtrace` in `buildstage\traces` (the `menu*` one when there are several). It must be a complete format version 2 trace with game-file references; a version 1 trace (every payload embedded, game files included) is refused, and so is one with an embedded payload that looks like a game file. A Release build also needs the PC's Diligent-Vulkan hash of every readback frame in it, no home directory in its header, and a `libfaf_android.so` that has the menu replay mode ([The trace](#the-trace)). |
| `-SkipMenuTrace` | Leave the menu trace out; the launcher then greys out **Menu replay**. A Release build with native code stops without a trace unless this is given. |
| `-AllowUnreferencedRunner` | Package engine/runner pairs whose build ids are not in the reference table (a warning instead of an error); development builds only. |
| `-SkipRunner` | Leave the replay runner, the `-O2` pair and the probe out (no `dependencies\WildMagic3p8` needed); the replay test then says the runner is missing. `-SkipNative` implies it. |
| `-NoReleaseStage` | Do not copy the APK and the unstripped libraries into `buildstage\releases\android-v<versionName>\` (Release builds do by default). |
| `-DryRun` | Resolve the tools and print every command without running anything. |

What it does:

1. **Native**: configures `port/android` with CMake, Ninja and the NDK toolchain
   (`ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-26`, `ANDROID_STL=c++_static`) in
   `buildstage\android-native` and builds the target `faf_android`. CMake is re-run only when its
   arguments change. The library is checked before it is packaged: it must be built for the ABI,
   export `ANativeActivity_onCreate`, need no library Android does not provide (Diligent and the
   C++ runtime are linked statically), and have 16 KB aligned segments for 16 KB page devices. A
   stripped copy goes into the APK; the unstripped one stays in
   `buildstage\android-apk\symbols\arm64-v8a\` for symbolizing crashes:
   `adb logcat | ndk-stack -sym buildstage\android-apk\symbols\arm64-v8a`.
2. **Runner**: `scripts/port/build_runner.py --abi <abi> --probe --deviceprobe --out
   buildstage\android-apk\runner` and `--opt O2 --out buildstage\android-apk\runner-O2` (seeded
   from `buildstage\runner\<abi>`, so only what changed compiles; it needs the local, gitignored
   `dependencies\WildMagic3p8`, and the device probe links the glslang libraries of step 1's build),
   or the binaries of `-RunnerDirectory`, `-RunnerO2Directory` and `-DeviceProbe`. Stripped copies go
   into the APK as `lib/<abi>/libfafengine.so`, `libfafrunner.so` (the executable
   `faf_headless_runner`: the installer only extracts `lib*.so` names), `libfafarenaprobe.so`,
   `libfafengine_o2.so`, `libfafrunner_o2.so` and `libfafdeviceprobe.so`, after these checks: built
   for the ABI; LOAD segments aligned to at least 16 KB; NEEDED only `libz`, `liblog`, `libm`,
   `libdl`, `libc` (the probe opens Vulkan, EGL and GLES with `dlopen`); the engines and the arena
   probe export `faf_headless_main`; both runners have the interpreter `/system/bin/linker64` and
   export the malloc family and `lowarena_enabled`; the device probe has that interpreter too; the
   build id survives the strip. The unstripped files stay next to `libfaf_android.so` in
   `symbols\<abi>\`, and the script says whether the build ids of both engine/runner pairs are the
   ones the reference table names. A Release build stops there when one of them is not in
   `port/android/assets/replay_refs.json` (a chain is only comparable between identical binaries):
   package the runner sets the references were measured with (`-RunnerDirectory`,
   `-RunnerO2Directory`), or regenerate the table from new reference runs
   (`run_runner_android.py --host-prefs none --write-refs`). `-AllowUnreferencedRunner` turns the
   error into a warning, for development builds only.
3. **Resources**: `aapt2 compile` + `aapt2 link` with the manifest, versionCode and versionName.
4. **Java**: `javac` (Java 8 bytecode against `android.jar`) and `d8 --min-api 26`.
5. **Package**: `classes.dex` and the assets are added *uncompressed*: `gamedata.json` (the
   manifest from `port/data`), `build.json` (version, commit, sha256 and build id of every library,
   which the replay test records), `replay_refs.json` (the reference table,
   `port/android/assets/replay_refs.json`, when it exists) and `licenses/*.txt`, the license texts
   of the third-party code in the libraries: `zstd.txt` (`port/third_party/zstd`, in the runners) and,
   from the local `dependencies/DiligentCore` checkout, `DiligentCore.txt` (Apache-2.0),
   `glslang.txt`, `SPIRV-Tools.txt`, `SPIRV-Headers.txt` (the SPIR-V grammar tables inside SPIRV-Tools
   and glslang), `SPIRV-Cross.txt` and `volk.txt` (libfaf_android.so; glslang, SPIRV-Tools and the
   grammar tables also in libfafdeviceprobe.so). A Release build refuses to go without them; the
   app's **Licenses** screen shows every file. Since 0.5.0 also the menu trace: an embedded helper
   (`galtrace_info.py`) reads the whole trace (header, every record up to the End record, the version 2
   payload definitions) and refuses it unless it is a complete version 2 trace with `PayloadRef` records
   whose embedded payloads do not look like game files (see [The trace](#the-trace) for the Release
   checks); it writes `assets/galplay/menu.json` (stored: size, sha256, the header metadata without the
   recorder's command line, the number and bytes of embedded, referenced, digest and composed payloads,
   the archives the references name, the readback frames, warnings), and `assets/galplay/menu.galtrace`
   is added *deflated*; build.json gets `menuTrace` (name, size, sha256, version, archives, readback
   frames). The summary's `Trace:` line gives the trace's size and its compressed size in the APK. The `lib/<abi>/*.so` entries are added *deflated*: the
   manifest sets `extractNativeLibs="true"`, because the replay test execs the runner from
   `nativeLibraryDir` (an app may only exec files the installer extracted there). The script reads
   that attribute and packages accordingly (`false` would need stored, page-aligned libraries and
   cannot carry the runner). Then `zipalign -P 16 4` and `apksigner sign`.
6. **Verify**: `apksigner verify`, `zipalign -c -P 16 4`, the compression of every entry,
   `extractNativeLibs` in the compiled manifest (`aapt2 dump xmltree`), and `aapt2 dump badging`
   must show the package, version, SDK levels, `native-code: '<abi>'` and the launcher activity.

A Release build then stages `buildstage\releases\android-v<versionName>\`: the arm64-v8a APK,
`lib<name>-<abi>.so` for each unstripped library (`libfaf_android`, `libfafengine`, `libfafrunner`,
`libfafarenaprobe`, `libfafengine_o2`, `libfafrunner_o2`, `libfafdeviceprobe`) and `build-<abi>.json`
(versionCode, commit, the APK's and the libraries' sha256 and build ids, the certificate). A crash
line from the phone is symbolized against these files.

For 0.4.1 the packaged sets are `buildstage\runner\r041b-pkg\<abi>` (runner, engine, arena probe,
device probe) and `buildstage\runner\r041b-pkg-O2\<abi>` (the second build: the runner's fast exit
and affinity hold, the probe's teardown after its result; the engines are byte-identical to the first
0.4.1 build, and the -O0 engine to 0.4.0's). 0.5.0 packages the same sets unchanged (the same build ids,
so the reference table still applies) and the menu trace from `buildstage\traces\menu.galtrace`, which
the build finds by itself:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/port/build_android.ps1 -RunnerDirectory buildstage/runner/r041b-pkg/arm64-v8a -RunnerO2Directory buildstage/runner/r041b-pkg-O2/arm64-v8a
```

`-Abi x86_64` builds the same APK for the SDK emulator's x86_64 image
(`faf-re-android-<versionName>-x86_64.apk`, with the x86_64 runner); it is never a release asset.

**Version.** `versionName` comes from `port/android/version.properties`. `versionCode` is
`git rev-list --count HEAD`, so a build of a newer commit always installs over an older one (a
shallow clone gives a wrong count; build from a full clone).

**Signing key.** `$env:FAF_ANDROID_KEYSTORE`, else `%USERPROFILE%\.android\debug.keystore` - the
standard Android debug key (alias `androiddebugkey`, passwords `android`), created on the first
build if missing. It deliberately lives outside every build directory: Android installs an update
only when it is signed with the same key as the installed app. Building on a second PC? Copy the
keystore there or point `FAF_ANDROID_KEYSTORE` at a shared copy.

## Put the game data on the device

The launcher shows what is present and what is missing (**Game data** section) and enables
**Start** once every *required* item is there. Three ways to fill it, which can be mixed:

### From the PC over USB (fastest)

Enable USB debugging on the device (Settings > About phone > tap "Build number" seven times, then
Developer options > USB debugging), connect it, and accept the "Allow USB debugging?" prompt.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/port/deploy_android.ps1
```

This installs the newest APK from `output\android`, starts the launcher once (the app creates its
data folder itself, which gives the folder the right owner), copies the game data, writes
`faf/fa_path.lua`, `faf/version.json` and `.deploy/deployed.json`, and makes the copied files
writable for the app. The SCFA install is found through the FAF client's
`C:\ProgramData\FAForever\fa_path.lua`, else through the Steam libraries. FAF files come from the
FAF client install and must match the manifest's checksums. Files already on the device with the
same size are skipped, so running it again after an interruption only copies the rest. The script
never deletes anything on the device.

| Option | Effect |
| --- | --- |
| `-Tier required\|recommended\|all` | How much to copy: main menu (0.6 GB), normal play (default, 4.9 GB), everything (9 GB). |
| `-Include <entry>[,<entry>]` | Add single entries, e.g. `scfa-movies`, `scfa-music`, `SupComDataPath.lua`. The plan lists all ids. |
| `-IncludeVault` | Also copy the FAF vault (downloaded maps and mods; can be many GB). `-VaultPath` overrides its location. |
| `-FafSource Download` | Fetch the FAF files from FAF's content server into `buildstage\faf-cache\<version>\` (resumable, checksum-verified) instead of using the local FAF client. Needed when the client already has a newer FAF version than the manifest. |
| `-ScfaPath <dir>`, `-FafPath <dir>` | Install locations when auto-detection is wrong. |
| `-DryRun` | Print the plan with sizes; no adb, no downloads, no copies. |
| `-StageDir <dir>` (`-Hardlink`) | Build the device layout in a folder on the PC instead (hard links instead of copies on the same volume). |
| `-Apk <file>`, `-NoInstall` | Install another APK, or none. |
| `-Launch` | Bring the launcher to the front when done. |
| `-Package <id>` | Application id (default `io.github.m3rt1n99.fafre`). |

Exactly one device must be connected (or `ANDROID_SERIAL` set). If the installed app was signed with
a different key the script stops and explains the one-time uninstall instead of doing it - see
[Troubleshooting](#troubleshooting).

### Import on the device

**Import SCFA folder…** picks the game folder with Android's folder picker (for example a copy on
the SD card or a USB stick) and copies the manifest's selection, with checkboxes for the
recommended and optional parts. It runs as a foreground service with a progress notification and
can be cancelled; files whose size already matches are skipped.

### Download the FAF files on the device

**Download FAF files (v3839)** fetches `init_faf.lua` and the `.nx2` archives from FAF's content
server (95 MB for the main menu, about 680 MB with the recommended set), resumes interrupted
downloads and checks every file's sha256. The SCFA files still have to come from the user's own
copy.

## Data layout

Everything lives below the app's external files directory,
`/sdcard/Android/data/io.github.m3rt1n99.fafre/files` (`<root>`):

```
<root>/scfa/                SCFA files (gamedata/*.scd, fonts/, sounds/, maps/, movies/, bin/splash.png)
<root>/faf/bin/             init_faf.lua (unmodified), optional SupComDataPath.lua
<root>/faf/gamedata/        FAF *.nx2 archives
<root>/faf/fa_path.lua      generated before every start
<root>/faf/version.json     FAF version and how the files got there
<root>/vault/maps, mods     the vault (custom_vault_path)
<root>/localappdata/        LOCAL_APPDATA: preferences, shader cache
<root>/documents/           Documents
<root>/logs/                faf_android_vulkan.log, faf_android_gles.log (+ .1.log: previous run), launcher.log, game.sclog, replaytest.log, menureplay.log
<root>/launch/status.json   result of the last run
<root>/replays/             replay test input: <id>.fafreplay as received, <id>.3764.scfareplay (converted), <id>.json (what is known about it)
<root>/runs/<run>/          one replay test, device probe or menu replay run (<run> = yyyyMMdd-HHmmss): result.json, meta.json, summary.txt, <n>-<step>.out (+ .log, .summary.json, .logcat.txt); a probe run also deviceprobe.json, deviceprobe-vulkan.png, deviceprobe-gles.png; a menu replay frame_<N>.png and .bmp, galplay.json, galplay.log, galreport.json, game.sclog (see Menu replay)
<root>/localappdata/galplay-cache/  the menu replay's SPIR-V and Vulkan pipeline caches (native side)
<root>/runner/home/         the runner's FAF_KNOWN_FOLDERS (emptied before every run; no Game.prefs)
<root>/.deploy/deployed.json  files placed by the deploy script or the import
```

The unpacked menu trace lives in the app's internal storage, not in the data root:
`/data/user/0/io.github.m3rt1n99.fafre/no_backup/galplay/menu-<sha256 prefix>.galtrace`.

[gamedata.md](gamedata.md#android-data-root) has the details, including the generated
`fa_path.lua`. The folder belongs to the app: Android deletes it when the app is uninstalled, and
the launcher asks Android to offer keeping it (`hasFragileUserData`).

## Launch contract

The launcher starts the native runtime with an explicit Intent to
`io.github.m3rt1n99.fafre/.GameActivity` (its own `:game` process) and a `String[]` extra `"argv"`
holding the game's command line, token by token, exactly as the desktop executable receives it:

```
/init <root>/faf/bin/init_faf.lua /nobugreport /log <root>/logs/game.sclog /renderer vulkan [/nomovie] [/nosound] [extra args]
```

| Option | Meaning on Android |
| --- | --- |
| `/init <file>` | Absolute path of the data-path script. Default `<root>/faf/bin/init_faf.lua`. Its folder is both `LaunchDir` and `InitFileDir`. |
| `/renderer vulkan\|gles` | Port-specific (FA ignores it). Default `vulkan`; when creating the Vulkan device or swapchain fails, the runtime falls back to OpenGL ES by itself. |
| `/log <file>` | Game log. The runtime also writes its own `<root>/logs/faf_android_<vulkan|gles>.log`, one per graphics backend, keeping the previous run as `.1.log` (mirrored to logcat tag `faf_android`; a native crash appends its backtrace there). |
| `/nomovie`, `/nosound` | Launcher settings; both on by default in M1. |
| anything else | Kept and logged, so the arguments the FAF client builds (`/gpgnet host:port`, `/mean`, `/deviation`, `/savereplay`, `/replay`, `/map`, ...) can be passed through later. |

Options compare case-insensitively and the first occurrence wins, as in the engine. Arguments typed
into the launcher's "extra arguments" replace the launcher's own value of the same option. In M1
`GameActivity` is not exported, so only the launcher can start it.

A second mode (release 0.5.0): with the String extra `"mode"` = `"menu-replay"` GameActivity replays
the menu trace instead of starting the game; its extras are in [Menu replay](#menu-replay).

## `status.json`

The runtime writes `<root>/launch/status.json` atomically (temporary file + rename) at every stage
change and at the end; the launcher shows it under **Last run** whenever it comes back to the
front.

```json
{"schema":1,"state":"running","stage":"frame","message":"","renderer":"vulkan",
 "mounts":785,"archives":20,"archiveEntries":38237,"scriptMs":812.5,"mountMs":190.2,
 "splash":"/textures/ui/common/menus02/background-paint01_bmp.dds","splashFormat":"DXT5","width":1024,"height":768,
 "timestamp":"2026-10-03T12:00:00Z","versionName":"0.3.0"}
```

| Field | Meaning |
| --- | --- |
| `state` | `loading`, `running`, `error` or `exited`. |
| `stage` | Where it is or where it failed: `args`, `datapath` (running `init_faf.lua`), `mount`, `splash`, `renderer`, `frame`. |
| `message` | Human-readable error, empty when fine. |
| `renderer` | `vulkan`, `gles`, or `none` before one was created. |
| `mounts`, `archives`, `archiveEntries` | Size of the mount table and the archives behind it. |
| `scriptMs`, `mountMs` | Time spent in `init_faf.lua` and in mounting. |
| `splash`, `splashFormat`, `width`, `height` | The background image that was found and decoded. |

On a fatal error the runtime writes `state: error` with the stage and message, logs it and closes
the activity; it never leaves a frozen black screen. Back, or a tap once the image is shown, ends
the run (`state: exited`).

## Replay test

The launcher's **Replay test** card runs the headless replay runner on the device by itself, so the
port's sim can be tested on a phone that is not attached to a PC. The user downloads a replay in the
phone's browser, hands it to the app, runs the test and sends back one zip.

### Getting a replay in

- **Open with / Share**: `https://replay.faforever.com/<id>` (FAF's public replay link, no login)
  downloads `<id>.fafreplay` (zstd). Opening that download with "FAF replay test" (or sharing it to
  the app) starts `ReplayInboxActivity`, which accepts `content:` URIs with the MIME types
  `application/octet-stream`, `application/zstd`, `application/x-zstd` and `application/x-fafreplay`.
- **Pick replay…**: the system file picker (`ACTION_OPEN_DOCUMENT`, any type).

Either way the file is copied into `<root>/replays/` at once (the read grant is temporary): at most
64 MB, recognised by its content (a `Supreme Commander v1.50.` header, or a JSON line followed by a
zstd frame or the legacy base64 body; anything else is refused with a message), and named after the
replay id in its header (else a sanitized display name: lower-case `[a-z0-9._-]`, runs of `-` and
of `.` collapsed to one, so `my..game.scfareplay` becomes `my.game`), never after what the sender
claims. Its sidecar `<id>.json` keeps the format, the sha256 and a few header fields (no
player names). Then the runner reads it: `libfafrunner.so /replayinfo <file>` (one JSON object: map,
featured mod and recorded version, beats, EndGame, ...) and
`/convertreplay <file> <id>.3764.scfareplay` (the .scfareplay the engine loads, version rewritten to
3764 as `scripts/perf/convert_replay.py` does). The app itself decodes nothing. While a test runs, a
replay that arrives through "Open with" is only copied and selected: the runner reads it when the next
test starts (one runner job at a time). The card shows the
selected replay with its map, length, recorded version and whether the reference table knows it.

Before a run the launcher checks the data: the required tier, the recommended FAF files and SCFA game
data (sounds and voices are not needed), the replay's map in `scfa/maps` or `vault/maps`, and
featured mod `faf`. Missing pieces block the replay (the self-test still runs); a replay recorded on
another FAF version, missing `mods.scd`/`skins.scd` and sim mods only give notes.

### What a run does

**Run test** starts `ImportService` with `ACTION_REPLAY_TEST` (the same dataSync foreground service
and partial wake lock as the imports: one job at a time, so no import rewrites data under a run). The
test never runs next to the game: a `:game` process whose run has ended (`status.json` says `exited`
or `error`; Android keeps the process cached) is ended first, as Start does; a game that is still
loading or running is left alone and the test is not started ("Not started: the game is running.
Close it first ..."). Then the job:

1. Makes `<filesDir>/r` a symlink to the data root. The engine lower-cases every directory it mounts
   and every file it opens, and `/storage/emulated/0/Android/data/...` has upper-case letters; every
   path the runner gets is on this alias (`/data/user/0/io.github.m3rt1n99.fafre/files/r/...`).
2. Creates `replays`, `runs/<run>`, `vault/{maps,mods}`, `scfa/{movies,sounds,fonts}` and an empty
   `runner/home`, and rewrites `faf/fa_path.lua` with the alias root (Start writes it back with the
   real root before every game start).
3. Runs the steps, each one `nativeLibraryDir/libfafrunner.so` exec'd with `ProcessBuilder` (a fresh
   process without ART, which the low-address arena needs), working directory `runs/<run>`,
   stdout+stderr into `<n>-<step>.out`, parsed live for the progress bar:

   | Step | Command | Passes when |
   |---|---|---|
   | Arena probe | runner with `FAF_ENGINE_LIB=<lib dir>/libfafarenaprobe.so` | exit 0 and `probe RESULT PASS` (41 placement checks) |
   | Arena capacity | the same with the argument `capacity` | exit 0 (fills the arena to its limit first) |
   | Engine load | runner without a replay | exit 1 with the usage line after `[lowarena] ...libfafengine.so at 0x...` (the engine loaded low, its static initialisers ran); the card names the library that loaded (`libfafengine_o2.so` with -O2) |
   | Replay | `/headlessreplay <alias>/replays/<id>.3764.scfareplay /init <alias>/faf/bin/init_faf.lua /log runs/<run>/<n>-replay.log /headlesssummary runs/<run>/<n>-replay.summary.json /headlessprogress 100` | exit 0 and "game over at beat N" |
   | Replay, second run | the same again (Advanced) | the same verdict, chain and game-over beat as the first run; otherwise the whole test is FAIL ("second run differs") |

   Environment: the app's, without `LD_PRELOAD`, plus `FAF_LOWARENA=1` (Advanced: `0`, "expected to
   crash"), `FAF_ENGINE_LIB=<lib dir>/libfafengine.so`, `FAF_KNOWN_FOLDERS=<alias>/runner/home` and
   `TMPDIR=<cache dir>`. No `Game.prefs` (the reference runs use `run_runner_android.py --host-prefs
   none`). Advanced also offers `/headlessinterlocked`, skipping the self-test and the two speed
   experiments ([below](#speed-telemetry-and-the-experiments-041)); **Self-test only** runs the first
   three steps.
   Every runner step ends with `[runner] exit <code>: _exit after flushing, ...` (0.4.1,
   `port/engine/runner/RunnerExit.cpp`): after the summary, the RESULT line, the engine log and the
   arena report are written, the runner ends with `_exit` instead of running the engine's static
   destructors, whose VFS teardown took 7-15 s on the emulator and about 10 s on the S22 after the
   RESULT line. The exit code is the same.
   Before the first step and again as each step starts, the run directory gets a provisional
   `result.json` (verdict `INTERRUPTED`, `in_progress: true`, the step that is running), `summary.txt`
   and, once known, `meta.json`, and the run is recorded as the last one. If Android ends the app
   during the run, that is what stays: see **INTERRUPTED** below.
4. After a step killed by a signal, saves `logcat -d -v threadtime -b main,system,crash` since the
   step started (the app's own lines) as `<n>-<step>.logcat.txt`. The runner's crash handler prints
   `[runner] CRASH ...` lines (signal, fault address, offsets in `libfafengine.so`, build ids) into
   the step's output. The device probe's step saves it too when the probe exits non-zero or a section
   crashed, hung or crashed in its driver teardown (the probe itself then exits normally, so there is
   no signal to go by).
5. Writes `meta.json` (app version and commit from `assets/build.json`, the sha256 and build id of
   every library in `nativeLibraryDir`, device model, SoC, Android version, `/proc/version`, page
   size, memory, the paths and the alias, `fa_path.lua`, `cpu_topology`, `app_process`, per step
   argv, environment, times, exit code and signal, and since 0.4.1 the telemetry samples under
   `telemetry`), `result.json` (the verdict and the lines the card shows; `speed` since 0.4.1) and
   `summary.txt`, and appends the summary to `logs/replaytest.log`. These replace the provisional
   files.

**Lifetime.** The runner is a child of the app's main process, which holds the foreground service and
a partial wake lock for the whole run, so it keeps running in the background and with the screen
off. It never outlives the job: Cancel (in the card or the notification), Android's dataSync time
limit (`onTimeout`) and the service's `onDestroy` cancel the job, which sends SIGTERM to the runner
and SIGKILL 3 s later. A step that prints nothing for 10 minutes (3 for the self-test steps) of
uptime (`SystemClock.uptimeMillis`, so time in deep sleep does not count as a hang) is ended the same
way. If Android kills the app process, the runner goes with its process group (checked on the
emulator: killing the app's main process during a replay step also removed the runner).

### Reading the result

- **PASS**: the runner exited 0 and the game ended ("game over at beat N"). The checksums recorded
  in the replay do *not* decide it: the data is FAF 3839 and the recordings are older, so mismatches
  against the recording are expected.
- The **checkpoint chain** (`checkpoint_chain_fnv1a`, every sim checkpoint hashed in order) is then
  compared with `assets/replay_refs.json`, keyed by the sha256 of the `.fafreplay` as downloaded:
  "matches the reference" means the device computed exactly what the emulator computed with the same
  binaries (the card also says whether the libraries' build ids are the ones the reference names).
  A different chain is not necessarily a port bug; the zip tells. Replays the table does not know get
  no comparison.
- **INCOMPLETE**: exit 0 without a game over (a replay without EndGame). **FAIL**: a failed
  self-test step, a non-zero exit (1 setup, 2 scenario load, 3 sim failure, 4 no progress) or a
  signal, with the crash handler's summary ("SIGSEGV at libfafengine.so+0x..."), or, with **Run the
  replay twice**, a second run that does not repeat the first one ("FAIL · second run differs ...";
  the notification then says "did not pass"). **CRASHED (expected)**: a signal with
  `FAF_LOWARENA=0` (two such runs count as identical when they end the same way).
- **INTERRUPTED**: the run never finished because Android ended the app (battery management, the app
  swiped away on some devices, low memory) or the app crashed. Reopening the app shows that run with
  the step it was in; **Save run (zip)…** exports what it wrote until then (the runner output and the
  engine log of the step that was running) and **Copy summary** copies its summary. Run the test again
  with the app open or in the background.

- **Speed** (0.4.1): one line from the telemetry of the first replay run, for example
  `Speed: sim thread mostly on mid cores (Cortex-A710, cpu4-6, 88% of its CPU time), CPU-bound
  (ran 97% of the sim time) · 2.31 GHz effective (92% of max) · 0.6 migrations/s · timer slack 50 µs
  · cpuset /top-app · cpus 2-7 (FAF's init_faf.lua)`. See
  [Speed telemetry](#speed-telemetry-and-the-experiments-041).

**Save run (zip)…** writes the run directory (never a replay file) and the tail of `launcher.log` to
a file the user picks (`ACTION_CREATE_DOCUMENT`; file managers cannot open `Android/data`). It opens
the file in mode `wt` and falls back to `w` for document providers that refuse truncation. **Save
all runs (zip)…** (0.4.1) does the same for every run under `runs/` in one zip
(`fafre-runs/<run>/...`, with the same exclusions and the 64 MB head-and-tail cut per file), so a
tester who ran the probe and several replay tests sends one file. **Copy summary** puts
`summary.txt` on the clipboard. **Runner output** shows the replay step's output. **Clear logs** also
deletes `runs/`; imported replays stay.

On Samsung phones set the app's battery usage to *Unrestricted* before a long run and do not swipe
the app away from the recent apps while the test runs.

**Which replay.** The reference test is the vault's
[26675870](https://replay.faforever.com/26675870) (SCMP_026, 465 beats, about 10 s on the x86_64
emulator, 40 s under ARM translation): its chain does not depend on leftover heap contents, nor on the
optional `mods.scd`/`skins.scd`/`loc_*.scd`. Longer replays work but take minutes and write large engine
logs (1.6-22 MB per 1000 beats, the engine's `[MOTDIAG]` diagnostics); the card notes replays over 20
minutes and less than 1 GB of free space. Chains of replays whose sims read uninitialised heap memory
(T3 26679175, see [headless-replay.md](headless-replay.md#in-app-replay-test-release-040)) can differ
between the app and the adb runs and are no parity check.

**Checked on the emulator** (API 36, 2026-10-07, x86_64 APK and the arm64-v8a APK under ARM
translation, through the app's UI): self-test, T1 PASS with the reference chain, a second run
identical, Cancel (runner gone within a second), 7 minutes in the background with the screen off,
the expected crash with `FAF_LOWARENA=0` (crash line, debuggerd backtrace in the saved logcat), Save
run (zip), Copy summary, "Open with" from the Files app, and GUI Start with Vulkan and OpenGL ES.
After the review fixes (x86_64 APK with the final runner set): T1 PASS twice, identical; the app's
process killed during a replay step (the runner went with it; after reopening, the card showed the
run as INTERRUPTED and its zip held that run); `my..game.scfareplay` imported as `my.game`; the test
refusing to start next to a game whose status was still `running`, and ending a finished game's
cached process. The details are in
[headless-replay.md](headless-replay.md#in-app-replay-test-release-040).

### Speed telemetry and the experiments (0.4.1)

The S22 Ultra loaded the reference replay in 15.8 s but simulated it at about 24 beats/s, slower than
arm64 under translation on the PC ([headless-replay.md](headless-replay.md#first-run-on-a-real-arm-core)).
0.4.1 records what the scheduler did with the runner, and offers two experiments.

**What is recorded** (`RunTelemetry.java`, a thread of the app during each replay step):

- **Once**: the runner's `/proc/<pid>/cpuset`, `cgroup`, `Cpus_allowed_list`, scheduling policy,
  nice and priority, `oom_score_adj`, and `timerslack_ns` (Linux shows another process's timer slack
  only with `CAP_SYS_NICE`, so this reads as unreadable or empty in an app). The runner is found as
  the app's child whose command line runs `libfafrunner.so` / `libfafrunner_o2.so`: argv[0], or
  argv[1] behind the emulator's ARM translation (`ndk_translation_program_runner_binfmt_misc_arm64`).
  The runner's own
  `[runner] sched {...}` line (port/engine/runner/RunnerSched.cpp: timer slack before and after,
  policy, nice, the task's utilisation clamp request `uclamp` and its cpu cgroup's clamp
  `uclamp_cgroup`, cpuset and its CPUs, allowed CPUs before and after, `affinity` with `in_force`,
  `set_by` and `owner`, CPU classes) is parsed into the step's `runner_sched`, and each
  `[runner] affinity {...}` line (one per distinct affinity request of the engine: what it asked for,
  what is in force, `set_by` `engine` or `runner`) into `runner_affinity`. The thread that forks the
  runner records its own timer
  slack (`prctl(PR_GET_TIMERSLACK)`), nice, policy, cpuset and allowed CPUs as `launcher_thread`:
  the runner inherits them at fork.
- **Every 200 ms**, per runner thread: `task/<tid>/schedstat` (ns on a CPU, ns runnable on a run
  queue) and `stat` (name, state, utime, stime, the CPU it last ran on). The run time gained since
  the previous look is credited to the class of the CPU the thread is on now, and weighted with that
  CPU's `scaling_cur_freq` (read once per look) for the thread's effective clock.
- **Every second** (recorded in `meta.json` `telemetry.<n>-<step>.samples`, rows
  `[tid, name, state, last cpu, utime, stime, ms on a cpu, ms queued, voluntary, involuntary
  switches]`): each thread's totals, its `Cpus_allowed_list` and context switches (`status`), its
  migrations (`sched`, where the kernel has it), every CPU's `scaling_cur_freq` (and, during the sim,
  the lowest `scaling_max_freq`, the policy's cap: `freq_sim.<cpu>.cap_min_khz`), the process's CPU
  time and RSS, the replay's phase and beat. Every 5 s the app's own state: importance (foreground service, ...), screen on, battery
  saver, device idle, thermal status and headroom, battery temperature, standby bucket, background
  restriction, the app's cpuset and `oom_score_adj` (`telemetry.<n>-<step>.app`).
- **CPU classes** (`meta.json` `cpu_topology`): `cpu_capacity` of every CPU, else
  `cpufreq/cpuinfo_max_freq`; one value means one class (`uniform`, as on the emulator), otherwise
  the lowest is `little`, the highest `big` and the rest `mid`. Core names come from `/proc/cpuinfo`
  (`CPU part`: Cortex-A510, A710, X2, ...). Unreadable sysfs leaves the classes `unknown`.

**The verdict** (`result.json` `speed`, the step's `telemetry` summary, one **Speed:** line) judges
the busiest runner thread during the sim phase (from "sim created" to the RESULT line; the whole run
up to the RESULT line when that phase lasted under 3 s). What follows the RESULT line (the `ending`
phase, the runner's shutdown) is recorded in the samples but belongs to neither window. The runner
does not name its threads, so all of them are called `libfafrunner.so`; the busiest one in threaded
mode is the sim thread. In this order:

| Verdict | When |
|---|---|
| `starved`: "waiting for a free core" | runnable but not running for at least 25 % of the time |
| `waiting`: "mostly waiting" | on a CPU less than 60 % of the time (sleeping or blocked) |
| `big` / `mid` / `little`: "on the big core", "mostly on little cores", ... "CPU-bound" | at least 60 % of its CPU time in one class |
| `mixed`: "spread over core classes" | no class reaches 60 % |
| `cpu-bound` | the classes are uniform or unknown |

A suffix names a pinning (`pinned to cpu1 (little)`) or an allowed list without the big cores. Then
the line has:

- the busiest thread's **effective clock**: its run time weighted with `scaling_cur_freq` of the CPU
  it was credited to, against `cpuinfo_max_freq` of those CPUs (`2.31 GHz effective (92% of max)`;
  `hot_clock` in the summary). Well below the maximum on a big core means a thermal or power cap
  (`freq_sim.<cpu>.cap_min_khz` shows the policy's lowest cap during the sim). The emulator's values
  are meaningless (`cpuinfo_max_freq` reads 2 kHz there);
- its **migrations per second** (`se.nr_migrations`, `hot_migrations_per_s`), a confidence hint for
  the core-class split: each 200 ms interval's run time goes to the CPU the thread is on at its end, so
  a migration can misplace up to 200 ms. When migrations × 200 ms exceed 20 % of its run time the line
  says `(core split approximate)` (`class_split` `approximate`, else `reliable`; `one class` on
  uniform cores such as the emulator's, where there is no split to doubt);
- the runner's timer slack and cpuset (sched line), and the **allowed CPUs as observed**: the busiest
  thread's `Cpus_allowed_list` during the judged window (`cpus_allowed_window`; `0-7 → 2-7` when it
  changed), with who set them: `speed experiment`, `speed experiment; FAF asked for 2-7` (the runner
  held its mask against FAF's request), `FAF's init_faf.lua` (the engine's request applied), or
  nothing (what the app passed on). FAF's `init_faf.lua` sets "every CPU but 0 and 1" on a device
  with six or more CPUs (2-7 on the S22 Ultra, whose CPUs 0-3 are the A510s), so `cpus 2-7 (FAF's
  init_faf.lua)` is the normal line there.

**The experiments** (Advanced options; both must give the reference chain of their own build):

- **Optimised engine (-O2 build)**: the replay test runs `libfafrunner_o2.so` with
  `FAF_ENGINE_LIB=<lib dir>/libfafengine_o2.so` (also for the engine-load step), the same code built
  with `-O2` and otherwise the same flags (`-ffp-contract=off`, no fast-math). Its chain is compared
  with the table's measurement of the `-O2` binaries only (`runs.<abi>-O2`, `"opt": "O2"`); a
  different `-O2` chain is a finding. Greyed out when the APK has no `-O2` pair.
- **Speed experiment: timer slack 1 ns, no little cores**: the replay steps get
  `FAF_RUNNER_TIMERSLACK_NS=1` and `FAF_RUNNER_AFFINITY=fast` (every allowed CPU except the lowest
  class: 4-7 on the S22), or `big` with *…only the biggest cores* (the single X2 there). The runner
  applies both to itself before `main`, so every engine thread inherits them, and reports them in its
  sched line. It also **holds** the mask: FAF's `init_faf.lua` asks for every CPU but 0 and 1 on six
  or more CPUs while the engine loads, and the shim applies such a request to every thread; the
  runner's exported `sched_setaffinity` keeps its own mask instead and prints a `[runner] affinity`
  line (`set_by` `runner`). Without the experiment FAF's request applies, as in the reference runs.
  The emulator's CPUs are all alike, so `fast` and `big` change nothing there; the hold was checked
  over adb with a forced CPU list and a copy of `init_faf.lua` that asks for 2-3 on four CPUs.

The run's `options` record `build`, `speed_experiment` and `affinity`; each runner step records its
`build` (`O0` or `O2`). The summary text names the build ids of the pair that ran.

### Device probe

**Device probe** (0.4.1) runs `libfafdeviceprobe.so --out <alias>/runs/<run>` as a job of its own
(the same foreground service, one step, watchdog 6 minutes without output: the probe's glslang
section may take up to 5 minutes and prints nothing meanwhile). It needs no game data: the data root
is not prepared and `init_faf.lua` is not required for it, so it runs right after installing. The probe runs each section in a
child process of its own (port/deviceprobe/DeviceProbe.cpp): `vulkan` (instance, devices, features,
formats: BC1-3, ETC2, ASTC, D24S8, D32, fillModeNonSolid, anisotropy, depth clamp, independent blend,
max texture size, timestamps, driver), `vulkan_render` (an offscreen test pattern read back into
`deviceprobe-vulkan.png`), `gles` (EGL/GLES 3.x, extensions: texture compression, clip control,
color_buffer_float, ...), `gles_render` (`deviceprobe-gles.png`) and `glslang` (compile times of a
fixed set of placeholder shaders, not FA's: real FA shader timing comes once M6b's emitter exists).
It writes `deviceprobe.json` (`probe`, `device`, `summary`, `sections`) and prints
`[probe] <section>: <status>: <line>` and `[probe] RESULT ... exit=N`. A section's child sends its
result before it tears its driver objects down (`vkDestroyDevice`, `vkDestroyInstance`,
`eglTerminate`); `sections.<name>.process.teardown` says how that went (`ok`, `crashed`, `timeout` after
20 s, `no_output`). A teardown that fails costs nothing the section found: the RESULT line gets
`,teardown=crashed`, the card a warning line, the run a `.logcat.txt`, and the exit code stays 0.

The step passes when the probe exits 0 (every section ran to its end, `unsupported` and `error`
included: those are findings) and wrote `deviceprobe.json`; exit 1 (a section crashed, hung or wrote
nothing) or a signal is FAIL. After the step the app kills any section process the probe left behind.
The card's **Device probe** block shows the last probe run: a headline such as `PASS · device probe:
Vulkan ok, render exact · OpenGL ES ok, render exact · glslang 812 ms` and one line per section from
the probe's own summary. **Probe images** shows the PNGs. `result.json` has the step's
`device_probe` (`headline`, `sections` with each status, `images`, `result_line`); the full report is
`deviceprobe.json` in the run directory and in the zip.

## Menu replay

Release 0.5.0 (milestone M7a1, step 8 of the [renderer plan](renderer.md#the-plan)). **Menu replay** in
the launcher shows FAF's main menu with its opening animation on the device: the real `gpg::gal` call
stream of the engine, recorded on the PC during a 900-frame menu run of the frame harness (a galtrace,
[port/graphics/trace](../../port/graphics/trace/README.md)), replayed by the port's own Diligent backend
over Vulkan on the device's GPU, with the textures and effects of the user's own game data. Then the app
says whether the frames it read back equal the PC's Diligent-Vulkan frames of the same trace. It is not
the live engine: no Lua, no UI logic, no sim (that is the GUI closure, step 9).

### The trace

- **In the APK**: `assets/galplay/menu.galtrace` (deflated) and its description `assets/galplay/menu.json`
  (build step 5). The trace is format version 2: what the engine read from game files (texture file
  images, effect sources, `GetTexture2D` inputs) is a *reference* (VFS path, content hashes, the archive
  the PC found it in); only what the engine generated itself is embedded (dynamic vertex and index data,
  the glyph atlas updates, render-target contents). The build refuses a version 1 trace, a cut-off one,
  and one with an embedded payload that looks like a game file (a DDS, PNG or JPEG image, or effect
  source text: the 900-frame version 1 menu trace has 21 DDS images and 11 effect sources, 1.3 MB, which
  a version 2 trace must hold as references). A **Release** build also refuses a trace without
  `reference_frames.diligent:vk` for every readback frame (the app could only say NO REFERENCE), with a
  home directory in its header metadata (the version 1 recorder's `command_line` names
  `C:\Users\<name>\...`), and a `libfaf_android.so` without the menu replay mode (it must contain the
  contract's names `menu-replay`, `menuReplay.trace` and `onMenuReplayFinished`; without them GameActivity
  would start the game instead). A Debug build only warns about those three.
- **Size**: 0.5.0's trace (`buildstage/traces/menu.galtrace`, made by `scripts/port/galtrace.py release`)
  is 111,039,865 bytes and 18,224,603 bytes deflated in the APK: the arm64 APK is 35.3 MB (0.4.1: 15.9 MB).
  Unpacked on the device it takes 111 MB. Of its payloads, 107.7 MB are the dynamic vertex and index data
  the engine generated (embedded); the 33 game files (1.3 MB), the nine readbacks and the 20
  `GetTexture2D` outputs are references or digests. (The version 1 recording, every payload embedded, is
  151.9 MB, 20.4 MB deflated.) Small enough for an APK asset; no separate download.
- **On the device**: the first Menu replay unpacks it into the app's internal storage
  (`no_backup/galplay/menu-<sha256 prefix>.galtrace`, checked against menu.json's size and sha256; once
  per APK, older copies are deleted). The card shows the trace's version, size, frame count, the frames
  it reads back, how many PC reference hashes it carries and the archives it uses.
- **Header keys** the app reads (`GalTraceFormat.h`): `harness_frames` (the readback frames, in order),
  `frame_rate`, `presents`, `payload_refs`, `ref_archives` (else menu.json's scan of the references) and
  `reference_frames.diligent:vk` (`"10:d92c3a2233b80c55,15:..."`, the PC's Vulkan frame hashes).

### Before the start

Everything runs on a thread of the launcher's (a progress panel with Cancel; it survives a rotation):

1. The APK must have the native runtime and a usable trace.
2. **Data check**: the required tier (the main menu's data; `init_faf.lua` mounts it) and every archive the
   trace refers to: a FAF file of the manifest (`effects.nx2`, `textures.nx2`, ...) or a file of an SCFA
   selection (`textures.scd`, ...). The card names what is missing before anything starts. A trace that
   names the FAF version it was recorded with (`faf_version`) gives a note when the device has another one.
   The native side checks every referenced file's hash when it reads it and stops with the file's path and
   archive if one differs.
3. **One user of the :game process at a time.** The game and the menu replay both run in GameActivity and
   both write `launch/status.json`; `Settings` records which one started last. A :game process whose status
   is not terminal (`loading`, `running`) is busy:

   | Asked for | while the game runs | while a menu replay runs |
   |---|---|---|
   | Menu replay | refused ("the game is running") | refused (button inactive) |
   | Start (the game) | as before (ends it and starts again) | refused ("the menu replay is running") |
   | Replay test, self-test, device probe | refused ("the game is running") | refused ("the menu replay is running") |

   A menu replay never starts while an import service job (a transfer or the replay test) runs. A finished
   run's cached :game process is ended first, as Start does.
4. `faf/fa_path.lua` is written with the real data root (as for Start), `launch/status.json` is removed, the
   trace is unpacked, and `runs/<run>/` gets a provisional `result.json` (verdict `INTERRUPTED`,
   `in_progress`, `"kind": "menu-replay"`) and `meta.json` (app, device, trace, data check, paths, the
   system's Vulkan feature level).
5. GameActivity is started (explicit Intent, its `:game` process) with:

   | Extra | Type | Meaning |
   |---|---|---|
   | `mode` | String | `menu-replay` |
   | `argv` | String[] | as for Start: `/init <root>/faf/bin/init_faf.lua /nobugreport /log <root>/runs/<run>/game.sclog /renderer vulkan /nomovie /nosound` (the native side takes `/init` and `/log` from it) |
   | `menuReplay.trace` | String | the unpacked trace |
   | `menuReplay.runDir` | String | `<root>/runs/<run>` |
   | `menuReplay.fast` | boolean | *As fast as possible* (default: the recorded 30 fps) |
   | `menuReplay.forceCpuDecode` | boolean | Advanced: BC textures decoded on the CPU even when the GPU samples them (the path for GPUs without BC, such as Mali) |
   | `menuReplay.noShaderCache` | boolean | Advanced: neither read nor write the shader and pipeline caches (a cold first launch) |

### While it runs

The native side (`port/android/src/GalPlay.h`, `galplay_run`) loads the data through the VFS, creates the
Vulkan device and swap chain on GameActivity's window, and replays the trace at its recorded pace. It calls
back into GameActivity (`onMenuReplayProgress`, `onMenuReplayReadback`, `onMenuReplayFinished`, on its
replay thread); `MenuReplaySession` copies what it needs and writes the files on a worker thread:

- **The overlay** (`MenuReplayOverlay`, a `PopupWindow` above the replay, because NativeActivity hands its
  own window's surface to the native renderer): the stage before the first frame (data, device, shaders),
  then frame number, frame time, the replay work of the frame and the average, and each readback frame's
  PASS/FAIL as it comes; at the end the result line, the timings and "Tap or press Back to return to the
  launcher". It takes no input: Back during the replay stops it (INCOMPLETE), a tap or Back after the end
  closes the activity (native side).
- **Each readback frame** (R G B A, top row first): the app hashes it itself (FNV-1a 64 over R, G, B, as the
  PC frame harness and `gfx_capture.py` hash frames) and compares the hash with the trace's PC
  Diligent-Vulkan hash; it writes `frame_<N>.png` (RGB, to look at) and `frame_<N>.bmp` (the harness's
  32-bit BMP, alpha included; unless the native side wrote it already). Android's `Bitmap` is not used:
  its premultiplied alpha would change pixels whose alpha is below 255, and the menu's head target has
  such pixels.
- **At the end** `result.json` (verdict, headline, per frame the app's and the native side's hash, the
  reference, the files; counts; frame-time statistics from the progress reports: mean, median, p95, max,
  and the replay work; galplay.json's `timings`, `caches` and `device`), `summary.txt`, `meta.json` (plus the
  :game process and galplay.json's summary), and a line in `logs/menureplay.log`. The native side writes
  `galplay.json` (`result`, `exitCode`, `message`, per readback frame `frameHashes` with the native side's
  hash, reference and verdict, `timings`, `caches`, `device`, `replay`, the player's report),
  `galplay.log`, `galreport.json` (the Diligent backend's report) and `launch/status.json`. The app's own
  hash of a frame and the native side's must agree; a disagreement is a warning line in the result.

### Reading the result

- **PASS**: every readback frame has the PC's Vulkan hash: the device drew the frame byte for byte as the
  PC did.
- **FAIL**: a frame's hash differs from the PC's, or the native side stopped with an error (the headline
  names it: missing data, a file whose hash differs, no Vulkan device, ...). A differing frame is *not
  byte-identical*; whether it is within the parity rule of the Windows gates (max |delta| <= 1 on <= 0.1 % of
  the pixels) needs the PC's pixels, so it is decided on the PC from the zip:
  `python scripts/port/gfx_capture.py parity <PC frames dir> <the unzipped run dir> --out <dir>`, where
  the PC frames are the same trace replayed into Diligent-Vulkan on the PC
  (`main.exe /galplay <trace> /galplayout <PC frames dir> /gal diligent:vk`, [port/graphics/trace](../../port/graphics/trace/README.md)),
  whose `frame_<N>.bmp` the phone's BMPs match in format byte for byte.
- **INCOMPLETE**: stopped (Back) or closed before every readback frame was read; the frames read so far are
  judged. **NO REFERENCE**: the trace carries no PC hashes. **NOT STARTED**: prepared, but cancelled at the
  last moment or the launcher was left before GameActivity could start (Android does not let an app start an
  activity from the background).
- **INTERRUPTED** / **CRASHED**: the :game process ended without a result. When the launcher sees that run
  again it adds the tail of `logs/faf_android_vulkan.log` and the app's logcat since the start
  (`logcat.txt`, with a native crash's backtrace) to the run directory and says CRASHED when they show a
  native crash.

The card also shows the shader line (shaders compiled and their time, SPIR-V cache hits, the Vulkan pipeline
cache warm or cold with the bytes loaded and saved: a first launch, or one with *Without the shader cache*,
against a later one) and the device line (adapter, swap-chain format and surface, the depth format, whether
BC textures are sampled by the GPU or decoded on the CPU). **Frames** shows the PNGs, **Copy summary** copies the summary, **Save
run (zip)…** writes the run directory as for the replay test (`fafre-run-<run>.zip` with
`fafre-run-<run>/...`; its `result.json` says `"kind": "menu-replay"`); **Save all runs** has menu replay
runs too. The PNGs and BMPs are pictures of the user's own game data, rendered on their device:
they belong in the tester's zip, never in a release.

A run stopped with Back whose frames read so far differ is **FAIL**, not INCOMPLETE (a differing frame
decides); its headline counts the frames not reached as well ("FAIL · 7 of 9 frames differ …" after 7 read
back), and the lines say "Frame 300: not reached". `galreport.json` is written when the activity closes (the
tap or Back after the result), so a zip saved after that has it.

### On the emulator (release check, 2026-10-08)

The API 36 emulator (SwiftShader Vulkan) runs the menu replay through the app's UI: 900 frames in 30.5 s at
the recorded pace (x86_64; 30.9 s for arm64 under translation, 9.0 s as fast as possible), 0 draws skipped,
0 Diligent errors, depth D32S8, a second launch with 0 shaders compiled (4 from the cache, against 58 ms of
compiling on the first). HOME during the replay pauses it and it continues with the same frames. Its frames
are identical on x86_64 and arm64 and between runs, but **not** the PC's: every frame is FAIL, and on the PC
34,050 to 135,384 pixels differ by up to 10, beyond the parity rule. The card therefore shows FAIL on the
emulator; [renderer.md](renderer.md#the-emulators-frames-swiftshader) has the numbers and the likely cause
(SwiftShader's 4-bit filtering precision). A phone's GPU decides.

## Checking data on the PC

`deploy_android.ps1 -StageDir <dir> -Tier required -Hardlink` builds the device layout on the PC
(hard links cost no space). The host tool `faf_datacheck` (`port/native`) runs `init_faf.lua`
against it with the same code the device uses and prints the mount table and lookups:

```powershell
faf_datacheck --init <dir>\faf\bin\init_faf.lua --localappdata <dir>\localappdata --documents <dir>\documents --no-writes
```

## Troubleshooting

**`INSTALL_FAILED_UPDATE_INCOMPATIBLE`** - the installed app is signed with another key (another
PC, a lost keystore, or an APK from someone else). Android cannot update it; it has to be removed
once: `adb uninstall io.github.m3rt1n99.fafre`. **This deletes the app's data folder, including all
copied game data, settings and logs.** Run the deploy script again afterwards. The deploy script
never uninstalls on its own. Keep `%USERPROFILE%\.android\debug.keystore` (or
`FAF_ANDROID_KEYSTORE`) to avoid this.

**`INSTALL_FAILED_VERSION_DOWNGRADE`** - the device has a build of a newer commit. Build from that
commit or later, or downgrade explicitly with `adb install -r -d <apk>`.

**Black screen or the game closes at once** - open the launcher: **Last run** shows the stage and
message from `status.json`. The full logs:

```powershell
adb pull /sdcard/Android/data/io.github.m3rt1n99.fafre/files/logs
adb logcat -s faf_android
```

**Vulkan problems** - the runtime switches to OpenGL ES by itself when Vulkan device or swapchain
creation fails (`renderer` in `status.json` says which one ran). To force OpenGL ES, choose it in the
launcher's settings (`/renderer gles`).

**"sha256 differs" / "size differs" for FAF files** - the FAF client has updated its files to
another game version than the manifest's 3839. Use `-FafSource Download`, or the launcher's
download, which fetch exactly 3839.

**"No Android device connected" / "has not authorized this computer"** - check `adb devices`;
unlock the device and accept the USB debugging prompt. With several devices, set `ANDROID_SERIAL`.

**`fchown failed` during the copy** - some devices refuse adb's ownership change in another app's
folder. The deploy script notices and continues through `/data/local/tmp`, which is slower but
works.

**Not enough space** - the deploy script compares what still has to be copied (plus a 512 MB
reserve) with the free space before copying. Use a smaller `-Tier`; required alone is 0.6 GB.

**Native build fails at configure** - run the bootstrap again (it reports what is missing). After
changing the NDK, build once with `-Clean`.

**Replay test: "Open with" does not offer the app** - some file managers report a `.fafreplay` with
a MIME type the app does not register. Use **Pick replay…** in the Replay test card instead.

**Replay test: a step was killed by SIGSYS (exit 159)** - the runner made a system call the app
seccomp filter blocks; the step's `.logcat.txt` and the `[runner] CRASH` line name it (`syscall=`).

**Replay test: a long replay is slow and the run directory huge** - the engine logs diagnostics for
every moving unit (`[MOTDIAG]`), so a long, large game writes hundreds of MB and the sim waits on the
log. Use the reference replay 26675870; **Clear logs** deletes old runs. The zip keeps the first 16 MB
and the last 48 MB of a file larger than 64 MB.

**Replay test stops when the screen is off or the app is in the background** - set the app's battery
usage to *Unrestricted* (Samsung: also take it off the sleeping-apps lists), keep the notification,
and do not swipe the app away; Android ends the runner together with the app. Such a run shows as
**INTERRUPTED** with the step it was in; its zip still has the logs up to that point.

**Replay test: "Not started: the game is running"** - the game (Start) is still loading or running,
maybe in the background. Close it with Back, or, if it is not on screen, end the app in Android's
settings (App info › Force stop) and open it again.

**Replay test: "This APK has no -O2 build of the runner"** - the APK was built with
`-SkipOptimizedRunner` (or with `-RunnerDirectory` and no `-RunnerO2Directory` in Debug). Turn the
option off, or install a release APK.

**Device probe: FAIL with exit 1** - a section crashed or hung in the driver before its result; the
others still ran. `deviceprobe.json` names it (`status`: `crashed`, `timeout`, `no_output`) and the
step's `.logcat.txt` has the driver's backtrace. A warning "the driver's teardown crashed" means the
section's result is complete and only its cleanup failed (also with a `.logcat.txt`).

**Menu replay does nothing** - the card says why: no native runtime or no trace in this APK (built with
`-SkipMenuTrace`), missing game data (the required tier or an archive the trace refers to), the game still
running, or a transfer or the replay test running.

**Menu replay: FAIL with "... sha256 differs" or a missing file** - the trace refers to game files by their
content; the files on the device are another version (FAF updates) or damaged. Get FAF's files again
(**Download FAF files**, which fetches exactly the manifest's version) or check the SCFA import.

**Menu replay: FAIL, frames differ** - send the zip: the BMPs in it are compared with the PC's frames under
the parity rule there.

**Menu replay: CRASHED or INTERRUPTED** - the run directory has the runtime log's tail and logcat.txt;
symbolize a native backtrace against the release's `libfaf_android-<abi>.so`.

**Speed line says "no telemetry"** - the app did not find the runner among its child processes in
`/proc` (it ended within milliseconds), or sampled it for under a second. The run's verdict is not
affected. (Before the second 0.4.1 build the emulator's ARM translation hid the runner too: its
command line starts with the translation's binfmt_misc runner. Phones were not affected.)
