# Devices that ran the replay test

One row per device and app version, from the run zips testers send back (how to test:
[phone-testing.md](phone-testing.md); how a zip is read: [android.md](android.md#reading-the-result)).

**What the columns mean:**
- **Self-test:** arena probe / arena capacity / engine load.
- **Replay:** the reference replay [26675870](https://replay.faforever.com/26675870) unless noted.
- **Chain:** compared with `port/android/assets/replay_refs.json` for the same engine and runner
  build ids. The reference chain for 0.4.0 is `4971bbe58c5586a0`.
- **Load / sim:** wall-clock seconds; the builds are `-O0` debug builds.

Testers are not named unless they ask to be.

| Date | Device (SoC) | Android (API), page | App (versionCode) | Self-test | Replay | Chain | 2nd run | Load / sim, beats/s | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 2026-10-07 | Samsung Galaxy S22 Ultra SM-S908B (Exynos 2200, 8 cores, 11 GB) | 16 (36), 4 KB | 0.4.0 (3446, pre-release build of the published binaries) | PASS (41/41, 1952 MB, engine at 0x2410000) | PASS, game over at 467 | `4971bbe58c5586a0`, matches | identical | 15.8 / 19.8 s, 24 | First run on a real ARM core. The sim is slower than arm64 under translation on the PC; cause not measured yet. `mods.scd`/`skins.scd` not imported. |
| 2026-10-07 | Android emulator `sdk_gphone64_x86_64` (PC, Intel Core Ultra 7 265KF) | 16 (36), 4 KB | 0.4.0 x86_64 APK | PASS (41/41, 1952 MB) | PASS, game over at 467 | `4971bbe58c5586a0` (the reference) | identical | 7.3 / 1.9 s | Reference run. |
| 2026-10-07 | the same emulator, arm64 under ARM translation | 16 (36), 4 KB | 0.4.0 arm64 APK | PASS (41/41, 1648-1664 MB) | PASS, game over at 467 | `4971bbe58c5586a0` (the reference) | identical | 25.6 / 11.1 s | Reference run. |
