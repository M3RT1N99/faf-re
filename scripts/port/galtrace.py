#!/usr/bin/env python3
"""galtrace: record the gal call stream of a frame-harness run, replay it with galplay, compare.

M6c step 5 (docs/port/renderer.md, port/graphics/trace/README.md). The recorder and galplay are part
of the graphics main.exe (FafPortGraphics); this script runs them the way gfx_capture.py runs the
harness: one GUI run at a time under the shared lock file, hidden, below-normal priority, inside a
job object, watched for visible or foreground windows.

  record    the frame harness (gfx_capture.py) with /galtrace: <out>/trace.galtrace, the harness's
            own captures in <out>/live/run1, and <out>/trace.json from galtrace-dump
  play      galplay: main.exe /galplay <trace> into D3D9 (default) or /gal diligent:<api>;
            <out>/frame_<N>.bmp per readback, galplay.json, galplay.log
  compare   galplay's BMPs against reference captures: byte for byte, and the parity rule
            (gfx_capture.py parity: max |delta| <= 1 on <= 0.1 % of the pixels) with heat maps
  dump      galtrace-dump on a trace (validation, op counts, content digest, payload kinds)
  tools     build galtrace-dump, galtrace-refs and the format unit test (CMake, Win32 and x64)
  gate      the step-5 gate: record a trace, replay it into every backend asked for, compare each
            replay with its references, record a second trace and compare the content digests
  refs      galtrace-refs (M7a1): convert a recording into a version 2 trace that refers to the game
            files instead of holding them, check one, extract its game files, set metadata
  release   M7a1's release trace: record (or take) a 900-frame menu recording, convert it to version 2,
            check that it holds no game data, prove that its replays into every backend are
            byte-identical to the recording's, store the PC's frame hashes (reference_frames.d3d9 from
            the recording, reference_frames.diligent:vk from a replay into Vulkan) in its header and
            write it to buildstage/traces/ with a provenance file

usage:
  python scripts/port/galtrace.py record --exe EXE --out DIR [--frames 10,...,900] [--gal API] [--lock L]
  python scripts/port/galtrace.py play --exe EXE --trace T --out DIR [--gal diligent:vk] [--data DIR] [--lock L] [-- options]
  python scripts/port/galtrace.py compare --play DIR --ref DIR [--ref DIR ...] [--exact] --out DIR
  python scripts/port/galtrace.py dump --trace T [--json F] [--records N] [--data DIR]
  python scripts/port/galtrace.py tools [--build DIR]
  python scripts/port/galtrace.py gate --exe EXE --out DIR --backends d3d9,diligent:d3d11,diligent:vk,diligent:gl [--live DIR ...]
  python scripts/port/galtrace.py refs convert|check|extract|setmeta|refs ... (galtrace-refs' own arguments;
         --init defaults to the FAF install's init_faf.lua)
  python scripts/port/galtrace.py release --exe EXE --work DIR [--recording T] [--dest buildstage/traces/menu.galtrace]
         [--prove d3d9,diligent:d3d11,diligent:vk,diligent:gl] [--lock L]

Version 2 traces (port/graphics/trace/README.md) name the game's files by VFS path and content hash;
`play --data DIR` gives galplay the tree `refs extract` writes (DIR/<vfs path>, lower case).

Every GUI run names the machine's shared lock (--lock or GFX_GUI_LOCK). FAF_* variables refuse the
run, as in gfx_capture.py (the committed render-path probes arm themselves from them).
"""

import argparse
import datetime
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import gfx_capture  # noqa: E402  (window monitor, job objects, the GUI lock, BMP reading)

DEFAULT_EXE = gfx_capture.DEFAULT_EXE
DEFAULT_LOCK = gfx_capture.DEFAULT_LOCK
GATE_FRAMES = "10,15,20,25,30,45,60,300,900"
TOOLS_DIR = os.path.join(REPO, "buildstage", "galtrace-tools")
TRACE_SRC = os.path.join(REPO, "port", "graphics", "trace")

PLAY_EXIT = {
    0: "complete, every readback equals the recording",
    2: "replay stopped early or the device could not be created",
    3: "bad options",
    4: "complete, some readback differs from the recording",
    5: "a game file the version 2 trace refers to is missing from --data or differs",
}
DEFAULT_INIT = "C:/ProgramData/FAForever/bin/init_faf.lua"  # the FAF client's data-path script (the harness mounts the same)
DEFAULT_RELEASE = os.path.join(REPO, "buildstage", "traces", "menu.galtrace")
REFERENCE_BACKEND = "diligent:vk"  # the phone compares its frames with the PC's frames of this backend


def refuse_faf_vars():
    names = sorted(k for k in os.environ if k.upper().startswith("FAF_"))
    if names:
        raise SystemExit(f"refusing to run: FAF_* environment variables are set ({', '.join(names)})")


