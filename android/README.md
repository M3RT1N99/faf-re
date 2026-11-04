# Android test app

An Android app that runs the engine's simulation on a phone, without graphics: the sim-math tests
and FAF vault replays played to the end on the headless simulation (`src/headless`), with every
checksum the players' games recorded compared against the phone's. It is how a phone's CPU is
checked before the game itself runs there. A renderer test brings up the engine's Vulkan device on
the screen, and the game card starts the whole engine, the game client's first steps
([CLIENT_PLAN.md](CLIENT_PLAN.md)). The engine's
programs and libraries come from the Android native build ([native/README.md](native/README.md)),
which builds the engine for Android without changing its files and the client on top.

## Building

Needs the Android SDK (platform 35, build-tools 35), the NDK release in
`cmake/toolchains/android-pins.cmake` (`sdkmanager "ndk;29.0.14206865"`), JDK 17, CMake 3.25+, Ninja
and Python 3. `ANDROID_HOME` names the SDK.

```sh
# The engine (dependencies once, then the native build's preset):
cmake -DFAF_DEPENDENCIES_ONLY="WildMagic3p8;zlib-1.2.3;boost_1_34_1" -P cmake/FafDependencies.cmake
cd android/native
cmake --preset android-arm64 && cmake --build --preset android-arm64   # build/android-arm64/bin

# The app (JAVA_HOME = JDK 17):
cd ..
./gradlew assembleRelease    # app/build/outputs/apk/release/faf-re-android-<version>-arm64-v8a.apk
```

For the emulator, build the `android-x64` preset too and add its ABI:
`./gradlew assembleRelease -PfafAbis=arm64-v8a,x86_64`. The emulator runs x86_64 natively and arm64
under its ARM translation (an arm64-only APK).

- `stageFafNative` packages `headless` and `faf_sim_math_tests` as `libfafheadless.so` and
  `libfafsimmath.so`: Android extracts only `lib*.so` files into the app's native library folder, the
  one place an app may start a program from. It adds the renderer test's `libfafgalsmoke.so` and
  the game's `libfafgame.so`.
  `gradle.properties` names the build folders.
- `stageFafAssets` puts `tests/sim_math/golden.txt` into the assets.
- `versionCode` is `git rev-list --count HEAD`, so a newer commit installs over an older one;
  `versionName` is in `gradle.properties`.
- Signing: `FAF_ANDROID_KEYSTORE` (with `FAF_ANDROID_KEYSTORE_PASSWORD`, `FAF_ANDROID_KEY_ALIAS`,
  `FAF_ANDROID_KEY_PASSWORD`), else the standard debug key `~/.android/debug.keystore`. Android installs
  an update only with the installed app's key; the testers' releases are signed with the debug key of
  the machine that built them.

## What the app does

The application id is the earlier launcher releases' (`io.github.m3rt1n99.fafre`), so an update keeps
the game data they imported. Everything lives in the app's external files folder
(`/sdcard/Android/data/io.github.m3rt1n99.fafre/files`):

| Folder | What |
|---|---|
| `scfa/` | The user's own *Supreme Commander: Forged Alliance*: `gamedata/*.scd` (*Import SCFA folder…*, or an earlier release's import), `maps/` if imported |
| `parity/game/<version>/` | FAF's game files for one game version, packed as `tools/parity-data` packs them for CI: `gamedata/*.nx2` cut to the files headless reads, `bin/init_ci.lua`, `fa_path.lua` pointing at `scfa/` |
| `parity/replays/`, `maps/`, `m28/` | Downloaded replays, vault maps and M28AI versions |
| `parity/runs/<id>/` | One replay's run: `replay.scfareplay`, its own vault, `headless.out`, `engine.log`, `speed.csv` |
| `parity/results/` | The results zips |
| `game/` | The game's: `home/` (its HOME: the preferences, the documents), `vault/`, `engine.sclog` |

- **Math check**: `faf_sim_math_tests golden.txt --known known_android-<arch>.txt` (the overlay only
  where `tests/sim_math` has one for the ABI: arm64's records the square root of a negative number,
  whose NaN is positive on ARM). PASS means all golden cases (recorded on the x87 of windows-x64),
  with the overlay's recorded differences, equal the phone's results. On a failure the app also
  writes every differing case (`sim_math_known.txt`, the overlay format).
