# Android test app

An Android app that runs the engine's simulation on a phone, without graphics: the sim-math tests
and FAF vault replays played to the end on the headless simulation (`src/headless`), with every
checksum the players' games recorded compared against the phone's. It is how a phone's CPU is
checked before the game itself runs there. The engine programs come from the Android presets
(`cmake/toolchains/android-*.cmake`); this folder only packages them.

## Building

Needs the Android SDK (platform 35, build-tools 35), the NDK release in
`cmake/toolchains/android-pins.cmake` (`sdkmanager "ndk;29.0.14206865"`), JDK 17, CMake 3.25+, Ninja
and Python 3. `ANDROID_HOME` names the SDK.

```sh
# The engine (dependencies once, then the presets):
cmake -DFAF_DEPENDENCIES_ONLY="WildMagic3p8;zlib-1.2.3;boost_1_34_1" -P cmake/FafDependencies.cmake
cmake --preset android-arm64-release
cmake --build --preset android-arm64-release --target headless faf_sim_math_tests

# The app (JAVA_HOME = JDK 17):
cd android
./gradlew assembleRelease    # app/build/outputs/apk/release/faf-re-android-<version>-arm64-v8a.apk
```

For the emulator, build `android-x64-release` too and add its ABI:
`./gradlew assembleRelease -PfafAbis=arm64-v8a,x86_64`. The emulator runs x86_64 natively and arm64
under its ARM translation (an arm64-only APK).

- `stageFafNative` packages `headless` and `faf_sim_math_tests` as `libfafheadless.so` and
  `libfafsimmath.so`: Android extracts only `lib*.so` files into the app's native library folder, the
  one place an app may start a program from. `gradle.properties` names the build folders.
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

- **Math check**: `faf_sim_math_tests golden.txt --known known_android-<arch>.txt` (the overlay only
  where `tests/sim_math` has one for the ABI: arm64's records the square root of a negative number,
  whose NaN is positive on ARM). PASS means all golden cases (recorded on the x87 of windows-x64),
  with the overlay's recorded differences, equal the phone's results. On a failure the app also
  writes every differing case (`sim_math_known.txt`, the overlay format).
- **Replay parity** follows `tools/parity-data/shard.py`: the replay from FAF's vault, decoded
  (zstd or zlib) and checked (featured mod `faf`, engine version, M28AI as the only sim mod); FAF's
  game files of its version from FAF's updater (about 700 MB downloaded once per version, 35 MB kept);
  its map from `scfa/maps` or FAF's map vault; then `headless --replay`. The verdicts are shard.py's:
  *pass*, *desync* (with the first beat whose checksum differs; a desync one live client had alone is
  not counted), *crash*, *incomplete*. Requests to FAF go one at a time, a second apart.
- **Where the simulation ran**: once a second during a replay, the busiest thread of the headless
  process (the simulation's) from `/proc/<pid>/task/<tid>/stat` (CPU time, last CPU), that CPU's
  class (little, mid, big by `cpuinfo_max_freq`), clock (`scaling_cur_freq`) and cap
  (`scaling_max_freq`), Android's thermal status, the battery's temperature and charger, the screen
  and whether the app is in front: a line under each replay, `speed` in `summary.json`, the rows in
  `speed.csv`. The screen stays on while a test runs.
- **Share results** zips `summary.json`, the math output, the log and each replay's `headless.out`,
  `engine.log` and `speed.csv`.

The default replays (26930624 and 26902315) and *Use the core set* are from
[docs/PR_REPLAY_CHECK.md](../docs/PR_REPLAY_CHECK.md), all recorded on FAF game version 3835.
