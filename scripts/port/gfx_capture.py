#!/usr/bin/env python3
"""Run main.exe's frame harness N times and compare the captures (M6a step 0, docs/port/renderer.md).

The harness is the FAF_PORT_GRAPHICS build of main.exe (msbuild /p:FafPortGraphics=true) started with
`/galharness <dir>`: it renders FAF's main menu in a window that is never shown, with a fixed frame
delta, a virtual Lua clock and a fixed random seed, and writes the back buffer of chosen frames,
read back through gal, as frame_<N>.bmp plus harness.json (port/graphics/capture/README.md).

This script owns everything around a run:
  - refuses to start when any FAF_* environment variable is set (the committed render-path probes
    arm themselves from them), or when the exe does not contain the harness (without it the options
    are ignored and the game window would open);
  - per run, a scratch directory with a pinned Game.prefs (passed with /prefs, never the user's), a
    copy of FAF's init_faf.lua that keeps the process priority, mounts nothing of the user's
    LOCALAPPDATA or vault and prunes no real cache (passed with /init), and /cachedir for the
    compiled effects;
  - one GUI run at a time on this machine: the lock file (--lock) is created exclusively before a run
    and deleted after it, and a run waits while it exists;
  - the process: below-normal priority, SW_HIDE and no busy cursor in its STARTUPINFO, a job object
    that kills it with this script, a timeout, and a monitor that kills it the moment one of its
    windows is visible or the foreground window, or its priority class changes;
  - the comparison: per frame, RGB exactly equal across runs, else max |delta| per channel, the
    count of differing pixels, their bounding box and a heat map (needs Pillow).

usage:
  python scripts/port/gfx_capture.py --runs 3 --lock <scratch>/m6a/gui.lock
  python scripts/port/gfx_capture.py --exe buildstage/m6a-H/out/Win32/Debug/main.exe --pace 0 --runs 1
  python scripts/port/gfx_capture.py --gal diligent:d3d11 --frames none --exit-frame 900
  python scripts/port/gfx_capture.py compare <capturesA> <capturesB>     # e.g. D3D9 vs Diligent

Output (default buildstage/gfx-capture/<timestamp>):
  run1..runN/   frame_000060.bmp ..., harness.json, harness.log, engine.log, run.json, Game.prefs,
                init_harness.lua, cache/, localappdata/, vault/
  report.json   per run: exit code, monitor findings, captures; per frame: identical or the deltas
  diff_*.png    heat maps of frames that differ (red = largest channel delta, scaled)

Exit code: 0 when every run completed and every frame is RGB-identical across runs, 1 when a run
failed or frames differ, 2 for usage errors or a refused start.
"""

import argparse
import ctypes
import datetime
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import time
from ctypes import wintypes

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_EXE = os.path.join(REPO, "output", "main-gfx", "Win32", "Debug", "main.exe")
DEFAULT_FAF = r"C:\ProgramData\FAForever"
DEFAULT_LOCK = os.path.join(REPO, "buildstage", "gfx-capture", "gui.lock")

EXIT_NAMES = {
    0: "complete",
    10: "refused by the harness (bad options, FAF_* variable, pin failed)",
    11: "visibility or focus violation (detected in the process)",
    12: "modal UI intercepted (message box or dialog: engine error)",
    13: "capture failed (frame not rendered or readback error)",
}

BELOW_NORMAL_PRIORITY_CLASS = 0x00004000
STARTF_USESHOWWINDOW = 0x00000001
STARTF_FORCEOFFFEEDBACK = 0x00000080
SW_HIDE = 0
SEM_FAILCRITICALERRORS = 0x0001
SEM_NOGPFAULTERRORBOX = 0x0002
SEM_NOOPENFILEERRORBOX = 0x8000

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
shell32 = ctypes.WinDLL("shell32", use_last_error=True)

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
user32.GetWindowThreadProcessId.restype = wintypes.DWORD
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetForegroundWindow.restype = wintypes.HWND
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.GetCursorPos.argtypes = [ctypes.POINTER(wintypes.POINT)]
kernel32.GetPriorityClass.argtypes = [wintypes.HANDLE]
kernel32.GetPriorityClass.restype = wintypes.DWORD
kernel32.CreateJobObjectW.restype = wintypes.HANDLE
kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
kernel32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]


class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD),
                ("SchedulingClass", wintypes.DWORD)]


class IO_COUNTERS(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint64) for n in ("ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                                                "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]