- **Replay parity** follows `tools/parity-data/shard.py`: the replay from FAF's vault, decoded
  (zstd or zlib) and checked (featured mod `faf`, engine version, M28AI as the only sim mod); FAF's
  game files of its version from FAF's updater (about 700 MB once per version, kept whole for the
  game); its map from `scfa/maps` or FAF's map vault; then `headless --replay`. The verdicts are shard.py's:
  *pass*, *desync* (with the first beat whose checksum differs; a desync one live client had alone is
  not counted), *crash*, *incomplete*. Requests to FAF go one at a time, a second apart.
- **Where the simulation ran**: once a second during a replay, the busiest thread of the headless
  process (the simulation's) from `/proc/<pid>/task/<tid>/stat` (CPU time, last CPU), that CPU's
  class (little, mid, big by `cpuinfo_max_freq`), clock (`scaling_cur_freq`) and cap
  (`scaling_max_freq`), Android's thermal status, the battery's temperature and charger, the screen
  and whether the app is in front: a line under each replay, `speed` in `summary.json`, the rows in
  `speed.csv`. The screen stays on while a test runs.
- **Performance hint** (the box under the replay IDs, on by default): the app opens an Android
  performance hint session (`PerformanceHintManager`, Android 12+) for the simulation thread and
  reports its run time every 100 ms against a 50 ms target, as a game reports its frames; the
  speed line says whether the phone granted it. The manifest files the app as a game
  (`android:appCategory`); `summary.json` `game` shows the category and game mode the phone gives
  it. A run first downloads every replay, map and M28AI version it needs, then plays them, so a
  long run needs no network once the games start; downloads are retried for about ten minutes
  when the network drops.
- **Renderer test** (card 4, `GalSmokeActivity`): `libfafgalsmoke.so`
  (`native/src/smoke/GalSmoke.cpp`) lists the phone's Vulkan devices, creates the engine's gal
  device (the Vulkan backend, which needs Vulkan 1.3) on a full-screen surface, then clears it in
  changing colours and presents for ten seconds. It needs no game data and draws without shaders or
  textures, which come with C2. The report gives:
  - the screen: the display mode and the app's size, which shows a scaled-down app;
  - each Vulkan device with the features the backend needs;
  - the engine's log lines;
  - the frame rate, or the line that failed.

  The test runs in a process of its own (`:renderer`), and the native
  side writes each report line to `renderer.txt` in the app's files as it comes. If the driver takes
  that process down, the app keeps the lines up to the crash and the reason Android gives for the
  end (`ApplicationExitInfo`). On Android 12 and later it also keeps the tombstone, which goes into
  the zip as `renderer-tombstone.pb`. The emulator's SwiftShader offers only Vulkan 1.2, so the test
  fails there. Started with `-gpu host` on a PC whose GPU has Vulkan 1.3, the emulator gives the
  guest Vulkan 1.3, and the test runs.
- **Game** (card 5, `GameActivity`): `libfafgame.so`, the whole engine with its host
  (`native/src/host`), starts as the PC starts it, with the SCFA import and FAF's game files of the
  newest version a replay downloaded: `ForgedAlliance.exe /init <its init file> /log
  game/engine.sclog`, the program's path in the game files' `bin` folder. It runs in a process of
  its own (`:game`) on a full-screen surface, which ends with the program, as a Windows program's
  does. Touch and the mouse are the mouse, keys the keyboard, Back is Esc. Until the renderer has
  its shader compiler (C2), the engine initialises (archives, preferences, Lua, the UI) and stops at
  the renderer, ending its process with exit code 1 as a PC without a usable renderer does. The
  host writes `game.txt` in the app's files; when the process is gone, the app adds how it ended
  (the exit code, or what `ApplicationExitInfo` says, the tombstone of a native crash) and why the
  renderer could not start, from the engine's log.
- **Share results** zips `summary.json`, the math output, the log, the renderer report
  (`renderer.txt`), the game's report and log (`game.txt`, `game/engine.sclog`) and each replay's
  `headless.out`, `engine.log` and `speed.csv`.

The default replays (26930624 and 26902315), *Use the core set* and *Use the extended set* (the core
set and its ten longer games) are from [docs/PR_REPLAY_CHECK.md](../docs/PR_REPLAY_CHECK.md), all
recorded on FAF game version 3835. A test runs at full speed only with the app in front: Android
moves a background app, and the simulation process it started, to another CPU group, on a Galaxy
S22 Ultra the little cores.
