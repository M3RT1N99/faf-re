#!/usr/bin/env python3
"""Gate 1 of M6a (docs/port/renderer.md): the Diligent integration spike.

Runs the FAF_PORT_GRAPHICS main.exe with `/gal diligent:d3d11` under the frame harness (step 0,
scripts/port/gfx_capture.py: hidden window, input dropped, pinned prefs and init, one GUI run at a
time through the lock file, below-normal priority) and checks:

  1. the engine reaches the main menu and runs 900 frames (harness.json: main_menu_module_loaded,
     app_frames; the backend's report: presents, EndScene calls);
  2. the D3D11 debug layer is active and live (an error provoked on purpose at setup, and not
     counted, is reported) and reports 0 errors and 0 corruption over those frames, and Diligent
     itself logs no error (a removed device would);
  3. every effect the engine loaded reports, through the Diligent backend's GetTechniques, the same
     technique list (FindNextValidTechnique order) that a D3D9 HAL device gives for the same source
     bytes (tools/fxtechlist.cpp, compiled here; it repeats DeviceD3D9::CreateEffectFromSourceBuffer
     and EffectD3D9::GetTechniques). The backend's rule replayed outside the engine (fxtechlist
     oracle: NULLREF device plus HAL caps) is reported too.

The effect sources are the buffers the engine passed to gal CreateEffect, written by the backend
(`/galdumpfx`); they are game data and stay in the output directory (scratch or buildstage).

usage:
  python port/graphics/diligent/tools/gate1.py --exe output/main-gfx/Win32/Debug/main.exe \
      --lock <scratch>/m6a/gui.lock --out <scratch>/m6a/D/gate1
  python port/graphics/diligent/tools/gate1.py --out <dir> --skip-run     # re-check an earlier run

Exit code 0 when all three checks pass.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", ".."))
HERE = os.path.dirname(os.path.abspath(__file__))
VCVARS32 = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
DXSDK = os.path.join(REPO, "dependencies", "DXSDK_D3DX")
FRAMES = 900


def build_fxtechlist(out_dir):
    exe = os.path.join(out_dir, "fxtechlist.exe")
    source = os.path.join(HERE, "fxtechlist.cpp")
    if os.path.isfile(exe) and os.path.getmtime(exe) >= os.path.getmtime(source):
        return exe
    os.makedirs(out_dir, exist_ok=True)
    # The x86 developer environment, read once from vcvars32.bat (a fixed path, no user input).
    # A string, not a list: list2cmdline would escape the quotes that cmd /s /c needs.
    dump = subprocess.run(f'cmd /s /c ""{VCVARS32}" >nul && set"', capture_output=True, text=True)
    env = dict(line.split("=", 1) for line in dump.stdout.splitlines() if "=" in line)
    # DXSDK include after the Windows SDK ones, as main.vcxproj orders them ($(IncludePath);$(DirectXInclude)).
    env["INCLUDE"] = env.get("INCLUDE", "") + ";" + os.path.join(DXSDK, "include")
    cl = shutil.which("cl", path=env.get("PATH") or env.get("Path"))
    if cl is None:
        raise SystemExit("cl.exe not found through vcvars32.bat")
    command = [cl, "/nologo", "/EHsc", "/std:c++17", "/W3", source, f"/Fe{exe}", f"/Fo{out_dir}\\",
               "/link", f"/LIBPATH:{os.path.join(DXSDK, 'lib', 'x86')}", "d3d9.lib", "d3dx9.lib", "user32.lib"]
    result = subprocess.run(command, capture_output=True, text=True, env=env, cwd=out_dir)
    if result.returncode != 0 or not os.path.isfile(exe):
        print(result.stdout, result.stderr)
        raise SystemExit("cannot build fxtechlist")
    return exe


def technique_list(tool, device, src, macros):
    args = [tool, device, src] + [f"{name}={value}" for name, value in macros]
    result = subprocess.run(args, capture_output=True, text=True, creationflags=0x00004000)  # BELOW_NORMAL
    try:
        return json.loads(result.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        return {"error": f"fxtechlist exit {result.returncode}: {result.stdout} {result.stderr}"}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=os.path.join(REPO, "output", "main-gfx", "Win32", "Debug", "main.exe"))
    parser.add_argument("--out", required=True, help="output directory (scratch or buildstage)")
    parser.add_argument("--lock", default=None, help="GUI lock file, passed to gfx_capture.py")
    parser.add_argument("--api", default="d3d11")
    parser.add_argument("--skip-run", action="store_true", help="only evaluate an earlier run in --out")
    parser.add_argument("--timeout", type=float, default=900.0)
    args = parser.parse_args()

    out = os.path.abspath(args.out)
    fx_dir = os.path.join(out, "fx")
    report_path = os.path.join(out, "diligent_report.json")
    capture_dir = os.path.join(out, "capture")
    if not args.skip_run:
        os.makedirs(fx_dir, exist_ok=True)
        # The backend writes its report after present FRAMES + 1 (which shows frame FRAMES: CD3DDevice::Paint
        # presents first and renders after, CD3DDevice.cpp:1897-1927); the harness ends the process at the
        # frame after it.
        command = [sys.executable, os.path.join(REPO, "scripts", "port", "gfx_capture.py"),
                   "--exe", os.path.abspath(args.exe), "--gal", f"diligent:{args.api}", "--frames", "none",
                   "--exit-frame", str(FRAMES + 2), "--runs", "1", "--out", capture_dir,
                   "--timeout", str(args.timeout)]
        if args.lock:
            command += ["--lock", args.lock]
        command += ["--", "/galreport", report_path, "/galreportframe", str(FRAMES + 1), "/galdumpfx", fx_dir,
                    "/galdebuglayerselftest"]
        print("> " + subprocess.list2cmdline(command), flush=True)
        run = subprocess.run(command)
        print(f"gfx_capture.py exit {run.returncode}", flush=True)

    checks = {}
    harness_path = os.path.join(capture_dir, "run1", "harness.json")
    harness = json.load(open(harness_path, encoding="utf-8")) if os.path.isfile(harness_path) else {}
    report = json.load(open(report_path, encoding="utf-8")) if os.path.isfile(report_path) else {}
    slots = {s["name"]: s["calls"] for s in report.get("slots", [])}

    menu = bool(harness.get("main_menu_module_loaded"))
    frames = harness.get("app_frames", 0)
    checks["1_main_menu_and_frames"] = {
        "pass": menu and frames >= FRAMES and report.get("presents", 0) >= FRAMES + 1 and slots.get("EndScene", 0) >= FRAMES,
        "harness_status": harness.get("status"), "harness_device_api": harness.get("device", {}).get("api_code"),
        "main_menu_module_loaded": menu, "app_frames": frames, "paints": harness.get("paints"),
        "presents": report.get("presents"), "end_scenes": slots.get("EndScene"),
        "head_clears": report.get("headClears"),
    }

    layer = report.get("debugLayer", {})
    self_test = layer.get("selfTest", {})
    checks["2_d3d11_debug_layer"] = {
        # Live: the error provoked on purpose at setup (not counted) was reported by the layer.
        # Diligent's own errors stay at 0 too: a removed device shows there (one per frame).
        "pass": bool(layer.get("active")) and self_test.get("errorsProvoked", 0) >= 1 and layer.get("error", 1) == 0
                and layer.get("corruption", 1) == 0 and report.get("diligentMessages", {}).get("error", 1) == 0
                and report.get("diligentMessages", {}).get("fatal", 1) == 0,
        "self_test": self_test,
        "active": layer.get("active"), "corruption": layer.get("corruption"), "error": layer.get("error"),
        "warning": layer.get("warning"), "info": layer.get("info"), "first": layer.get("first"),
        "diligent_messages": report.get("diligentMessages"),
    }

    tool = build_fxtechlist(os.path.join(out, "tools"))
    effects = []
    all_equal = True
    dumped = sorted(glob.glob(os.path.join(fx_dir, "*.src")))
    by_path = {e["path"]: e for e in report.get("effects", [])}
    for src in dumped:
        meta = json.load(open(src[:-4] + ".json", encoding="utf-8"))
        engine = by_path.get(meta["sourcePath"], {})
        hal = technique_list(tool, "hal", src, meta.get("macros", []))
        oracle = technique_list(tool, "oracle", src, meta.get("macros", []))
        equal = "error" not in hal and engine.get("validTechniques") == hal.get("validTechniques")
        all_equal &= equal
        effects.append({"path": meta["sourcePath"], "bytes": meta.get("bytes"), "equal_to_d3d9_hal": equal,
                        "diligent": len(engine.get("validTechniques", [])), "d3d9_hal": len(hal.get("validTechniques", [])),
                        "oracle_tool": len(oracle.get("validTechniques", [])), "oracle_tool_equal": oracle.get("validTechniques") == engine.get("validTechniques"), "declared": hal.get("techniques"),
                        "hal_rejects": sorted(set(hal.get("allTechniques", [])) - set(hal.get("validTechniques", []))),
                        "hal_error": hal.get("error"), "diligent_list": engine.get("validTechniques"),
                        "hal_list": hal.get("validTechniques")})
    checks["3_technique_lists"] = {"pass": all_equal and len(effects) >= 11 and len(effects) == len(by_path),
                                   "effects": len(effects), "engine_effects": len(by_path), "per_effect": effects}

    verdict = all(c["pass"] for c in checks.values())
    result = {"exe": os.path.abspath(args.exe), "pass": verdict, "checks": checks}
    with open(os.path.join(out, "gate1.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=1)
    c1, c2, c3 = checks["1_main_menu_and_frames"], checks["2_d3d11_debug_layer"], checks["3_technique_lists"]
    print(f"1 main menu {c1['main_menu_module_loaded']}, harness frames {c1['app_frames']}, presents {c1['presents']}, "
          f"EndScene {c1['end_scenes']}, device api {c1['harness_device_api']} -> {'PASS' if c1['pass'] else 'FAIL'}")
    print(f"2 D3D11 debug layer active {c2['active']}, self test provoked {c2['self_test'].get('errorsProvoked')} "
          f"({c2['self_test'].get('sample', '')[:90]}): corruption {c2['corruption']}, errors {c2['error']}, "
          f"warnings {c2['warning']}, info {c2['info']}; Diligent errors {(c2['diligent_messages'] or {}).get('error')} "
          f"-> {'PASS' if c2['pass'] else 'FAIL'}")
    for e in effects:
        print(f"   {e['path']}: declared {e['declared']}, Diligent {e['diligent']}, D3D9 HAL {e['d3d9_hal']}, "
              f"oracle tool {e['oracle_tool']}, equal {e['equal_to_d3d9_hal']}" + (f", HAL rejects {e['hal_rejects']}" if e['hal_rejects'] else "")
              + (f", HAL error {e['hal_error']}" if e['hal_error'] else ""))
    print(f"3 technique lists: {c3['effects']} effects, all equal {all_equal} -> {'PASS' if c3['pass'] else 'FAIL'}")
    print("GATE 1: " + ("PASS" if verdict else "FAIL"))
    return 0 if verdict else 1


if __name__ == "__main__":
    sys.exit(main())
