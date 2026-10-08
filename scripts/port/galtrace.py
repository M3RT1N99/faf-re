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
  dump      galtrace-dump on a trace (validation, op counts, content digest)
  tools     build galtrace-dump and the format unit test (CMake, Win32 and x64)
  gate      the step-5 gate: record a trace, replay it into every backend asked for, compare each
            replay with its references, record a second trace and compare the content digests

usage:
  python scripts/port/galtrace.py record --exe EXE --out DIR [--frames 10,...,900] [--gal API] [--lock L]
  python scripts/port/galtrace.py play --exe EXE --trace T --out DIR [--gal diligent:vk] [--lock L] [-- options]
  python scripts/port/galtrace.py compare --play DIR --ref DIR [--ref DIR ...] [--exact] --out DIR
  python scripts/port/galtrace.py dump --trace T [--json F] [--records N]
  python scripts/port/galtrace.py tools [--build DIR]
  python scripts/port/galtrace.py gate --exe EXE --out DIR --backends d3d9,diligent:d3d11,diligent:vk,diligent:gl [--live DIR ...]

Every GUI run names the machine's shared lock (--lock or GFX_GUI_LOCK). FAF_* variables refuse the
run, as in gfx_capture.py (the committed render-path probes arm themselves from them).
"""

import argparse
import datetime
import glob
import hashlib
import json
import os
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
}


def refuse_faf_vars():
    names = sorted(k for k in os.environ if k.upper().startswith("FAF_"))
    if names:
        raise SystemExit(f"refusing to run: FAF_* environment variables are set ({', '.join(names)})")


def sha256(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


# ---------------------------------------------------------------------------------------------------
# tools


def dump_exe(build_dir=None, arch="x64"):
    base = build_dir or os.path.join(TOOLS_DIR, arch)
    for config in ("Release", "Debug"):
        path = os.path.join(base, config, "galtrace-dump.exe")
        if os.path.isfile(path):
            return path
    return None


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
        print(f"{arch}: built in {out}; format test: {t.stdout.strip().splitlines()[-1] if t.stdout else t.returncode}")
        ok = ok and t.returncode == 0
    return 0 if ok else 1


def run_dump(trace, json_path=None, records=None, exe=None):
    exe = exe or dump_exe() or dump_exe(arch="win32")
    if exe is None:
        raise SystemExit("galtrace-dump not built: python scripts/port/galtrace.py tools")
    cmd = [exe, trace]
    if json_path:
        cmd += ["--json", json_path]
    if records is not None:
        cmd += ["--records", str(records)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout


def cmd_dump(argv):
    p = argparse.ArgumentParser(prog="galtrace.py dump")
    p.add_argument("--trace", required=True)
    p.add_argument("--json")
    p.add_argument("--records", type=int)
    p.add_argument("--dump-exe")
    a = p.parse_args(argv)
    code, out = run_dump(a.trace, a.json, a.records, a.dump_exe)
    print(out, end="")
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


def play(exe, trace, out, gal, lock, extra, timeout=900.0, lock_wait=3600.0):
    """main.exe /galplay, hidden and watched like a harness run; returns the run record."""
    os.makedirs(out, exist_ok=True)
    cmd = [exe, "/galplay", os.path.abspath(trace), "/galplayout", os.path.abspath(out)]
    if gal and gal != "d3d9":
        cmd += ["/gal", gal]
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
    print(f"play {gal or 'd3d9'}: exit {proc.returncode} ({rec['exit_meaning']}), {rec.get('seconds')} s, "
          f"{g.get('records')} records, {g.get('presents')} presents, {g.get('draws')} draws, readbacks "
          f"{g.get('readbacks_identical')}/{len(g.get('readbacks', []))} identical to the recording, "
          f"gal errors {g.get('gal_errors')}, mismatches {mism or 'none'}, monitor {rec['monitor'] or 'clean'}", flush=True)
    return rec


def cmd_play(argv):
    p = argparse.ArgumentParser(prog="galtrace.py play")
    p.add_argument("--exe", default=DEFAULT_EXE)
    p.add_argument("--trace", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--gal", default="d3d9")
    p.add_argument("--lock", default=os.environ.get("GFX_GUI_LOCK", DEFAULT_LOCK))
    p.add_argument("--timeout", type=float, default=900.0)
    p.add_argument("extra", nargs="*")
    a = p.parse_args(argv)
    refuse_faf_vars()
    rec = play(os.path.abspath(a.exe), a.trace, os.path.abspath(a.out), a.gal, a.lock, a.extra, a.timeout)
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


def main():
    commands = {"record": cmd_record, "play": cmd_play, "compare": cmd_compare, "dump": cmd_dump, "tools": cmd_tools,
                "gate": cmd_gate}
    if len(sys.argv) < 2 or sys.argv[1] not in commands:
        print(__doc__)
        return 2
    return commands[sys.argv[1]](sys.argv[2:])


if __name__ == "__main__":
    sys.exit(main())
