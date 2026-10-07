"""fxdiff: the portable effect front end's SM5 shaders against the game's D3D9 shaders (M6b).

    python scripts/port/fxdiff.py --lock <scratch>/m6a/gui.lock [--work DIR] [--build DIR] [--no-build]
                                  [--effects primbatcher ui frame] [--archive effects_nx2]
                                  [--scfa DIR] [--faf DIR] [--ndk DIR] [--size 64] [--seed 1]
                                  [--tolerance 0.00392156862745098] [--no-render]

For the menu effects (primbatcher, ui and frame of FAF's effects.nx2 by default), every stage that
FxHlslEmitter generates (port/graphics/fx/src/FxHlslEmitter.cpp) has to:

1. compile with FXC as vs_5_0/ps_5_0 (tools/fxhlsl --fxc, d3dcompiler_47, the compiler Diligent's
   D3D11 backend uses);
2. compile through Diligent's own HLSL -> SPIR-V path for Vulkan (GLSLangUtils::HLSLtoSPIRV with
   ShaderVkImpl.cpp's VULKAN define) and pass spirv-val --target-env vulkan1.0 (the NDK's);
3. render what the game's shader renders: tools/fxdiff draws every pass of every valid technique
   with D3D9 (D3DX and the legacy d3dx9_31 compiler, as DeviceD3D9::CreateEffectFromSourceBuffer)
   and with the Diligent backend's effect layer on D3D11 (EffectGpu, PassBinding::GetProgram and
   Commit), same parameters, textures and vertices, and compares the targets: a float target
   without alpha test (shader arithmetic, |delta| <= --tolerance) and an 8-bit target with the
   pass's alpha test (coverage identical, |delta| <= 1 LSB). A pass render passes when no pixel is
   drawn by one side only and at most 0.1 % of its drawn pixels exceed the tolerance.

Effects are extracted from the archives with fx_metadata_gate.py's corpus code into --work
(default buildstage/fxdiff-work; never the repository). The D3D9 side needs a HAL device, so
the render step takes the machine-wide GUI lock (--lock or $GFX_GUI_LOCK, the lock
gfx_capture.py uses), runs at below-normal priority, and is killed if one of its windows becomes
visible (its D3D9 focus window is created hidden at -20000,-20000 and never shown).

Writes WORK/fxdiff-gate.json and, for passes that differ, heat maps next to the raw dumps.
Exit code 0 when every check passes, 1 if not, 2 on setup errors.
"""
import argparse
import datetime
import glob
import json
import os
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fx_metadata_gate as corpus  # noqa: E402  (collect_corpus, DEFAULT_SCFA, DEFAULT_FAF)
import gfx_capture  # noqa: E402  (acquire_lock, release_lock, windows_of)

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BELOW_NORMAL_PRIORITY_CLASS = 0x00004000
DEFAULT_NDK = r"C:\Android\sdk\ndk\29.0.14206865"

# Parameters the generic values would make useless: frame.fx StrategicVS projects
# (x, mapElevation, z) through WorldToView * Projection, which near-identity matrices flatten to a
# line; this WorldToView swaps y and z so the grid covers the target.
EFFECT_OVERRIDES = {
    "frame": ["WorldToView=1,0,0,0,0,0,1,0,0,1,0,0,0,0,0,1", "mapElevation=0.5"],
}


def run(command, **kwargs):
    print("$ " + " ".join(command), flush=True)
    return subprocess.run(command, **kwargs)


def build(build_dir):
    fx_build = os.path.join(build_dir, "fx")
    diff_build = os.path.join(build_dir, "fxdiff")
    steps = [
        ["cmake", "-S", os.path.join(REPO, "port", "graphics", "fx"), "-B", fx_build, "-A", "Win32"],
        ["cmake", "--build", fx_build, "--config", "Release", "--target", "fxhlsl"],
        ["cmake", "-S", os.path.join(REPO, "port", "graphics", "fx", "tools", "fxdiff"), "-B", diff_build, "-A", "Win32"],
        ["cmake", "--build", diff_build, "--config", "Debug"],
    ]
    for step in steps:
        result = run(step, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace",
                     creationflags=BELOW_NORMAL_PRIORITY_CLASS)
        if result.returncode != 0:
            print(result.stdout[-4000:])
            corpus.fail("build step failed: " + " ".join(step))


