# Helping test on your phone

The Android port can already run Forged Alliance's simulation on a phone without any graphics. It
plays a FAF replay to the end, inside the app, and checks the result against a reference. Every
phone model that runs this test tells us whether the engine computes exactly the same game on that
CPU, kernel and Android version. Results so far are in [device-matrix.md](device-matrix.md).

Since version 0.4.1 the app also records how the phone schedules the engine (the simulation was
slower on the first phone than expected), and it runs a short **device probe**: what the phone's GPU
drivers offer the graphics port that comes next.

Since version 0.5.0 the app draws something: **Menu replay** shows FAF's main menu with its opening
animation, drawn by the port's own renderer on the phone's GPU (Vulkan) from your own game data, and
checks whether the phone's frames equal the PC's. No phone has run it yet, so your result is the first.

The test takes about 15 minutes the first time, most of it putting the game data on the phone; the
runs themselves take about 5 minutes. You need no PC tools and no USB debugging.

## What you need

- **Phone:** 64-bit Android 8.0 or newer (arm64), with about 6 GB of free storage. The data takes
  about 5 GB, and each run writes a few MB of logs.
- **The base game:** your own copy of *Supreme Commander: Forged Alliance* (Steam or GOG). Its game
  folder has to get onto the phone once, for example by copying it to the phone over a cable, an SD
  card, a USB stick or a cloud drive. The app never ships game data.
- **The FAF files:** the app downloads them itself, about 680 MB.

## Steps