class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION), ("IoInfo", IO_COUNTERS),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


def local_appdata():
    buf = ctypes.create_unicode_buffer(260)
    if shell32.SHGetFolderPathW(None, 0x1C, None, 0, buf) != 0:  # CSIDL_LOCAL_APPDATA
        raise SystemExit("SHGetFolderPath(CSIDL_LOCAL_APPDATA) failed")
    return buf.value


def windows_of(pid):
    """(hwnd, visible, class) of every top-level window owned by `pid`."""
    found = []

    def callback(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            name = ctypes.create_unicode_buffer(128)
            user32.GetClassNameW(hwnd, name, 127)
            found.append((hwnd, bool(user32.IsWindowVisible(hwnd)), name.value))
        return True

    user32.EnumWindows(WNDENUMPROC(callback), 0)
    return found


def foreground_owner():
    hwnd = user32.GetForegroundWindow()
    if not hwnd:
        return None, 0
    owner = wintypes.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
    return hwnd, owner.value


# ---------------------------------------------------------------------------------------------------
# GUI lock


def acquire_lock(path, wait_seconds, label):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    deadline = time.time() + wait_seconds
    announced = False
    while True:
        try:
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        except FileExistsError:
            if time.time() > deadline:
                raise SystemExit(f"GUI lock {path} still held after {wait_seconds} s; not starting")
            if not announced:
                try:
                    holder = open(path, encoding="utf-8", errors="replace").read().strip()
                except OSError:
                    holder = "?"
                print(f"waiting for the GUI lock {path} (held by: {holder})", flush=True)
                announced = True
            time.sleep(2)
            continue
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            json.dump({"pid": os.getpid(), "owner": "scripts/port/gfx_capture.py", "run": label,
                       "since": datetime.datetime.now().isoformat(timespec="seconds")}, f)
        return


def release_lock(path):
    try:
        os.remove(path)
    except FileNotFoundError:
        pass


# ---------------------------------------------------------------------------------------------------
# run setup

PREFS_TEMPLATE = """-- Pinned preferences of one frame-harness run (scripts/port/gfx_capture.py). Never the user's Game.prefs.
version = {{ major = 1 }}
options_overrides = {{ language = 'us' }}
movie = {{ nologo = true }}
profile = {{
    current = 1,
    profiles = {{
        {{
            Name = 'harness',
            -- the first-start tutorial question (lua/ui/menus/main.lua TutorialPrompt)
            MenuTutorialPrompt = true,
            options = {{
                -- CreateDevice reads these when no /windowed is given; options.lua applies them at start
                primary_adapter = 'windowed',
                secondary_adapter = 'disabled',
                -- SC_ToggleCursorClip (the process's ClipCursor is stubbed under the harness anyway)
                lock_fullscreen_cursor_to_window = 0,
                -- no multisampled back buffer: GetRenderTargetData cannot read one
                antialiasing = 0,
                vsync = 0,
                -- ren_bloom off: bloom runs before the UI and the front end has no world (m6u-CRIT R6)
                bloom_render = 0,
                -- no background movie (it is also off with /nomovie)
                mainmenu_bgmovie = false,
                ui_scale = 1.0,
            }},
        }},
    }},
}}
"""

INIT_ANCHOR = "dofile(InitFileDir .. '/../fa_path.lua')"


def lua_path(path):
    return path.replace("\\", "/")


def write_init(run_dir, faf_dir, fa_path_snapshot):
    """A copy of FAF's init_faf.lua that runs from FAF's bin folder but touches nothing of the user's.

    fa_path.lua is read from a snapshot taken once per invocation: the FAF client rewrites the real one
    (GameType, GameVersion) whenever the user starts a game, and every run must see the same values."""
    source = os.path.join(faf_dir, "bin", "init_faf.lua")
    text = open(source, encoding="utf-8", errors="surrogateescape").read()
    if text.count(INIT_ANCHOR) != 1:
        raise SystemExit(f"{source}: expected the line {INIT_ANCHOR!r} once; the init file changed, check the patch")
    for needle in ("SHGetFolderPath('LOCAL_APPDATA')", 'rawget(_G, "SetProcessPriority")'):
        if needle not in text:
            raise SystemExit(f"{source}: {needle!r} not found; the init file changed, check the patch")
    local = lua_path(os.path.join(run_dir, "localappdata")) + "/"
    documents = lua_path(os.path.join(run_dir, "documents")) + "/"
    vault = lua_path(os.path.join(run_dir, "vault"))
    prologue = f"""-- Frame-harness prologue, written by scripts/port/gfx_capture.py. The rest of this file is
-- {lua_path(source)} unchanged except for the fa_path.lua line (a snapshot, and no user vault).
-- InitFileDir: the original resolves fa_path.lua and gamedata/ relative to FAF's bin folder.
InitFileDir = '{lua_path(os.path.join(faf_dir, "bin"))}'
-- Lines 31-70 of the original raise the process to HIGH_PRIORITY_CLASS and move its affinity; the
-- harness stays below normal (the engine's SetPriorityClass lookup is also stubbed in the process).
SetProcessPriority = nil
GetProcessAffinityMask = nil
SetProcessAffinityMask = nil
-- The original prunes LOCAL_APPDATA/.../cache and mounts LOCAL_APPDATA/... as /preferences; both
-- go to this run's scratch directory instead of the user's folder.
local __harness_SHGetFolderPath = SHGetFolderPath
SHGetFolderPath = function(name, create)
    if name == 'LOCAL_APPDATA' then return '{local}' end
    if name == 'PERSONAL' then return '{documents}' end
    return __harness_SHGetFolderPath(name, create)
end
"""
    patched = text.replace(INIT_ANCHOR, INIT_ANCHOR + f"\ncustom_vault_path = '{vault}' -- harness: no user maps or mods", 1)
    path = os.path.join(run_dir, "init_harness.lua")
    with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="\n") as f:
        f.write(prologue + patched)
    for sub in ("localappdata/Gas Powered Games/Supreme Commander Forged Alliance/cache", "documents",
                "vault/maps", "vault/mods", "cache"):
        os.makedirs(os.path.join(run_dir, sub), exist_ok=True)
    return path