def tools(build_dir):
    fxhlsl = os.path.join(build_dir, "fx", "Release", "fxhlsl.exe")
    fxdiff = os.path.join(build_dir, "fxdiff", "Debug", "fxdiff.exe")
    for path in (fxhlsl, fxdiff):
        if not os.path.isfile(path):
            corpus.fail(path + " not built")
    return fxhlsl, fxdiff


def run_watched(command, timeout):
    """Runs a GPU tool at below-normal priority; kills it if one of its windows is visible."""
    proc = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace",
                            creationflags=BELOW_NORMAL_PRIORITY_CLASS)
    violations = []
    done = threading.Event()

    def watch():
        while not done.is_set():
            for hwnd, visible, cls in gfx_capture.windows_of(proc.pid):
                if visible:
                    violations.append(f"visible window {hwnd:#x} ({cls})")
                    proc.kill()
                    return
            time.sleep(0.05)

    watcher = threading.Thread(target=watch, daemon=True)
    watcher.start()
    try:
        output, _ = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        output, _ = proc.communicate()
        violations.append(f"timeout after {timeout} s")
    done.set()
    watcher.join()
    return proc.returncode, output, violations


def heatmap(stem, size):
    """Heat map of |d3d9 - diligent| (max over RGBA) for a pass that differs; returns the PNG path."""
    try:
        from PIL import Image
    except ImportError:
        return None
    try:
        a = open(stem + ".d3d9.f32", "rb").read()
        b = open(stem + ".dg.f32", "rb").read()
    except OSError:
        return None
    count = size * size * 4
    fa = struct.unpack(f"<{count}f", a[:count * 4])
    fb = struct.unpack(f"<{count}f", b[:count * 4])
    image = Image.new("RGB", (size * 3, size))
    for p in range(size * size):
        x, y = p % size, p // size
        pa = fa[p * 4:p * 4 + 4]
        pb = fb[p * 4:p * 4 + 4]
        delta = max(abs(u - v) for u, v in zip(pa, pb))
        heat = min(255, int(delta * 255 * 8))
        image.putpixel((x, y), tuple(max(0, min(255, int(c * 255))) for c in pa[:3]))
        image.putpixel((size + x, y), tuple(max(0, min(255, int(c * 255))) for c in pb[:3]))
        image.putpixel((2 * size + x, y), (heat, 0 if heat else 32, 0))
    path = stem + ".heat.png"
    image.resize((size * 6, size * 2), Image.NEAREST).save(path)
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--work", default=os.path.join(REPO, "buildstage", "fxdiff-work"))
    parser.add_argument("--build", default=os.path.join(REPO, "buildstage", "fxdiff"))
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--effects", nargs="+", default=["primbatcher", "ui", "frame"])
    parser.add_argument("--archive", default="effects_nx2", help="archive tag (effects_nx2 = FAF 3839)")
    parser.add_argument("--scfa", default=corpus.DEFAULT_SCFA)
    parser.add_argument("--faf", default=corpus.DEFAULT_FAF)
    parser.add_argument("--ndk", default=DEFAULT_NDK)
    parser.add_argument("--size", type=int, default=64)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--tolerance", type=float, default=1.0 / 255.0,
                        help="float target: max |delta| per channel (default one 8-bit step, 1/255)")
    parser.add_argument("--no-render", action="store_true", help="only FXC and SPIR-V (no GPU, no lock)")
    parser.add_argument("--lock", default=os.environ.get("GFX_GUI_LOCK", gfx_capture.DEFAULT_LOCK))
    parser.add_argument("--lock-wait", type=float, default=3600.0)
    parser.add_argument("--timeout", type=float, default=600.0)
    args = parser.parse_args()
    sys.stdout.reconfigure(errors="replace")

    work = os.path.abspath(args.work)
    if os.path.commonpath([work, REPO]) == REPO and not work.startswith(os.path.join(REPO, "buildstage")):
        corpus.fail("--work inside the repository must be under buildstage/")
    os.makedirs(work, exist_ok=True)
    if not args.no_build:
        build(args.build)
    fxhlsl, fxdiff = tools(args.build)

    variants, _ = corpus.collect_corpus(args.scfa, args.faf, work)
    chosen = []
    for name in args.effects:
        tag = args.archive + "/" + name
        match = [v for v in variants if v["tag"] == tag or tag in v["duplicates"]]
        if not match:
            corpus.fail(f"no effect {tag} in the archives")
        chosen.append(match[0])
    compat = chosen[0]["compat"]
    if any(v["compat"] != compat for v in chosen):
        corpus.fail("the chosen effects use different compat headers")
    fx_files = [v["fx"] for v in chosen]
    report = {"date": datetime.datetime.now().isoformat(timespec="seconds"), "effects": [v["tag"] for v in chosen],
              "size": args.size, "seed": args.seed, "tolerance": args.tolerance}
    ok = True

    # 1. FXC
    hlsl_dir = os.path.join(work, "hlsl")
    os.makedirs(hlsl_dir, exist_ok=True)
    result = run([fxhlsl, "--compat", compat, "--out", hlsl_dir, "--fxc"] + fx_files, stdout=subprocess.PIPE,
                 stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", creationflags=BELOW_NORMAL_PRIORITY_CLASS)
    print(result.stdout)
    report["fxc"] = {"exit": result.returncode, "summary": [line for line in result.stdout.splitlines() if "FXC" in line or "stages:" in line]}
    ok = ok and result.returncode == 0

    # 2. SPIR-V through Diligent's glslang, then spirv-val
    spirv_dir = os.path.join(work, "spirv")
    for old in glob.glob(os.path.join(spirv_dir, "*.spv")):
        os.remove(old)
    os.makedirs(spirv_dir, exist_ok=True)
    result = run([fxdiff, "--compat", compat, "--out", spirv_dir, "--spirv"] + fx_files, stdout=subprocess.PIPE,
                 stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", creationflags=BELOW_NORMAL_PRIORITY_CLASS)
    print(result.stdout)
    spirv = json.load(open(os.path.join(spirv_dir, "fxdiff.json"), encoding="utf-8"))["spirv"]
    validator = os.path.join(args.ndk, "shader-tools", "windows-x86_64", "spirv-val.exe")
    validated, invalid = 0, []
    for entry in spirv:
        if not entry["ok"]:
            continue
        check = subprocess.run([validator, "--target-env", "vulkan1.0", os.path.join(spirv_dir, entry["file"])],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        if check.returncode == 0:
            validated += 1
        else:
            invalid.append(entry["file"] + ": " + check.stdout.strip()[:300])
    glslang_ok = sum(1 for e in spirv if e["ok"])
    print(f"SPIR-V: glslang {glslang_ok}/{len(spirv)}, spirv-val {validated}/{glslang_ok}")
    for line in invalid:
        print("  spirv-val: " + line)
    report["spirv"] = {"stages": len(spirv), "glslang": glslang_ok, "spirvVal": validated, "invalid": invalid,
                       "failed": [e["effect"] + " " + e["stage"] + " " + e["entry"] + ": " + e["messages"][:300]
                                  for e in spirv if not e["ok"]]}
    ok = ok and glslang_ok == len(spirv) and validated == glslang_ok and result.returncode == 0

    # 3. Render: D3D9 against the Diligent backend's effect layer
    if not args.no_render:
        render_dir = os.path.join(work, "render")
        os.makedirs(render_dir, exist_ok=True)
        for old in glob.glob(os.path.join(render_dir, "*.f32")) + glob.glob(os.path.join(render_dir, "*.png")):
            os.remove(old)
        passes = []
        violations = []
        diligent_errors = 0
        for variant in chosen:
            name = os.path.splitext(os.path.basename(variant["fx"]))[0]
            command = [fxdiff, "--compat", compat, "--out", render_dir, "--render", "--size", str(args.size),
                       "--seed", str(args.seed), "--tolerance", repr(args.tolerance)]
            for assignment in EFFECT_OVERRIDES.get(name, []):
                command += ["--set", assignment]
            command.append(variant["fx"])
            gfx_capture.acquire_lock(args.lock, args.lock_wait, "fxdiff " + name)
            try:
                print("$ " + " ".join(command), flush=True)
                code, output, found = run_watched(command, args.timeout)
            finally:
                gfx_capture.release_lock(args.lock)
            print(output)
            violations += found
            data = json.load(open(os.path.join(render_dir, "fxdiff.json"), encoding="utf-8"))
            diligent_errors += data["diligentErrors"]
            passes += data["passes"]
            if code not in (0, 1):
                ok = False
        for entry in passes:
            if entry["status"].startswith("differ"):
                stem = os.path.join(render_dir, f"{entry['effect']}.{entry['technique']}.p{entry['pass']}.{entry['mode']}")
                entry["heatmap"] = heatmap(stem, args.size)
        agree = [p for p in passes if p["status"].startswith("agree")]
        differ = [p for p in passes if p["status"].startswith("differ")]
        skipped = [p for p in passes if p["status"].startswith("skipped")]
        empty = [p for p in agree if p["status"] != "agree"]
        worst_float = max([p["maxDelta"] for p in passes if p["mode"] == "float" and not p["status"].startswith("skipped")] or [0.0])
        worst_unorm = max([p["maxDelta"] for p in passes if p["mode"] == "unorm" and not p["status"].startswith("skipped")] or [0.0])
        print(f"render: {len(passes)} pass renders, {len(agree)} agree ({len(empty)} drew nothing), {len(differ)} differ, "
              f"{len(skipped)} skipped; max |delta| float {worst_float:.3g}, unorm {worst_unorm:.3g}; "
              f"Diligent errors {diligent_errors}; window violations {len(violations)}")
        for p in differ + skipped + empty:
            print(f"  {p['effect']}/{p['technique']}/P{p['pass']} {p['mode']}: {p['status']} (max {p['maxDelta']:.3g}, "
                  f"over {p['overTolerance']}, coverage {p['coverageMismatch']}, drawn {p['drawn']})"
                  + (f" heat map {p['heatmap']}" if p.get("heatmap") else ""))
        report["render"] = {"passes": passes, "agree": len(agree), "differ": len(differ), "skipped": len(skipped),
                            "drewNothing": len(empty), "maxDeltaFloat": worst_float, "maxDeltaUnorm": worst_unorm,
                            "diligentErrors": diligent_errors, "windowViolations": violations}
        # The gate: no pass drawn by one side only, and at most 0.1 % of a pass's drawn pixels over
        # the tolerance (the share the menu's pixel gate allows). Passes with any such pixel are
        # listed above with their heat maps.
        outliers = [p for p in differ if p["coverageMismatch"] == 0 and p["overTolerance"] <= p["drawn"] // 1000]
        failing = [p for p in differ if p not in outliers]
        print(f"render gate: {len(agree)} exact within tolerance, {len(outliers)} with outliers on <= 0.1 % of their pixels, "
              f"{len(failing)} failing")
        report["render"]["outlierPasses"] = len(outliers)
        report["render"]["failingPasses"] = len(failing)
        ok = ok and not failing and not skipped and not empty and not violations and diligent_errors == 0

    report["passed"] = ok
    with open(os.path.join(work, "fxdiff-gate.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=1)
    print("FXDIFF GATE: " + ("PASSED" if ok else "FAILED") + f" ({os.path.join(work, 'fxdiff-gate.json')})")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
