#pragma once

namespace moho
{
  /**
   * Port addition (no binary counterpart): the headless replay runner, milestone M3a of
   * docs/port/android-roadmap.md.
   *
   * What it does:
   * Plays one `.scfareplay` to its end with no window, device, audio or UI and returns a process
   * exit code. It brings up only the services the sim needs (logging, reflection, resources, the
   * data-path mounts), builds the session the way the GUI path does (`VCR_SetupReplaySession`,
   * the world-session loader, `SIM_CreateDriver`), drains the driver's sync packets, compares the
   * sim's beat checksums with the ones recorded in the replay and writes a JSON summary.
   *
   * Command line (read through `CFG_GetArgOption`, so the engine's own options such as `/init`,
   * `/log`, `/synclog`, `/simworkers`, `/sse2` and `/prefs` keep their meaning):
   *   /headlessreplay <file.scfareplay>   the replay; the only required option
   *   /headlesssummary <file.json>        where to write the JSON summary (default: stdout only)
   *   /headlessprogress <beats>           progress line every N beats (default 500, 0 = off)
   *   /headlesstimeout <seconds>          give up after this long without a new beat (default 120)
   *   /headlessspeed <rate>               requested game speed, -10..50 (default 50 = unthrottled)
   *   /headlessinterlocked                run the beats on this thread (`ISTIDriver::ProcessEvents`)
   *                                       instead of the driver's own "Sim" thread
   *
   * Exit codes: 0 the replay played to its end (checksum mismatches do not change this), 1 bad
   * arguments or setup, 2 the scenario failed to load, 3 the sim failed or the engine died,
   * 4 no progress within the timeout.
   *
   * Portable: no wx, no D3D, no UI. The few Windows-only host details (no error dialogs, a console
   * for stdout) are behind `_WIN32`, so the same TU builds for the Android runner (M3b).
   */
  int HEADLESS_RunReplay();
} // namespace moho
