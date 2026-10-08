#!/usr/bin/env python3
"""Build galtrace's reader and decoder for Android (x86_64 and arm64) with the NDK, and run it.

The trace format (port/graphics/trace/format) has no engine, Windows or graphics dependency, so the
same sources that the Win32 graphics main.exe records and replays with build for the phone's ABI.
This proves the M6c rule that a trace decodes on the pointer width and ABI it will be played on
(galplay on the phone is the plan's step 8, M7a1). Commands in the style of
scripts/port/build_runner.py: the NDK's clang per translation unit, --target=<triple>, a static
executable per tool.

  galtrace-dump          tools/galtrace_dump.cpp: validate and decode a trace, print its digest
  galtrace_format_test   tests/galtrace_format_test.cpp: the format unit test

each twice: <tool>, a dynamic PIE against bionic (how Android runs executables; the emulator's ARM
translation runs only these), and <tool>-static, for a plain Linux kernel of the ABI (WSL).

usage:
  python port/graphics/trace/android/build_reader.py [--abi x86_64,arm64-v8a] [--out DIR] [--ndk DIR]
         [--run-wsl TRACE ...]      run the static x86_64 tools under WSL (docker-desktop) on these traces
                                    and the unit test
         [--run-adb TRACE ...] [--adb PATH]
                                    push the dynamic tools of every ABI and the traces to a device or
                                    emulator (/data/local/tmp/galtrace-reader, removed afterwards), run there

Output (default buildstage/galtrace-android/<abi>): the objects, the two executables, build.json.
Exit code 0 when every ABI built (and every run passed), 1 otherwise.
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TRACE = os.path.abspath(os.path.join(HERE, ".."))
REPO = os.path.abspath(os.path.join(TRACE, "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "scripts", "port"))
import engine_sweep  # noqa: E402  (find_clang: the NDK the Android builds use)

API = 26  # minSdk (port/android/AndroidManifest.xml)
TRIPLES = {"x86_64": f"x86_64-linux-android{API}", "arm64-v8a": f"aarch64-linux-android{API}"}
LIBRARY = ["format/GalTraceFormat.cpp", "format/GalTraceIO.cpp"]
TOOLS = {"galtrace-dump": "tools/galtrace_dump.cpp", "galtrace_format_test": "tests/galtrace_format_test.cpp"}
FLAGS = ["-std=c++17", "-O2", "-g", "-Wall", "-Wextra", "-Wshadow", "-Werror", "-fno-exceptions"]


def fwd(path):
    return path.replace("\\", "/")


def compile_unit(clang, triple, src, obj):
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    cmd = [fwd(clang), f"--target={triple}"] + FLAGS + ["-I", fwd(os.path.join(TRACE, "format")), "-c", fwd(src), "-o", fwd(obj)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0, cmd, (r.stdout + r.stderr).strip()


def build_abi(clang, abi, out_dir):
    triple = TRIPLES[abi]
    result = {"abi": abi, "triple": triple, "clang": clang, "units": [], "tools": {}, "ok": True}
    objects = []
    for rel in LIBRARY + list(TOOLS.values()):
        src = os.path.join(TRACE, rel)
        obj = os.path.join(out_dir, "obj", rel.replace("/", "_") + ".o")
        ok, cmd, log = compile_unit(clang, triple, src, obj)
        result["units"].append({"source": rel, "ok": ok, "log": log[-2000:]})
        result["ok"] = result["ok"] and ok
        objects.append((rel, obj))
        if not ok:
            print(f"{abi}: compile failed: {rel}\n{log[-2000:]}")
    if not result["ok"]:
        return result
    library_objects = [obj for rel, obj in objects if rel in LIBRARY]
    readelf = fwd(os.path.join(os.path.dirname(clang), "llvm-readelf.exe" if os.name == "nt" else "llvm-readelf"))
    for tool, rel in TOOLS.items():
        tool_obj = dict(objects)[rel]
        # <tool>: a dynamic PIE against bionic, as Android runs executables (and the only kind the
        # emulator's ARM translation runs: a static arm64 executable crashes there with SIGSEGV).
        # <tool>-static: static, for a plain Linux kernel of the ABI (WSL) without bionic.
        for variant, link in (("", ["-static-libstdc++"]), ("-static", ["-static", "-static-libstdc++"])):
            exe = os.path.join(out_dir, tool + variant)
            cmd = [fwd(clang), f"--target={triple}"] + link + ["-o", fwd(exe), fwd(tool_obj)] + [fwd(o) for o in library_objects]
            r = subprocess.run(cmd, capture_output=True, text=True)
            ok = r.returncode == 0
            info = {"ok": ok, "path": exe, "command": " ".join(cmd), "log": (r.stdout + r.stderr).strip()[-2000:]}
            if ok:
                f = subprocess.run([readelf, "-h", fwd(exe)], capture_output=True, text=True)
                info["elf"] = " ".join(" ".join(l.split()) for l in f.stdout.splitlines() if "Class:" in l or "Machine:" in l or "Type:" in l)
            result["tools"][tool + variant] = info
            result["ok"] = result["ok"] and ok
            print(f"{abi}: {tool + variant} {'built' if ok else 'LINK FAILED'}: {info.get('elf', info['log'][-300:])}")
    return result


def wsl_path(path):
    """A Windows path as the docker-desktop WSL distro sees it (drives under /mnt/host/<letter>)."""
    path = os.path.abspath(path)
    return "/mnt/host/" + path[0].lower() + fwd(path[2:])


def run_wsl(out_dir, traces):
    runs = []
    dump = wsl_path(os.path.join(out_dir, "galtrace-dump-static"))
    test = wsl_path(os.path.join(out_dir, "galtrace_format_test-static"))
    base = ["wsl", "-d", "docker-desktop", "--"]
    # The unit test writes a few kB of scratch files under /tmp.
    r = subprocess.run(base + ["sh", "-c", f"mkdir -p /tmp/galtrace && {test} /tmp/galtrace"], capture_output=True, text=True)
    runs.append({"what": "galtrace_format_test", "where": "wsl", "abi": "x86_64", "exit": r.returncode,
                 "out": r.stdout.strip()[-1500:] + r.stderr.strip()[-500:]})
    print(f"wsl x86_64 format test: exit {r.returncode}: {r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()}")
    for trace in traces:
        r = subprocess.run(base + [dump, wsl_path(trace), "--quiet"], capture_output=True, text=True)
        line = r.stdout.strip() or r.stderr.strip()
        runs.append({"what": "galtrace-dump", "where": "wsl", "abi": "x86_64", "trace": trace, "exit": r.returncode, "out": line[-1500:]})
        print(f"wsl x86_64 galtrace-dump {trace}: exit {r.returncode}: {line}")
    return runs


def run_adb(adb, out_dir, abi, traces):
    """Push the dynamic tools and the traces to /data/local/tmp/galtrace-reader, run, remove again."""
    runs = []
    remote = "/data/local/tmp/galtrace-reader"

    def shell(command):
        # The exit status is echoed: older adb shells do not return it.
        r = subprocess.run([adb, "shell", command + "; echo EXIT=$?"], capture_output=True, text=True)
        out = r.stdout.strip()
        code = int(out.rsplit("EXIT=", 1)[1]) if "EXIT=" in out else -1
        return code, out.rsplit("EXIT=", 1)[0].strip()

    subprocess.run([adb, "shell", "mkdir", "-p", remote], capture_output=True)
    for tool in TOOLS:
        subprocess.run([adb, "push", os.path.join(out_dir, tool), f"{remote}/{tool}"], capture_output=True)
        subprocess.run([adb, "shell", "chmod", "755", f"{remote}/{tool}"], capture_output=True)
    code, out = shell(f"{remote}/galtrace_format_test {remote}")
    runs.append({"what": "galtrace_format_test", "where": "adb", "abi": abi, "exit": code, "out": out[-1500:]})
    print(f"adb {abi} format test: exit {code}: {out.splitlines()[-1] if out else ''}")
    for trace in traces:
        name = os.path.basename(trace)
        subprocess.run([adb, "push", trace, f"{remote}/{name}"], capture_output=True)
        code, out = shell(f"{remote}/galtrace-dump {remote}/{name} --quiet")
        runs.append({"what": "galtrace-dump", "where": "adb", "abi": abi, "trace": trace, "exit": code, "out": out[-1500:]})
        print(f"adb {abi} galtrace-dump {trace}: exit {code}: {out}")
    subprocess.run([adb, "shell", "rm", "-rf", remote], capture_output=True)
    return runs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--abi", default="x86_64,arm64-v8a")
    ap.add_argument("--out", default=os.path.join(REPO, "buildstage", "galtrace-android"))
    ap.add_argument("--ndk")
    ap.add_argument("--clang")
    ap.add_argument("--run-wsl", nargs="*", default=None, metavar="TRACE")
    ap.add_argument("--run-adb", nargs="*", default=None, metavar="TRACE")
    ap.add_argument("--adb", default="adb", help="adb executable (default: adb on PATH)")
    args = ap.parse_args()
    clang, ndk = engine_sweep.find_clang(args.ndk, args.clang)
    report = {"ndk": ndk, "clang": clang, "abis": {}, "runs": []}
    ok = True
    for abi in args.abi.split(","):
        out_dir = os.path.join(os.path.abspath(args.out), abi)
        result = build_abi(clang, abi, out_dir)
        report["abis"][abi] = result
        ok = ok and result["ok"]
        if result["ok"] and abi == "x86_64" and args.run_wsl is not None:
            runs = run_wsl(out_dir, [os.path.abspath(t) for t in args.run_wsl])
            report["runs"] += runs
            ok = ok and all(r["exit"] == 0 for r in runs)
        if result["ok"] and args.run_adb is not None:
            runs = run_adb(args.adb, out_dir, abi, [os.path.abspath(t) for t in args.run_adb])
            report["runs"] += runs
            ok = ok and all(r["exit"] == 0 for r in runs)
    os.makedirs(os.path.abspath(args.out), exist_ok=True)
    with open(os.path.join(os.path.abspath(args.out), "build.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    print("BUILD_READER: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
