# Helping test on your phone

The Android port can already run Forged Alliance's simulation on a phone without any graphics. It
plays a FAF replay to the end, inside the app, and checks the result against a reference. Every
phone model that runs this test tells us whether the engine computes exactly the same game on that
CPU, kernel and Android version. Results so far are in [device-matrix.md](device-matrix.md).

The test takes about 15 minutes the first time, most of it putting the game data on the phone. You
need no PC tools and no USB debugging.

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
   installed: the new one installs over it and keeps the data.
2. **Put the game data on the phone**, in the app's **Game data** card:
   - **Download FAF files**. Keep *Recommended for normal play* on.
   - **Import SCFA folder…**. Pick the copied *Supreme Commander Forged Alliance* folder and keep
     *Recommended for normal play* on. Sounds, voices, movies and the optional extras are not needed
     for the test.

   The test needs every *Required* and *Recommended* item, including the skirmish map **SCMP_026**.
   The card lists anything still missing.
3. **Phone settings.**
   - Set Settings > Apps > the app (*FAF (faf-re)*) > Battery to **Unrestricted**.
   - On Samsung, also keep the app off the *sleeping apps* lists.
   - Allow notifications when the app asks; the test shows its progress there.
4. **Get the test replay.** Open **https://replay.faforever.com/26675870** in the phone's browser. It
   downloads `26675870.fafreplay` (2.4 KB), a short 1v1 on SCMP_026 from FAF's public replay vault.
5. **Open the replay with the app.** Either:
   - tap the download and choose *FAF replay test*; or
   - in the app, open **Replay test** > **Pick replay…** > *Downloads* > `26675870.fafreplay`.

   The card then shows `scmp_026 · faf · 465 beats` and a reference chain.
6. **Run the test.**
   1. Open **Advanced options** and tick **Run the replay twice**.
   2. Tap **Run test (self-test + replay)**.
   3. Keep the app open, or let it run in the background. Do not swipe it away from the recent apps.

   One run takes about 35 s on a Galaxy S22 Ultra, so with two runs and the self-test expect a
   minute or two.
7. **Send the result.**
   1. Tap **Save run (zip)…** and save the file, for example to *Downloads*.
   2. Send it the way you agreed with the maintainer, for example as an attachment to a GitHub issue
      on [M3RT1N99/faf-re](https://github.com/M3RT1N99/faf-re/issues) or as a direct message.
   3. Add the phone's model name if you like. The zip already contains the model, Android version and
      CPU.

   If you cannot send files, **Copy summary** copies a text summary you can paste into a message.

## What the result means

| The card says | Meaning | What to send |
|---|---|---|
| **PASS** · game over at beat 467 · chain … matches the reference | Your phone computed exactly what the reference did. | The zip, so your phone goes into the device list. |
| **PASS** with *chain … differs* | The game played to its end, but the result differs from the reference. This is a finding, not necessarily a bug on your phone. | The zip. |
| **FAIL** in the self-test | Your phone's memory layout or kernel does something the engine does not expect yet. | The zip. This is what testing is looking for. |
| **FAIL** · SIGSEGV / SIGBUS / … at libfafengine.so+0x… | The engine crashed. | The zip; it holds the crash details. |
| **FAIL** · second run differs | The two runs disagreed. | The zip. |
| **INTERRUPTED** | Android stopped the app before the end (battery saver, app swiped away, low memory). | The zip, then run again with the battery setting from step 3. |
| **Not started** · … missing | A data file or the map is missing; the line says which. | Nothing yet; fix the data (step 2) and run again. |

*Recorded checksums: 0/20 match* and *engine desyncs 20* are expected. The replay was recorded with
an older FAF version than the data, so only the chain is compared.

## Privacy

The zip contains:
- your phone's model, Android version, kernel build and CPU details;
- the app's logs;
- the engine log of the run. The engine log names the replay's two players by their FAF nickname,
  as FAF's public replay page does.

It contains no replay file, no game data, no account, no contacts and no location. Delete the zip
when you no longer need it.

## Other replays

Any FAF replay whose map you have (stock skirmish maps or maps in the app's vault folder) can be run
the same way. Only 26675870 has a reference to compare against, so for other replays only "played to
the end" counts. Long games take minutes and write large logs.