def sha256(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


# ---------------------------------------------------------------------------------------------------
# tools


def tool_exe(name, build_dir=None, arch="x64"):
    base = build_dir or os.path.join(TOOLS_DIR, arch)
    for config in ("Release", "Debug"):
        path = os.path.join(base, config, name + ".exe")
        if os.path.isfile(path):
            return path
    return None


def dump_exe(build_dir=None, arch="x64"):
    return tool_exe("galtrace-dump", build_dir, arch)


def refs_exe(explicit=None):
    """galtrace-refs: --refs-exe, GALTRACE_REFS, else the tools build (x64 first)."""
    for path in (explicit, os.environ.get("GALTRACE_REFS")):
        if path and os.path.isfile(path):
            return os.path.abspath(path)
    exe = tool_exe("galtrace-refs") or tool_exe("galtrace-refs", arch="win32")
    if exe is None:
        raise SystemExit("galtrace-refs not built: python scripts/port/galtrace.py tools")
    return exe


def cmd_tools(argv):
    p = argparse.ArgumentParser(prog="galtrace.py tools")
    p.add_argument("--build", default=TOOLS_DIR, help="build root (default %(default)s); <root>/<arch> per architecture")
    p.add_argument("--arch", default="Win32,x64")
    a = p.parse_args(argv)
    ok = True
    for arch in a.arch.split(","):
        out = os.path.join(a.build, arch.lower() if arch != "Win32" else "win32")
        subprocess.run(["cmake", "-S", TRACE_SRC, "-B", out, "-A", arch], check=True, stdout=subprocess.DEVNULL)
        r = subprocess.run(["cmake", "--build", out, "--config", "Release"], capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout[-4000:], r.stderr[-2000:])
            ok = False
            continue
        t = subprocess.run([os.path.join(out, "Release", "galtrace_format_test.exe"), out], capture_output=True, text=True)
        refs = os.path.join(out, "Release", "galtrace-refs.exe")
        print(f"{arch}: built in {out}; format test: {t.stdout.strip().splitlines()[-1] if t.stdout else t.returncode}; "
              f"galtrace-refs {'built' if os.path.isfile(refs) else 'NOT built (port/native missing?)'}")
        ok = ok and t.returncode == 0
    return 0 if ok else 1


def run_dump(trace, json_path=None, records=None, exe=None, data=None):
    exe = exe or dump_exe() or dump_exe(arch="win32")
    if exe is None:
        raise SystemExit("galtrace-dump not built: python scripts/port/galtrace.py tools")
    cmd = [exe, trace]
    if json_path:
        cmd += ["--json", json_path]
    if records is not None:
        cmd += ["--records", str(records)]
    if data:
        cmd += ["--data", data]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout


def cmd_dump(argv):
    p = argparse.ArgumentParser(prog="galtrace.py dump")
    p.add_argument("--trace", required=True)
    p.add_argument("--json")
    p.add_argument("--records", type=int)
    p.add_argument("--data", help="version 2: the game files as galtrace-refs extract writes them; references are read and checked")
    p.add_argument("--dump-exe")
    a = p.parse_args(argv)
    code, out = run_dump(a.trace, a.json, a.records, a.dump_exe, a.data)
    print(out, end="")
    return code


def run_refs(args, exe=None, log=None):
    """galtrace-refs with `args`; returns (exit code, stdout, stderr)."""
    cmd = [refs_exe(exe)] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if log:
        with open(log, "w", encoding="utf-8") as f:
            f.write(subprocess.list2cmdline(cmd) + "\n\n" + r.stdout + "\n--- stderr\n" + r.stderr)
    return r.returncode, r.stdout, r.stderr


def cmd_refs(argv):
    """galtrace.py refs <command> ...: galtrace-refs, with --init defaulting to the FAF install's script."""
    if not argv or argv[0] in ("-h", "--help"):
        print("usage: galtrace.py refs [--refs-exe EXE] convert|check|extract|setmeta|refs <galtrace-refs arguments>")
        code, out, err = run_refs([])
        print(err, end="")
        return 2
    exe = None
    if argv[0] == "--refs-exe":
        exe, argv = argv[1], argv[2:]
    args = list(argv)
    if args and args[0] in ("convert", "check", "extract") and "--init" not in args:
        args += ["--init", DEFAULT_INIT]
    code, out, err = run_refs(args, exe)
    print(out, end="")
    print(err, end="", file=sys.stderr)
    return code


# ---------------------------------------------------------------------------------------------------
# record


def record(exe, out, frames, gal, lock, extra, trace_frames=0, timeout=900.0):
    """The frame harness with /galtrace; returns (exit code, trace path, gfx_capture report)."""
    os.makedirs(out, exist_ok=True)
    trace = os.path.join(out, "trace.galtrace")
    live = os.path.join(out, "live")
    cmd = [sys.executable, os.path.join(HERE, "gfx_capture.py"), "--exe", exe, "--frames", frames, "--runs", "1",
           "--pace", "0", "--lock", lock, "--out", live, "--timeout", str(timeout)]
    if gal and gal != "d3d9":
        cmd += ["--gal", gal]
    cmd += ["--", "/galtrace", trace]
    if trace_frames:
        cmd += ["/galtraceframes", str(trace_frames)]
    cmd += list(extra)
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    started = time.time()
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    with open(os.path.join(out, "record.log"), "w", encoding="utf-8") as f:
        f.write(" ".join(cmd) + "\n\n" + r.stdout + "\n" + r.stderr)
    report_path = os.path.join(live, "report.json")
    report = json.load(open(report_path, encoding="utf-8")) if os.path.exists(report_path) else None
    print(f"record: gfx_capture exit {r.returncode} in {time.time() - started:.0f} s; trace {trace} "
          f"({os.path.getsize(trace) if os.path.exists(trace) else 0} bytes)", flush=True)
    for line in r.stdout.splitlines():
        if line.strip().startswith("run1:") or line.startswith("GATE"):
            print("  " + line.strip())
    return r.returncode, trace, report


def cmd_record(argv):
    p = argparse.ArgumentParser(prog="galtrace.py record")
    p.add_argument("--exe", default=DEFAULT_EXE)
    p.add_argument("--out", required=True)
    p.add_argument("--frames", default=GATE_FRAMES)
    p.add_argument("--gal", default="d3d9", help="d3d9 (default) or diligent:<api>")
    p.add_argument("--trace-frames", type=int, default=0, help="/galtraceframes: end the trace after this many presents")
    p.add_argument("--lock", default=os.environ.get("GFX_GUI_LOCK", DEFAULT_LOCK))
    p.add_argument("--timeout", type=float, default=900.0)
    p.add_argument("extra", nargs="*")
    a = p.parse_args(argv)
    refuse_faf_vars()
    code, trace, _ = record(os.path.abspath(a.exe), os.path.abspath(a.out), a.frames, a.gal, a.lock, a.extra,
                            a.trace_frames, a.timeout)
    if os.path.exists(trace):
        dcode, out = run_dump(trace, os.path.join(a.out, "trace.json"))
        print(out.strip().splitlines()[-2] if out.strip() else "", "\n" + (out.strip().splitlines()[-1] if out.strip() else ""))
        code = code or dcode
    return code


# ---------------------------------------------------------------------------------------------------
# play


def play(exe, trace, out, gal, lock, extra, timeout=900.0, lock_wait=3600.0, data=None):
    """main.exe /galplay, hidden and watched like a harness run; returns the run record. `data`: the game
    files a version 2 trace refers to (/galplaydata, the tree galtrace-refs extract writes)."""
    os.makedirs(out, exist_ok=True)
    cmd = [exe, "/galplay", os.path.abspath(trace), "/galplayout", os.path.abspath(out)]
    if gal and gal != "d3d9":
        cmd += ["/gal", gal]
    if data:
        cmd += ["/galplaydata", os.path.abspath(data)]
    cmd += list(extra)
    info = subprocess.STARTUPINFO()
    info.dwFlags = gfx_capture.STARTF_USESHOWWINDOW | gfx_capture.STARTF_FORCEOFFFEEDBACK
    info.wShowWindow = gfx_capture.SW_HIDE
    k32 = gfx_capture.kernel32
    rec = {"command": subprocess.list2cmdline(cmd), "monitor": []}
    fg_before = gfx_capture.foreground_owner()
    gfx_capture.acquire_lock(lock, lock_wait, f"galplay {out}")
    try:
        job = k32.CreateJobObjectW(None, None)
        limits = gfx_capture.JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
        limits.BasicLimitInformation.LimitFlags = 0x2000  # KILL_ON_JOB_CLOSE
        k32.SetInformationJobObject(job, 9, gfx_capture.ctypes.byref(limits), gfx_capture.ctypes.sizeof(limits))
        started = time.time()
        proc = subprocess.Popen(cmd, cwd=out, startupinfo=info, creationflags=gfx_capture.BELOW_NORMAL_PRIORITY_CLASS,
                                stdout=subprocess.DEVNULL, stderr=open(os.path.join(out, "stderr.txt"), "wb"))
        k32.AssignProcessToJobObject(job, int(proc._handle))
        killed = None
        while proc.poll() is None:
            for hwnd, visible, cls in gfx_capture.windows_of(proc.pid):
                if visible:
                    killed = f"window visible: hwnd={hwnd:#x} class={cls}"
            fg_hwnd, fg_pid = gfx_capture.foreground_owner()
            if fg_pid == proc.pid:
                killed = f"foreground window belongs to galplay: hwnd={fg_hwnd:#x}"
            if time.time() - started > timeout:
                killed = f"timeout after {timeout} s"
            if killed:
                proc.kill()
                rec["monitor"].append(killed)
                break
            time.sleep(0.05)
        proc.wait()
        k32.CloseHandle(job)
        rec["seconds"] = round(time.time() - started, 1)
    finally:
        gfx_capture.release_lock(lock)
    rec["foreground_before_after"] = [fg_before[0] or 0, gfx_capture.foreground_owner()[0] or 0]
    rec["exit_code"] = proc.returncode
    rec["exit_meaning"] = PLAY_EXIT.get(proc.returncode, "unexpected exit (crash or killed)")
    summary = os.path.join(out, "galplay.json")
    rec["galplay"] = json.load(open(summary, encoding="utf-8")) if os.path.exists(summary) else None
    with open(os.path.join(out, "play.json"), "w", encoding="utf-8") as f:
        json.dump(rec, f, indent=2)
    g = rec["galplay"] or {}
    mism = g.get("mismatches", {})
    refs = ""
    if g.get("format_version", 1) >= 2:
        refs = (f", format v{g.get('format_version')}, {g.get('references_read')} game files read, "
                f"{g.get('provided_payloads')} backend outputs provided")
    if g.get("readbacks_with_reference"):
        refs += (f", reference_frames.{g.get('reference_backend')}: {g.get('readbacks_pass')}/"
                 f"{g.get('readbacks_with_reference')} pass")
    if g.get("missing_data"):
        refs += f", MISSING DATA: {g.get('fatal')}"
    print(f"play {gal or 'd3d9'}: exit {proc.returncode} ({rec['exit_meaning']}), {rec.get('seconds')} s, "
          f"{g.get('records')} records, {g.get('presents')} presents, {g.get('draws')} draws, readbacks "
          f"{g.get('readbacks_identical')}/{len(g.get('readbacks', []))} identical to the recording{refs}, "
          f"gal errors {g.get('gal_errors')}, mismatches {mism or 'none'}, monitor {rec['monitor'] or 'clean'}", flush=True)
    return rec


def cmd_play(argv):
    p = argparse.ArgumentParser(prog="galtrace.py play")
    p.add_argument("--exe", default=DEFAULT_EXE)
    p.add_argument("--trace", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--gal", default="d3d9")
    p.add_argument("--data", help="version 2: the game files (galtrace-refs extract's tree), /galplaydata")
    p.add_argument("--lock", default=os.environ.get("GFX_GUI_LOCK", DEFAULT_LOCK))
    p.add_argument("--timeout", type=float, default=900.0)
    p.add_argument("extra", nargs="*")
    a = p.parse_args(argv)
    refuse_faf_vars()
    rec = play(os.path.abspath(a.exe), a.trace, os.path.abspath(a.out), a.gal, a.lock, a.extra, a.timeout, data=a.data)
    return 0 if rec["exit_code"] in (0, 4) and not rec["monitor"] else 1


# ---------------------------------------------------------------------------------------------------
# compare


def compare(play_dir, refs, out, exact):
    """Per readback BMP of `play_dir`: byte equality with each reference's BMP of the same frame,
    and the parity rule through gfx_capture.py parity. Returns (pass, result dict)."""
    os.makedirs(out, exist_ok=True)
    summary = os.path.join(play_dir, "galplay.json")
    if not os.path.exists(summary):
        print(f"compare: no galplay.json in {play_dir}", flush=True)
        return False, {"play": play_dir, "error": "no galplay.json"}
    g = json.load(open(summary, encoding="utf-8"))
    frames = [r["frame"] for r in g.get("readbacks", []) if r.get("file")]
    result = {"play": play_dir, "frames": frames, "references": {}, "galplay_completed": g.get("completed")}
    ok = bool(frames) and g.get("completed")
    for ref in refs:
        entry = {"frames": {}}
        for frame in frames:
            name = f"frame_{frame:06d}.bmp"
            a, b = os.path.join(ref, name), os.path.join(play_dir, name)
            if not os.path.exists(a) or not os.path.exists(b):
                entry["frames"][str(frame)] = {"error": "BMP missing: " + (a if not os.path.exists(a) else b)}
                ok = False
                continue
            same = open(a, "rb").read() == open(b, "rb").read()
            entry["frames"][str(frame)] = {"bmp_identical": same}
        # The parity rule, with heat maps: gfx_capture.py parity <reference run dir> <backend run dir>.
        pout = os.path.join(out, "parity_" + hashlib.sha1(os.path.abspath(ref).encode()).hexdigest()[:8])
        r = subprocess.run([sys.executable, os.path.join(HERE, "gfx_capture.py"), "parity", ref, play_dir, "--out", pout],
                           capture_output=True, text=True)
        entry["parity_exit"] = r.returncode
        entry["parity_dir"] = pout
        entry["parity_tail"] = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()[-300:]
        identical = all(v.get("bmp_identical") for v in entry["frames"].values())
        entry["all_bmp_identical"] = identical
        entry["pass"] = identical if exact else (r.returncode == 0)
        ok = ok and entry["pass"]
        result["references"][ref] = entry
        print(f"compare {play_dir} vs {ref}: BMPs identical {sum(1 for v in entry['frames'].values() if v.get('bmp_identical'))}"
              f"/{len(frames)}; parity: {entry['parity_tail']}", flush=True)
    result["pass"] = bool(ok)
    with open(os.path.join(out, "compare.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    return bool(ok), result


def cmd_compare(argv):
    p = argparse.ArgumentParser(prog="galtrace.py compare")
    p.add_argument("--play", required=True)
    p.add_argument("--ref", action="append", required=True)
    p.add_argument("--exact", action="store_true", help="require byte-identical BMPs (default: the parity rule)")
    p.add_argument("--out", required=True)
    a = p.parse_args(argv)
    ok, _ = compare(os.path.abspath(a.play), [os.path.abspath(r) for r in a.ref], os.path.abspath(a.out), a.exact)
    print("COMPARE: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


# ---------------------------------------------------------------------------------------------------
# gate


def cmd_gate(argv):
    p = argparse.ArgumentParser(prog="galtrace.py gate")
    p.add_argument("--exe", default=DEFAULT_EXE)
    p.add_argument("--out", required=True)
    p.add_argument("--frames", default=GATE_FRAMES)
    p.add_argument("--record-gal", default="d3d9", help="the backend the trace is recorded on")
    p.add_argument("--backends", default="d3d9,diligent:d3d11,diligent:vk,diligent:gl")
    p.add_argument("--live", action="append", default=[],
                   help="backend=run dir of a live harness capture of that backend (e.g. diligent:vk=<dir>/run1)")
    p.add_argument("--trace", help="replay this trace instead of recording one")
    p.add_argument("--reference", help="with --trace: the D3D9 harness run dir the trace was recorded in (frame BMPs)")
    p.add_argument("--determinism", action="store_true", help="record a second trace and compare content digests")
    p.add_argument("--lock", default=os.environ.get("GFX_GUI_LOCK", DEFAULT_LOCK))
    p.add_argument("--timeout", type=float, default=900.0)
    a = p.parse_args(argv)
    refuse_faf_vars()
    exe = os.path.abspath(a.exe)
    out = os.path.abspath(a.out)
    os.makedirs(out, exist_ok=True)
    print(f"exe {exe} sha256 {sha256(exe)[:16]}", flush=True)
    summary = {"exe": exe, "exe_sha256": sha256(exe), "frames": a.frames, "replays": {}, "pass": True}

    if a.trace:
        trace = os.path.abspath(a.trace)
        reference = os.path.abspath(a.reference) if a.reference else None
    else:
        code, trace, report = record(exe, os.path.join(out, "record"), a.frames, a.record_gal, a.lock, [], 0, a.timeout)
        reference = os.path.join(out, "record", "live", "run1")
        summary["record_exit"] = code
        if code != 0:
            summary["pass"] = False
    dcode, dout = run_dump(trace, os.path.join(out, "trace.json"))
    print("dump: " + (dout.strip().splitlines()[-1] if dout.strip() else f"exit {dcode}"))
    summary["trace"] = trace
    summary["trace_valid"] = dcode == 0
    summary["pass"] = summary["pass"] and dcode == 0

    if a.determinism:
        code2, trace2, _ = record(exe, os.path.join(out, "record2"), a.frames, a.record_gal, a.lock, [], 0, a.timeout)
        d1 = json.load(open(os.path.join(out, "trace.json"), encoding="utf-8"))
        run_dump(trace2, os.path.join(out, "trace2.json"))
        d2 = json.load(open(os.path.join(out, "trace2.json"), encoding="utf-8"))
        same = d1.get("content_digest") == d2.get("content_digest")
        summary["determinism"] = {"digest1": d1.get("content_digest"), "digest2": d2.get("content_digest"), "identical": same}
        print(f"determinism: digests {d1.get('content_digest')} / {d2.get('content_digest')} -> "
              f"{'identical' if same else 'DIFFERENT'}", flush=True)
        summary["pass"] = summary["pass"] and same

    live = dict(item.split("=", 1) for item in a.live)
    for backend in a.backends.split(","):
        pdir = os.path.join(out, "play-" + backend.replace(":", "-"))
        rec = play(exe, trace, pdir, backend, a.lock, [], a.timeout)
        refs = [r for r in (reference, live.get(backend)) if r]
        exact = backend in ("d3d9", "diligent:d3d11")
        ok, result = compare(pdir, refs, os.path.join(pdir, "compare"), exact) if refs else (False, {})
        g = rec.get("galplay") or {}
        entry = {"exit": rec["exit_code"], "monitor": rec["monitor"], "completed": g.get("completed"),
                 "readbacks_identical_to_recording": g.get("readbacks_identical"), "readbacks": len(g.get("readbacks", [])),
                 "mismatches": g.get("mismatches"), "gal_errors": g.get("gal_errors"), "rule": "bytes" if exact else "parity",
                 "compare": result, "pass": ok and not rec["monitor"] and rec["exit_code"] in (0, 4)}
        summary["replays"][backend] = entry
        summary["pass"] = summary["pass"] and entry["pass"]
    with open(os.path.join(out, "gate.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    for backend, entry in summary["replays"].items():
        print(f"  {backend}: {'PASS' if entry['pass'] else 'FAIL'} ({entry['rule']}), galplay exit {entry['exit']}, "
              f"readbacks identical to the recording {entry['readbacks_identical_to_recording']}/{entry['readbacks']}")
    print("GALTRACE GATE: " + ("PASS" if summary["pass"] else "FAIL"))
    return 0 if summary["pass"] else 1


# ---------------------------------------------------------------------------------------------------
# release (M7a1)


def harness_hashes(live_dir):
    """frame -> RGB hash of the frame harness's own captures (gfx_capture.py's report.json)."""
    report = json.load(open(os.path.join(live_dir, "report.json"), encoding="utf-8"))
    return {int(c["frame"]): c["rgb_fnv1a64"] for c in report["runs"][0]["summary"]["captures"]}


def readback_hashes(play_dir):
    """frame -> RGB hash of each readback a galplay run replayed (galplay.json)."""
    g = json.load(open(os.path.join(play_dir, "galplay.json"), encoding="utf-8"))
    return {int(r["frame"]): r["replayed_rgb_fnv1a64"] for r in g.get("readbacks", [])}


def frame_hashes_text(hashes):
    """The metadata form of reference_frames.<backend>: "10:d92c3a2233b80c55,15:..."."""
    return ",".join(f"{frame}:{hashes[frame]}" for frame in sorted(hashes))


def compare_plays(a_dir, b_dir):
    """Two galplay runs of the same frames: BMPs byte for byte and the replayed hashes."""
    a, b = readback_hashes(a_dir), readback_hashes(b_dir)
    frames = sorted(set(a) | set(b))
    rows = {}
    for frame in frames:
        name = f"frame_{frame:06d}.bmp"
        pa, pb = os.path.join(a_dir, name), os.path.join(b_dir, name)
        same_bmp = os.path.exists(pa) and os.path.exists(pb) and open(pa, "rb").read() == open(pb, "rb").read()
        rows[str(frame)] = {"bmp_identical": same_bmp, "hash_a": a.get(frame), "hash_b": b.get(frame),
                            "hash_identical": a.get(frame) is not None and a.get(frame) == b.get(frame)}
    ok = bool(frames) and all(r["bmp_identical"] and r["hash_identical"] for r in rows.values())
    return ok, rows


def cmd_release(argv):
    p = argparse.ArgumentParser(prog="galtrace.py release", description="M7a1's release trace (see the module docstring)")
    p.add_argument("--exe", default=DEFAULT_EXE, help="the graphics main.exe (records and replays)")
    p.add_argument("--work", required=True, help="scratch directory: recording, plays, the extracted game files")
    p.add_argument("--recording", help="use this version 1 recording instead of recording one")
    p.add_argument("--recording-live", help="with --recording: its harness run dir (default <recording dir>/live)")
    p.add_argument("--frames", default=GATE_FRAMES)
    p.add_argument("--init", default=DEFAULT_INIT, help="the data-path script that mounts the game data (default %(default)s)")
    p.add_argument("--dest", default=DEFAULT_RELEASE)
    p.add_argument("--prove", default="d3d9,diligent:d3d11,diligent:vk,diligent:gl",
                   help="backends the version 1 and version 2 replays must be byte-identical on ('' to skip)")
    p.add_argument("--reference-backend", default=REFERENCE_BACKEND)
    p.add_argument("--refs-exe")
    p.add_argument("--lock", default=os.environ.get("GFX_GUI_LOCK", DEFAULT_LOCK))
    p.add_argument("--timeout", type=float, default=900.0)
    a = p.parse_args(argv)
    refuse_faf_vars()
    exe = os.path.abspath(a.exe)
    work = os.path.abspath(a.work)
    os.makedirs(work, exist_ok=True)
    summary = {"exe_sha256": sha256(exe), "steps": {}, "pass": True}

    def step(name, ok, **info):
        summary["steps"][name] = dict(info, ok=bool(ok))
        summary["pass"] = summary["pass"] and bool(ok)
        detail = ", ".join(f"{k} {v}" for k, v in info.items() if not isinstance(v, (dict, list)))
        print(f"release: {name}: {'OK' if ok else 'FAILED'}{' (' + detail + ')' if detail else ''}", flush=True)
        return ok

    def finish():
        with open(os.path.join(work, "release.json"), "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=2)
        print("GALTRACE RELEASE: " + ("PASS" if summary["pass"] else "FAIL"))
        return 0 if summary["pass"] else 1

    # 1. The recording (version 1, every payload embedded) and the harness's own frame hashes.
    if a.recording:
        v1 = os.path.abspath(a.recording)
        live = os.path.abspath(a.recording_live or os.path.join(os.path.dirname(v1), "live"))
    else:
        code, v1, _ = record(exe, os.path.join(work, "record"), a.frames, "d3d9", a.lock, [], 0, a.timeout)
        live = os.path.join(work, "record", "live")
        if not step("record", code == 0, exit=code):
            return finish()
    d3d9 = harness_hashes(live)
    dcode, _ = run_dump(v1, os.path.join(work, "v1.json"))
    v1info = json.load(open(os.path.join(work, "v1.json"), encoding="utf-8"))
    step("recording", dcode == 0 and v1info.get("version") == 1, version=v1info.get("version"), bytes=os.path.getsize(v1),
         digest=v1info.get("content_digest"), presents=v1info.get("presents"), d3d9_frames=frame_hashes_text(d3d9))
    summary["recording"] = {"bytes": os.path.getsize(v1), "content_digest": v1info.get("content_digest"),
                            "metadata": {k: v for k, v in v1info.get("metadata", {}).items() if k != "command_line"}}

    # 2. Version 2: game files as references, readbacks and GetTexture2D outputs as digests.
    v2 = os.path.join(work, "menu-v2.galtrace")
    meta = []
    engine_log = os.path.join(live, "run1", "engine.log")
    game = re.search(r"Game version: (\d+)", open(engine_log, encoding="utf-8", errors="replace").read()) if os.path.exists(engine_log) else None
    if game:
        meta += ["--meta", "game_version=" + game.group(1)]  # the FAF game version of the data it refers to
    summary["game_version"] = game.group(1) if game else None
    meta += ["--meta", "reference_frames.d3d9=" + frame_hashes_text(d3d9),
            "--meta", "content=FAF main menu from the start, " + str(v1info.get("presents")) + " frames at the frame harness's 30 fps "
                      "(pinned clock and seed, scratch profile), recorded on D3D9 by scripts/port/galtrace.py release"]
    code, out, err = run_refs(["convert", v1, v2, "--init", a.init, "--json", os.path.join(work, "convert.json")] + meta, a.refs_exe,
                              os.path.join(work, "convert.log"))
    conv = json.load(open(os.path.join(work, "convert.json"), encoding="utf-8")) if os.path.exists(os.path.join(work, "convert.json")) else {}
    if not step("convert", code == 0, bytes=conv.get("output_bytes"), embedded=conv.get("embedded"), references=conv.get("references"),
                digests=conv.get("digests"), compositions=conv.get("compositions"), error=conv.get("error", err[-300:])):
        return finish()
    summary["convert"] = {k: conv.get(k) for k in ("input_bytes", "output_bytes", "embedded", "references", "digests", "compositions",
                                                   "by_use", "reference_lines", "composition_lines", "unresolved", "notes")}

    # 3. No game data in it, and equivalent to the recording payload by payload.
    code, out, err = run_refs(["check", v2, "--init", a.init, "--against", v1, "--json", os.path.join(work, "check.json")], a.refs_exe,
                              os.path.join(work, "check.log"))
    chk = json.load(open(os.path.join(work, "check.json"), encoding="utf-8"))
    step("check", code == 0, valid=chk.get("valid"), references_resolved=chk.get("references_resolved"),
         equivalent=chk.get("equivalent"), whole_file_matches=chk.get("whole_file_matches"),
         game_bytes_in_matching_windows=chk.get("game_bytes_in_matching_windows"), embedded_bytes=chk.get("embedded_bytes"),
         literal_bytes=chk.get("literal_bytes"))

    # 4. The game files it refers to, as a tree for galplay's DirectoryResolver (scratch only).
    data = os.path.join(work, "data")
    code, out, err = run_refs(["extract", v2, "--init", a.init, "--out", data], a.refs_exe, os.path.join(work, "extract.log"))
    step("extract", code == 0, files=sum(1 for line in out.splitlines() if line.startswith("wrote ")))

    # 5. Replays of the recording and of version 2 into every backend: byte-identical frames.
    plays = {}
    for backend in [b for b in a.prove.split(",") if b]:
        tag = backend.replace(":", "-")
        r1 = play(exe, v1, os.path.join(work, "play-v1-" + tag), backend, a.lock, [], a.timeout)
        r2 = play(exe, v2, os.path.join(work, "play-v2-" + tag), backend, a.lock, [], a.timeout, data=data)
        same, rows = compare_plays(os.path.join(work, "play-v1-" + tag), os.path.join(work, "play-v2-" + tag))
        g2 = r2.get("galplay") or {}
        ok = same and r1["exit_code"] == 0 and r2["exit_code"] == 0 and not r1["monitor"] and not r2["monitor"]
        plays[backend] = {"v1_exit": r1["exit_code"], "v2_exit": r2["exit_code"], "frames": rows,
                          "v2_identical_to_recording": g2.get("readbacks_identical"), "v2_references_read": g2.get("references_read"),
                          "v2_provided_payloads": g2.get("provided_payloads"), "v1_seconds": r1.get("seconds"), "v2_seconds": r2.get("seconds")}
        step("prove " + backend, ok, v1_exit=r1["exit_code"], v2_exit=r2["exit_code"],
             bmps_identical=f"{sum(1 for r in rows.values() if r['bmp_identical'])}/{len(rows)}",
             hashes_identical=f"{sum(1 for r in rows.values() if r['hash_identical'])}/{len(rows)}")
    summary["plays"] = plays

    # 6. The PC's frame hashes for the reference backend, from its replay of version 2, into the header.
    ref_tag = a.reference_backend.replace(":", "-")
    ref_dir = os.path.join(work, "play-v2-" + ref_tag)
    if a.reference_backend not in plays:
        play(exe, v2, ref_dir, a.reference_backend, a.lock, [], a.timeout, data=data)
    ref = readback_hashes(ref_dir)
    frames = [int(f) for f in v1info.get("metadata", {}).get("harness_frames", a.frames).split(",") if f]
    if not step("reference " + a.reference_backend, sorted(ref) == sorted(frames), frames=frame_hashes_text(ref),
                equal_to_d3d9=f"{sum(1 for f in ref if d3d9.get(f) == ref[f])}/{len(ref)}"):
        return finish()
    dest = os.path.abspath(a.dest)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    tmp = dest + ".tmp"
    code, out, err = run_refs(["setmeta", v2, tmp, "--meta", f"reference_frames.{a.reference_backend}={frame_hashes_text(ref)}"],
                              a.refs_exe, os.path.join(work, "setmeta.log"))
    if not step("setmeta", code == 0, error=err[-300:]):
        return finish()
    os.replace(tmp, dest)

    # 7. The written file: valid with every reference read, no game data, and its own replay passes on
    #    the reference backend against the hashes it carries.
    dcode, dout = run_dump(dest, os.path.join(work, "final.json"), data=data)
    fin = json.load(open(os.path.join(work, "final.json"), encoding="utf-8"))
    step("final dump", dcode == 0, verdict=fin.get("verdict"), digest=fin.get("content_digest"),
         references_resolved=fin.get("payloads", {}).get("references_resolved"))
    code, out, err = run_refs(["check", dest, "--init", a.init, "--against", v1, "--json", os.path.join(work, "final-check.json")],
                              a.refs_exe, os.path.join(work, "final-check.log"))
    fchk = json.load(open(os.path.join(work, "final-check.json"), encoding="utf-8"))
    step("final check", code == 0, whole_file_matches=fchk.get("whole_file_matches"),
         game_bytes_in_matching_windows=fchk.get("game_bytes_in_matching_windows"), equivalent=fchk.get("equivalent"))
    final_play = play(exe, dest, os.path.join(work, "play-final-" + ref_tag), a.reference_backend, a.lock, [], a.timeout, data=data)
    g = final_play.get("galplay") or {}
    step("final replay " + a.reference_backend, final_play["exit_code"] == 0 and g.get("readbacks_pass") == len(frames),
         exit=final_play["exit_code"], verdicts=f"{g.get('readbacks_pass')}/{g.get('readbacks_with_reference')} pass")

    # 8. Provenance beside the trace: what it is and how it was checked; no paths of this machine.
    refs = []
    for line in summary["convert"].get("reference_lines") or []:
        refs.append(line)
    provenance = {
        "file": os.path.basename(dest), "bytes": os.path.getsize(dest), "sha256": sha256(dest), "format_version": fin.get("version"),
        "content_digest": fin.get("content_digest"), "metadata": fin.get("metadata"), "payloads": fin.get("payloads"),
        "recording": summary["recording"], "made_by": "scripts/port/galtrace.py release (galtrace-refs convert/check, galplay)",
        "made_at": datetime.datetime.now().astimezone().isoformat(timespec="seconds"), "exe_sha256": summary["exe_sha256"],
        "references": refs, "compositions": summary["convert"].get("composition_lines"),
        "embedded_by_use": summary["convert"].get("by_use"),
        "game_content_check": {k: fchk.get(k) for k in ("valid", "references", "references_resolved", "embedded_payloads", "embedded_bytes",
                                                         "literal_bytes", "game_windows_indexed", "whole_file_matches",
                                                         "game_bytes_in_matching_windows", "equivalent", "records_compared",
                                                         "payloads_compared", "records_differing_only_in_paths")},
        "replays": {b: {k: v for k, v in e.items() if k != "frames"} | {"frames_identical": sum(1 for r in e["frames"].values() if r["bmp_identical"])}
                    for b, e in plays.items()},
        "reference_frames": {"d3d9": frame_hashes_text(d3d9), a.reference_backend: frame_hashes_text(ref)},
        "final_replay": {"backend": a.reference_backend, "exit": final_play["exit_code"], "pass": g.get("readbacks_pass"),
                         "with_reference": g.get("readbacks_with_reference")},
        "pass": summary["pass"],
    }
    with open(dest + ".json", "w", encoding="utf-8") as f:
        json.dump(provenance, f, indent=2)
    summary["dest"] = dest
    print(f"release: {dest} ({os.path.getsize(dest)} bytes, sha256 {provenance['sha256'][:16]}), provenance {dest}.json", flush=True)
    return finish()


def main():
    commands = {"record": cmd_record, "play": cmd_play, "compare": cmd_compare, "dump": cmd_dump, "tools": cmd_tools,
                "gate": cmd_gate, "refs": cmd_refs, "release": cmd_release}
    if len(sys.argv) < 2 or sys.argv[1] not in commands:
        print(__doc__)
        return 2
    return commands[sys.argv[1]](sys.argv[2:])


if __name__ == "__main__":
    sys.exit(main())