def prefs_argument(prefs_file):
    """/prefs is a name inside <LOCALAPPDATA>\\Gas Powered Games\\Supreme Commander Forged Alliance
    (USER_LoadPreferences appends it, StartupHelpers.cpp:6030-6040), so the scratch file is passed as a
    path relative to that folder."""
    pref_dir = os.path.join(local_appdata(), "Gas Powered Games", "Supreme Commander Forged Alliance")
    if os.path.splitdrive(pref_dir)[0].lower() != os.path.splitdrive(prefs_file)[0].lower():
        raise SystemExit(f"the run directory must be on the drive of {pref_dir}")
    rel = os.path.relpath(prefs_file, pref_dir)
    if os.path.isabs(rel) or not os.path.normpath(os.path.join(pref_dir, rel)) == os.path.normpath(prefs_file):
        raise SystemExit(f"cannot express {prefs_file} relative to {pref_dir}")
    return rel


def exe_has_harness(exe):
    data = open(exe, "rb").read()
    return "galharness".encode("utf-16-le") in data


# ---------------------------------------------------------------------------------------------------
# one run


def run_once(args, index, out_dir):
    run_dir = os.path.join(out_dir, f"run{index}")
    os.makedirs(run_dir, exist_ok=True)
    prefs = os.path.join(run_dir, "Game.prefs")
    with open(prefs, "w", encoding="utf-8", newline="\n") as f:
        f.write(PREFS_TEMPLATE.format())
    init = write_init(run_dir, args.faf_dir, args.fa_path_snapshot)

    command = [args.exe,
               "/galharness", run_dir,
               "/galframes", args.frames,
               "/galpace", str(args.pace),
               "/galseed", str(args.seed),
               "/windowed", str(args.width), str(args.height),
               "/framerate", str(args.framerate),
               "/nomovie", "/nosound", "/nomusic", "/nobugreport",
               "/init", init,
               "/prefs", prefs_argument(prefs),
               "/cachedir", os.path.join(run_dir, "cache"),
               "/log", os.path.join(run_dir, "engine.log")]
    if args.exit_frame:
        command += ["/galexitframe", str(args.exit_frame)]
    if args.gal:
        command += ["/gal", args.gal]
    if args.no_pins:
        command += ["/galnopins"]
    command += args.extra

    info = subprocess.STARTUPINFO()
    info.dwFlags = STARTF_USESHOWWINDOW | STARTF_FORCEOFFFEEDBACK
    info.wShowWindow = SW_HIDE

    record = {"run": index, "dir": run_dir, "command": subprocess.list2cmdline(command), "monitor": []}
    fg_before = foreground_owner()

    acquire_lock(args.lock, args.lock_wait, f"run{index} {run_dir}")
    try:
        job = kernel32.CreateJobObjectW(None, None)
        limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
        limits.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        kernel32.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits))
        started = time.time()
        proc = subprocess.Popen(command, cwd=run_dir, startupinfo=info,
                                creationflags=BELOW_NORMAL_PRIORITY_CLASS,
                                stdout=subprocess.DEVNULL, stderr=open(os.path.join(run_dir, "stderr.txt"), "wb"))
        kernel32.AssignProcessToJobObject(job, int(proc._handle))
        killed = None
        # Whether the real cursor moved during the run (the user works or plays meanwhile): only a
        # count of distinct positions is kept, as evidence that pointer input cannot reach the run.
        cursor_positions = set()
        point = wintypes.POINT()
        while proc.poll() is None:
            if user32.GetCursorPos(ctypes.byref(point)):
                cursor_positions.add((point.x, point.y))
            for hwnd, visible, cls in windows_of(proc.pid):
                if visible:
                    killed = f"window visible: hwnd={hwnd:#x} class={cls}"
            fg_hwnd, fg_pid = foreground_owner()
            if fg_pid == proc.pid:
                killed = f"foreground window belongs to the harness: hwnd={fg_hwnd:#x}"
            priority = kernel32.GetPriorityClass(int(proc._handle))
            if priority and priority != BELOW_NORMAL_PRIORITY_CLASS:
                killed = f"priority class changed to {priority:#x}"
            if time.time() - started > args.timeout:
                killed = f"timeout after {args.timeout} s"
            if killed:
                proc.kill()
                record["monitor"].append(killed)
                print(f"  run{index}: KILLED - {killed}", flush=True)
                break
            time.sleep(0.05)
        proc.wait()
        kernel32.CloseHandle(job)
        record["seconds"] = round(time.time() - started, 1)
        record["cursor_positions_seen"] = len(cursor_positions)
    finally:
        release_lock(args.lock)

    fg_after = foreground_owner()
    record["foreground_before_after"] = [fg_before[0] or 0, fg_after[0] or 0]
    record["exit_code"] = proc.returncode
    record["exit_meaning"] = EXIT_NAMES.get(proc.returncode, "unexpected exit (crash or killed)")
    summary_path = os.path.join(run_dir, "harness.json")
    record["summary"] = json.load(open(summary_path, encoding="utf-8")) if os.path.exists(summary_path) else None
    with open(os.path.join(run_dir, "run.json"), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2)
    return record