1. **Install the app.** Download the newest `faf-re-android-<version>-arm64-v8a.apk` from the
   [releases page](https://github.com/M3RT1N99/faf-re/releases). Open it on the phone and allow
   installing from your browser or file manager if Android asks. An older version of the app can stay
   installed: the new one installs over it and keeps the data. The line under the app's name then
   shows the new version.
2. **Put the game data on the phone**, in the app's **Get game data** card:
   - **Download FAF files**.
   - **Import SCFA folder…**. Pick the copied *Supreme Commander Forged Alliance* folder and keep
     *Recommended for normal play* on. Sounds, voices, movies and the optional extras are not needed
     for the test.

   The test needs every *Required* and *Recommended* item, including the skirmish map **SCMP_026**.
   The **Game data** card above lists anything still missing. Only the **Device probe** (step 6) runs
   without the game data.
3. **Phone settings.**
   - Set Settings > Apps > the app (*FAF (faf-re)*) > Battery to **Unrestricted**.
   - On Samsung, also keep the app off the *sleeping apps* lists.
   - Allow notifications when the app asks; the test shows its progress there.
4. **Get the test replay.** Open **https://replay.faforever.com/26675870** in the phone's browser. It
   downloads `26675870.fafreplay` (2.4 KB), a short 1v1 on SCMP_026 from FAF's public replay vault.
5. **Open the replay with the app.** Either:
   - tap the download and choose *FAF replay test*; or
   - in the app, open **Replay test** > **Pick replay…** > *Downloads* > `26675870.fafreplay`.

   The card then shows `26675870.fafreplay · scmp_026 · faf · 465 beats` and
   `reference: chain 4971bbe58c5586a0, game over at beat 467`.
6. **Run the tests.** In the **Replay test** card, open **Advanced options** and tick **Run the replay
   twice (is the result deterministic?)**. Then run these one after the other, each once the previous
   result is shown:
   1. **Run test (self-test + replay)**. This is the normal build.
   2. **Device probe**. It takes a few seconds. Its result appears under the *Device probe* heading at
      the bottom of the card, and **Probe images** shows its two test pictures.
   3. Tick **Optimised engine (-O2 build)**, then tap **Run test (self-test + replay) · -O2**.
   4. Also tick **Speed experiment: timer slack 1 ns, no little cores** (leave *…only the biggest
      cores* off), then tap **Run test (self-test + replay) · -O2, speed experiment**.

   Afterwards untick the two options again. Keep the app open, or let it run in the background. Do not
   swipe it away from the recent apps.

   One run of the normal build takes about 36 s on a Galaxy S22 Ultra. (In 0.4.0 it took 45 s: the
   engine needed another 10 s to shut down after the game was over. Since 0.4.1 it ends right after
   its result.) The optimised build should be faster. Expect about 4 minutes for all four steps.
7. **Menu replay** (0.5.0). In the **Menu replay** card, the line under the description shows the trace
   (`Trace menu.galtrace · v2 · 111 MB · 900 frames at 30 fps`) and `Uses your effects.nx2, textures.scd`.
   When that data is missing, the card names it in red and the button stays inactive.
   1. Tap **Menu replay** (with *As fast as possible* and *Advanced options* off). The phone turns to
      landscape and shows the main menu building up, with a small panel at the top left: frame number,
      frame time and, for frames 10, 15, 20, 25, 30, 45, 60, 300 and 900, PASS or FAIL. It runs 30 seconds,
      then shows the result. Tap the screen (or Back) to return to the launcher.
   2. Tap **Menu replay** once more. The second run takes the shaders from the cache; the *Shaders* line
      on the card then says `0 compiled … 4 from the cache`.
   3. Open *Advanced options*, tick **Decode BC textures on the CPU**, tap **Menu replay** again, then
      untick it.

   The first start unpacks the trace (111 MB) into the app's storage; that takes a few seconds once.
8. **Send the result.**
   1. Tap **Save all runs (zip)…** and save the file, for example to *Downloads*. It holds every run
      in the app's run list, including runs from earlier days and the menu replays. To send only
      today's runs, tap **Clear logs** (in the *Logs* card) before step 6. It deletes old runs and
      logs, not the game data or the replays. **Save run (zip)…** saves only the run the card shows.
   2. Send it the way you agreed with the maintainer, for example as an attachment to a GitHub issue
      on [M3RT1N99/faf-re](https://github.com/M3RT1N99/faf-re/issues) or as a direct message.
   3. Add the phone's model name if you like. The zip already contains the model, Android version and
      CPU.

   If you cannot send files, **Copy summary** copies the text summary of the run the card shows. You
   can paste it into a message.

## What the result means

| The card says | Meaning | What to send |
|---|---|---|
| **PASS** · game over at beat 467 · chain … matches the reference | Your phone computed exactly what the reference did. | The zip, so your phone goes into the device list. |
| **PASS** · … · chain … differs from the reference … | The game played to its end, but the result differs from the reference. This is a finding, not necessarily a bug on your phone. | The zip. |
| **FAIL** · self-test: … failed | Your phone's memory layout or kernel does something the engine does not expect yet. | The zip. This is what testing is looking for. |
| **FAIL** · SIGSEGV / SIGBUS / … at libfafengine.so+0x… | The engine crashed. | The zip; it holds the crash details. |
| **FAIL** · second run differs | The two runs disagreed. | The zip. |
| **INTERRUPTED** | Android stopped the app before the end (battery saver, app swiped away, low memory). | The zip, then run again with the battery setting from step 3. |
| Self-test passed · replay not run: … is missing | A data file or the map is missing. The card lists it in red before you run. | Nothing yet. Fix the data (step 2) and run again. |
| *Last test …: Not started: the game is running* | The game (*Start*) is still open. | Nothing yet. Close the game (Back), then run again. |
| **PASS** · device probe: Vulkan ok, render exact · OpenGL ES ok, render exact · glslang … ms | Every part of the probe ran. A part that says *unsupported*, *error* or *render wrong* is a result about your phone's drivers, not a failure of the test. | The zip. |
| **PASS** · device probe … and a warning *…: the driver's teardown crashed …* | The part reported everything, then the GPU driver crashed or hung while the part closed it again. | The zip; it holds Android's log of the crash. |
| **FAIL** · device probe | A part of the probe crashed or hung inside a GPU driver before it could report. | The zip; it holds Android's log of the crash. |
| Menu replay: **PASS** · every frame equals the PC's Vulkan frames | Your phone's GPU drew the menu byte for byte as the PC did. | The zip. |
| Menu replay: **FAIL** · N of 9 frames differ from the PC's Vulkan frames | The menu was drawn, but not byte for byte as on the PC. Whether the difference is within the tolerance is checked on the PC from the pictures in the zip. Not a failure of your phone. (The PC's Android emulator gets this result too.) | The zip. |
| Menu replay: **FAIL** · a file is missing or differs / no Vulkan device / an error | The replay could not run; the line says why. | The zip. For missing data, fix the data (step 2) first. |
| Menu replay: **INCOMPLETE** or **CRASHED** | It stopped before the end (Back, the app left, or a crash). | The zip; after a crash it holds the logs. |

*Recorded checksums: 0/20 match* and *engine desyncs 20* are expected. The replay was recorded with
an older FAF version than the data, so only the chain is compared.

**The Speed line.** Each replay run shows a line such as `Speed: sim thread mostly on mid cores
(Cortex-A710, cpu4-6, 88% of its CPU time), CPU-bound (ran 97% of the sim time) · 2.31 GHz effective
(92% of max) · 0.6 migrations/s · timer slack 50 µs · cpuset /top-app · cpus 2-7 (FAF's
init_faf.lua)`. It is a measurement, not a pass or fail. It says:
- on which kind of CPU core (big, middle or little) the engine's simulation thread ran, and whether it
  was busy, waiting for a free core, or sleeping;
- the clock that thread ran at on average (*effective*), and how often it moved between cores
  (*migrations/s*; with many moves the line adds *(core split approximate)*);
- which CPUs the engine was allowed to use while it simulated, and who chose them. On a phone with
  six or more CPUs, FAF's own start-up script (`init_faf.lua`) allows every CPU except the first two,
  as it does on a PC: `cpus 2-7 (FAF's init_faf.lua)` on an 8-core phone is normal. With the speed
  experiment the line says, for example, `cpus 4-7 (speed experiment; FAF asked for 2-7)`: the
  experiment's choice held.

The runs with the optimised build and the speed experiment show whether the build or Android's
scheduling holds the phone back. The -O2 runs and the speed experiment must still give the reference
chain.

## Privacy

The zip contains:
- your phone's model, Android version, kernel build and CPU details;
- the app's logs, and the CPU load of the app and of the engine's threads during each run;
- from the device probe: the GPU's name, its driver versions and the features it reports, and two
  small test pictures the app drew itself;
- the engine log of each replay run. The engine log names the replay's two players by their FAF
  nickname, as FAF's public replay page does;
- when a part of the device probe fails, the app's own part of Android's log (logcat) from that run;
- from each menu replay (0.5.0): the nine read-back frames as PNG and BMP pictures (FAF's main menu,
  drawn on your phone from your own game data), the GPU's name and Vulkan details, and the timings. The
  menu shows the profile name the PC recording used (`harness`), not yours.

It contains no replay file, no game data, no account, no contacts and no location. Delete the zip
when you no longer need it.

## Other replays

Any FAF replay whose map you have (stock skirmish maps or maps in the app's vault folder) can be run
the same way. Only 26675870 has a reference to compare against, so for other replays only "played to
the end" counts. Long games take minutes and write large logs.
