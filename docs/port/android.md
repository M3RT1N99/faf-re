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
   app's **Licenses** screen shows every file. The `lib/<abi>/*.so` entries are added *deflated*: the
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
0.4.1 build, and the -O0 engine to 0.4.0's):

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
<root>/logs/                faf_android_vulkan.log, faf_android_gles.log (+ .1.log: previous run), launcher.log, game.sclog, replaytest.log
<root>/launch/status.json   result of the last run
<root>/replays/             replay test input: <id>.fafreplay as received, <id>.3764.scfareplay (converted), <id>.json (what is known about it)
<root>/runs/<run>/          one replay test or device probe run (<run> = yyyyMMdd-HHmmss): result.json, meta.json, summary.txt, <n>-<step>.out (+ .log, .summary.json, .logcat.txt); a probe run also deviceprobe.json, deviceprobe-vulkan.png, deviceprobe-gles.png
<root>/runner/home/         the runner's FAF_KNOWN_FOLDERS (emptied before every run; no Game.prefs)
<root>/.deploy/deployed.json  files placed by the deploy script or the import
```

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

**Speed line says "no telemetry"** - the app did not find the runner among its child processes in
`/proc` (it ended within milliseconds), or sampled it for under a second. The run's verdict is not
affected. (Before the second 0.4.1 build the emulator's ARM translation hid the runner too: its
command line starts with the translation's binfmt_misc runner. Phones were not affected.)
