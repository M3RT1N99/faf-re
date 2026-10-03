# Android

One APK, `io.github.m3rt1n99.fafre` ("FAF (faf-re)"), arm64-v8a, Android 8.0 (API 26) or newer:

- **Launcher** (Java, `port/android/src/java`): checks and imports the game data, holds the launch
  options, starts the game, shows the result of the last run and the logs.
- **Native runtime** (`libfaf_android.so`, `port/android/src`, built on `port/native`): runs FAF's
  unmodified `init_faf.lua`, mounts the game data the way the engine does and, for milestone M1,
  shows a main-menu background from the user's data through Diligent (Vulkan, OpenGL ES fallback).
  It is not the game yet; see the [roadmap](android-roadmap.md).

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
path, size and the signing certificate's SHA-256.

| Option | Effect |
| --- | --- |
| `-Configuration Release\|Debug` | Release (default) or Debug: unoptimized native code, debuggable APK. |
| `-SkipNative` | Launcher only, no native build: `faf-re-android-<versionName>-nonative.apk`. For UI work; the game cannot start from it. |
| `-Clean` | Deletes the build directories first. Needed after switching NDKs. |
| `-AndroidSdk`, `-Ndk`, `-Jdk`, `-CMake`, `-Ninja`, `-Python` | Tool locations, when the defaults do not find them. |
| `-Keystore <file>` | Signing key (default below). |
| `-NativeLibrary <libfaf_android.so>` | Package a library built elsewhere (CI) instead of building `port/android`. |
| `-DryRun` | Resolve the tools and print every command without running anything. |

What it does:

1. **Native**: configures `port/android` with CMake, Ninja and the NDK toolchain
   (`ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=android-26`, `ANDROID_STL=c++_static`) in
   `buildstage\android-native` and builds the target `faf_android`. CMake is re-run only when its
   arguments change. The library is checked before it is packaged: it must export
   `ANativeActivity_onCreate`, need no library Android does not provide (Diligent and the C++
   runtime are linked statically, the APK carries exactly one `.so`), and have 16 KB aligned
   segments for 16 KB page devices. A stripped copy goes into the APK; the unstripped one stays in
   `buildstage\android-apk\symbols\arm64-v8a\` for symbolizing crashes:
   `adb logcat | ndk-stack -sym buildstage\android-apk\symbols\arm64-v8a`.
2. **Resources**: `aapt2 compile` + `aapt2 link` with the manifest, versionCode and versionName.
3. **Java**: `javac` (Java 8 bytecode against `android.jar`) and `d8 --min-api 26`.
4. **Package**: `classes.dex`, `lib/arm64-v8a/libfaf_android.so` and `assets/gamedata.json` (the
   manifest from `port/data`) are added *uncompressed*: the manifest sets
   `extractNativeLibs="false"`, so Android maps the library straight out of the APK. Then
   `zipalign -P 16 4` and `apksigner sign`.
5. **Verify**: `apksigner verify`, `zipalign -c -P 16 4`, and `aapt2 dump badging` must show the
   package, version, SDK levels, `native-code: 'arm64-v8a'` and the launcher activity.

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
<root>/logs/                faf_android.log, launcher.log, game.sclog
<root>/launch/status.json   result of the last run
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
| `/log <file>` | Game log. The runtime also writes its own `<root>/logs/faf_android.log` (truncated per run, mirrored to logcat tag `faf_android`). |
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