# ---------------------------------------------------------------------------------------------------
# images


def read_bmp(path):
    """(width, height, top-down BGRA bytes) of the harness's 32-bit BI_RGB bottom-up BMP."""
    data = open(path, "rb").read()
    if data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height, _, bpp, compression = struct.unpack_from("<iiHHI", data, 18)
    if bpp != 32 or compression != 0:
        raise ValueError(f"{path}: expected 32-bit BI_RGB")
    rows = abs(height)
    stride = width * 4
    pixels = data[offset:offset + stride * rows]
    if height > 0:
        pixels = b"".join(pixels[(rows - 1 - r) * stride:(rows - r) * stride] for r in range(rows))
    return width, rows, pixels


def rgb_of(bgra):
    out = bytearray(len(bgra) // 4 * 3)
    out[0::3] = bgra[2::4]
    out[1::3] = bgra[1::4]
    out[2::3] = bgra[0::4]
    return bytes(out)


def fnv1a64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


def compare_images(path_a, path_b, heatmap_path=None):
    wa, ha, a = read_bmp(path_a)
    wb, hb, b = read_bmp(path_b)
    if (wa, ha) != (wb, hb):
        return {"identical": False, "size_mismatch": [[wa, ha], [wb, hb]]}
    ra, rb = rgb_of(a), rgb_of(b)
    alpha_equal = a[3::4] == b[3::4]
    if ra == rb:
        return {"identical": True, "alpha_identical": alpha_equal}
    result = {"identical": False, "alpha_identical": alpha_equal}
    try:
        from PIL import Image, ImageChops
    except ImportError:
        diff = [abs(x - y) for x, y in zip(ra, rb)]
        result["max_abs_delta_rgb"] = [max(diff[c::3]) for c in range(3)]
        px = [max(diff[i:i + 3]) for i in range(0, len(diff), 3)]
        result["pixels_differing"] = sum(1 for d in px if d)
        result["pixels_total"] = len(px)
        return result
    ia = Image.frombytes("RGB", (wa, ha), ra)
    ib = Image.frombytes("RGB", (wa, ha), rb)
    diff = ImageChops.difference(ia, ib)
    result["max_abs_delta_rgb"] = [hi for _, hi in diff.getextrema()]
    r, g, bb = diff.split()
    peak = ImageChops.lighter(ImageChops.lighter(r, g), bb)
    hist = peak.histogram()
    result["pixels_differing"] = wa * ha - hist[0]
    result["pixels_delta_gt1"] = sum(hist[2:])
    result["pixels_total"] = wa * ha
    result["bbox"] = list(peak.getbbox() or [])
    if heatmap_path:
        top = max(1, max(result["max_abs_delta_rgb"]))
        scaled = peak.point(lambda v: 0 if v == 0 else 64 + int(191 * v / top))
        heat = Image.merge("RGB", (scaled, Image.new("L", scaled.size, 0), Image.new("L", scaled.size, 0)))
        Image.blend(ia.convert("RGB"), heat, 0.75).save(heatmap_path)
        result["heatmap"] = heatmap_path
    return result


# ---------------------------------------------------------------------------------------------------


def compare_runs(records, out_dir):
    frames = {}
    complete = [r for r in records if r["exit_code"] == 0 and r["summary"] and r["summary"].get("status") == "complete"]
    if not complete:
        return {}, False
    base = complete[0]
    ok = len(complete) == len(records)
    for capture in base["summary"]["captures"]:
        frame = capture["frame"]
        name = capture["file"]
        entry = {"frame": frame, "reference": f"run{base['run']}", "rgb_fnv1a64": {}, "vs_reference": {}}
        for rec in complete:
            entry["rgb_fnv1a64"][f"run{rec['run']}"] = next(
                (c["rgb_fnv1a64"] for c in rec["summary"]["captures"] if c["frame"] == frame), None)
        for rec in complete[1:]:
            path_a = os.path.join(base["dir"], name)
            path_b = os.path.join(rec["dir"], name)
            if not os.path.exists(path_b):
                entry["vs_reference"][f"run{rec['run']}"] = {"identical": False, "missing": True}
                ok = False
                continue
            heat = os.path.join(out_dir, f"diff_{frame:06d}_run{base['run']}_run{rec['run']}.png")
            res = compare_images(path_a, path_b, heat)
            entry["vs_reference"][f"run{rec['run']}"] = res
            ok = ok and res["identical"]
        frames[str(frame)] = entry
    return frames, ok


def cmd_compare(argv):
    parser = argparse.ArgumentParser(prog="gfx_capture.py compare",
                                     description="Compare the frame_*.bmp of two capture directories.")
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--out", default=None, help="where heat maps go (default: next to b)")
    args = parser.parse_args(argv)
    out = args.out or args.b
    os.makedirs(out, exist_ok=True)
    names = sorted(n for n in os.listdir(args.a) if re.fullmatch(r"frame_\d+\.bmp", n))
    report = {}
    all_equal = True
    for name in names:
        path_b = os.path.join(args.b, name)
        if not os.path.exists(path_b):
            report[name] = {"missing_in_b": True}
            all_equal = False
            continue
        res = compare_images(os.path.join(args.a, name), path_b, os.path.join(out, "diff_" + name[:-4] + ".png"))
        report[name] = res
        all_equal = all_equal and res["identical"]
        print(f"{name}: {json.dumps(res)}")
    print("identical" if all_equal else "different")
    return 0 if all_equal else 1


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "compare":
        return cmd_compare(sys.argv[2:])

    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--exe", default=DEFAULT_EXE, help="FAF_PORT_GRAPHICS main.exe (default: %(default)s)")
    parser.add_argument("--faf-dir", default=DEFAULT_FAF, help="FAF install, read only (default: %(default)s)")
    parser.add_argument("--out", default=None, help="output directory (default: buildstage/gfx-capture/<time>)")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--frames", default="60,300,900", help="capture frames, comma list, or none")
    parser.add_argument("--exit-frame", type=int, default=0, help="default: the last capture frame")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--framerate", type=int, default=30, help="the engine's fixed frame rate (/framerate)")
    parser.add_argument("--pace", type=float, default=30.0, help="real-time cap in frames/s, 0 = none")
    parser.add_argument("--seed", type=lambda s: int(s, 0), default=0x6D366100)
    parser.add_argument("--gal", default=None, help="passed as /gal <api> (the Diligent backend, step 1)")
    parser.add_argument("--no-pins", action="store_true",
                        help="/galnopins: wall-clock Lua time and time-seeded random, to show what the pins fix")
    parser.add_argument("--timeout", type=float, default=600.0, help="seconds per run before the process is killed")
    parser.add_argument("--lock", default=os.environ.get("GFX_GUI_LOCK", DEFAULT_LOCK),
                        help="the machine-wide GUI-run lock file (default: $GFX_GUI_LOCK or %(default)s)")
    parser.add_argument("--lock-wait", type=float, default=3600.0, help="seconds to wait for the lock")
    parser.add_argument("extra", nargs="*", help="more engine options, after --")
    args = parser.parse_args()

    faf_vars = sorted(k for k in os.environ if k.upper().startswith("FAF_"))
    if faf_vars:
        print(f"refusing to run: FAF_* environment variables are set ({', '.join(faf_vars)}); the committed "
              "render-path probes arm themselves from them", file=sys.stderr)
        return 2
    args.exe = os.path.abspath(args.exe)
    if not os.path.isfile(args.exe):
        print(f"no exe at {args.exe}", file=sys.stderr)
        return 2
    if os.path.normcase(os.path.abspath(args.faf_dir)) in os.path.normcase(args.exe):
        print("refusing to run an exe inside the FAF install (the user's main.exe)", file=sys.stderr)
        return 2
    if not exe_has_harness(args.exe):
        print(f"refusing to run: {args.exe} has no frame harness (build it with /p:FafPortGraphics=true); without "
              "it the window would be shown", file=sys.stderr)
        return 2
    if not os.path.isfile(os.path.join(args.faf_dir, "bin", "init_faf.lua")):
        print(f"no init_faf.lua under {args.faf_dir}", file=sys.stderr)
        return 2

    # No WER or critical-error dialogs for anything started from here (the child inherits the mode).
    kernel32.SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX)

    out_dir = os.path.abspath(args.out or os.path.join(
        REPO, "buildstage", "gfx-capture", datetime.datetime.now().strftime("%Y%m%d-%H%M%S")))
    os.makedirs(out_dir, exist_ok=True)
    args.fa_path_snapshot = os.path.join(out_dir, "fa_path_snapshot.lua")
    with open(os.path.join(args.faf_dir, "fa_path.lua"), "rb") as src, open(args.fa_path_snapshot, "wb") as dst:
        dst.write(src.read())
    exe_sha = hashlib.sha256(open(args.exe, "rb").read()).hexdigest()
    print(f"exe {args.exe} sha256 {exe_sha[:16]}...; output {out_dir}", flush=True)

    records = []
    for index in range(1, args.runs + 1):
        rec = run_once(args, index, out_dir)
        records.append(rec)
        s = rec["summary"] or {}
        caps = ", ".join(f"{c['frame']}:{c['rgb_fnv1a64']}" for c in s.get("captures", []))
        sandbox = s.get("sandbox", {})
        print(f"  run{index}: exit {rec['exit_code']} ({rec['exit_meaning']}), {rec.get('seconds')} s, "
              f"cursor positions seen {rec.get('cursor_positions_seen')}, input dropped "
              f"{sandbox.get('input_messages_dropped')}, activations blocked {sandbox.get('activations_blocked')}, "
              f"violations {len(sandbox.get('violations', [])) + len(rec['monitor'])}, "
              f"status {s.get('status')}, frames {s.get('app_frames')}, captures [{caps}]"
              + (f", reason: {s.get('reason')}" if s.get("reason") else ""), flush=True)

    frames, identical = compare_runs(records, out_dir)
    all_complete = all(r["exit_code"] == 0 and r["summary"] and r["summary"].get("status") == "complete"
                       for r in records)
    report = {"exe": args.exe, "exe_sha256": exe_sha, "runs": records, "frames": frames,
              "all_runs_complete": all_complete, "rgb_identical_across_runs": identical and all_complete}
    with open(os.path.join(out_dir, "report.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    for frame, entry in frames.items():
        verdicts = ", ".join(f"{k}: {'identical' if v.get('identical') else 'DIFFERENT ' + json.dumps(v)}"
                             for k, v in entry["vs_reference"].items())
        print(f"frame {frame}: {verdicts or 'single run'}")
    print("GATE: " + ("PASS - every frame RGB-identical across runs" if report["rgb_identical_across_runs"]
                      else "FAIL"))
    return 0 if report["rgb_identical_across_runs"] else 1


if __name__ == "__main__":
    sys.exit(main())
