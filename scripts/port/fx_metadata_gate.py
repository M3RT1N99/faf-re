"""Gate for the effect front end (M6a step 2): fxmeta's metadata must equal
what D3DX reports, for every effect variant the game and FAF load.

    python scripts/port/fx_metadata_gate.py [--work DIR] [--build DIR] [--no-build]
                                            [--scfa DIR] [--faf DIR] [--only TEXT ...]
                                            [--hal-caps] [--arm64-check] [--ndk DIR] [--show N]

What it does:

1. Finds the effect archives: SCFA's gamedata (*.scd) and FAF's gamedata
   (C:\\ProgramData\\FAForever\\gamedata: effects.nx2/.nx5, the Nomads
   effects.nmd, the old faforever.faf, any other zip with effects/*.fx).
   Archives are only read. Every effects/*.fx and effects/d3d9states.compat is
   extracted into WORK/src/<archive>/ - scratch or buildstage, never the repo.
2. Forms the variants the engine compiles: compat + .fx, concatenated as
   CD3DEffect::InitEffectFromFile does (CD3DEffectTechnique.cpp:459-476). An
   archive without its own compat gets the one FAF mounts (effects.nx2; SCFA's
   if there is no FAF install). Byte-identical variants (effects.nx5 =
   effects.nx2) are checked once. The synthetic effects in
   port/graphics/fx/tests/effects/*.fx are added; they exercise grammar and
   value conversions the shipped effects do not use.
3. Builds port/graphics/fx (CMake, Win32 like main.exe) and runs its unit tests.
4. Runs fxd3dx_dump (D3DX reflection through d3dx9_43 + the legacy compiler on
   a fake device) and fxmeta (the portable front end) on every variant and
   compares their JSON: techniques and their validity per device profile (and
   the FindNextValidTechnique order), passes with annotations, pass states and
   the vertex/pixel shader entry points, parameters with descs, default
   values, strings, annotations, struct members, and the sampler_state of
   every sampler a pass uses.
5. Prints a table and exits 1 if anything differs.

--arm64-check also compiles the library and fxmeta for aarch64-linux-android
with the NDK clang (-Werror) and links fxmeta, to keep the front end portable.
--hal-caps makes D3DX's "all" profile the real adapter's caps (only
IDirect3D9::GetDeviceCaps is called; no device or window is created).
Exit code: 0 when everything matches, 1 on a difference or a failed unit test,
2 on setup errors (no archives, build failure).
"""
import argparse
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FX_DIR = os.path.join(REPO, "port", "graphics", "fx")
DEFAULT_SCFA = r"C:\Program Files (x86)\Steam\steamapps\common\Supreme Commander Forged Alliance\gamedata"
DEFAULT_FAF = r"C:\ProgramData\FAForever\gamedata"
DEFAULT_NDK = r"C:\Android\sdk\ndk\29.0.14206865"

PARAM_FIELDS = ("name", "semantic", "class", "type", "rows", "columns", "elements", "structMembers", "flags", "bytes")


def fail(message):
    print("fx_metadata_gate: " + message, file=sys.stderr)
    sys.exit(2)


# ---- corpus ----------------------------------------------------------------------


def archive_effects(path):
    """{lower-case base name: bytes} of effects/*.fx and the compat in a zip archive."""
    try:
        archive = zipfile.ZipFile(path)
    except (zipfile.BadZipFile, OSError):
        return {}
    found = {}
    with archive:
        for info in archive.infolist():
            name = info.filename.replace("\\", "/").lower()
            if not name.startswith("effects/") or info.file_size == 0 or "/" in name[len("effects/"):]:
                continue
            base = name[len("effects/"):]
            if base.endswith(".fx") or base == "d3d9states.compat":
                found[base] = archive.read(info.filename)
    return found


def collect_corpus(scfa_dir, faf_dir, work):
    """Extracts the effects and returns (variants, archives) where a variant is a dict
    with tag, fx path, compat path, source archive."""
    archives = []
    for root, pattern in ((scfa_dir, "*.scd"), (faf_dir, "*")):
        if not root or not os.path.isdir(root):
            continue
        for path in sorted(glob.glob(os.path.join(root, pattern))):
            if os.path.isfile(path):
                effects = archive_effects(path)
                if any(name.endswith(".fx") for name in effects):
                    archives.append((path, effects))
    if not archives:
        fail("no effect archives found (use --scfa / --faf)")

    def tag_of(path):
        base = os.path.basename(path).lower()
        stem, ext = os.path.splitext(base)
        return (stem + "_" + ext[1:]).replace(" ", "_")

    # The compat FAF mounts: effects.nx2's, else SCFA's.
    fallback_compat = None
    for path, effects in archives:
        if os.path.basename(path).lower() == "effects.nx2" and "d3d9states.compat" in effects:
            fallback_compat = (tag_of(path), effects["d3d9states.compat"])
    if fallback_compat is None:
        for path, effects in archives:
            if "d3d9states.compat" in effects:
                fallback_compat = (tag_of(path), effects["d3d9states.compat"])
                break

    src_root = os.path.join(work, "src")
    if os.path.isdir(src_root):
        shutil.rmtree(src_root)
    variants = []
    seen = {}
    for path, effects in archives:
        tag = tag_of(path)
        folder = os.path.join(src_root, tag)
        os.makedirs(folder, exist_ok=True)
        for name, data in effects.items():
            with open(os.path.join(folder, name), "wb") as handle:
                handle.write(data)
        if "d3d9states.compat" in effects:
            compat_path = os.path.join(folder, "d3d9states.compat")
            compat_data = effects["d3d9states.compat"]
            compat_from = tag
        elif fallback_compat is not None:
            compat_path = os.path.join(src_root, fallback_compat[0], "d3d9states.compat")
            compat_data = fallback_compat[1]
            compat_from = fallback_compat[0]
        else:
            fail("no d3d9states.compat for " + path)
        for name in sorted(n for n in effects if n.endswith(".fx")):
            key = hashlib.sha1(compat_data + b"\0" + effects[name]).hexdigest()
            variant_tag = tag + "/" + name[:-3]
            if key in seen:
                seen[key]["duplicates"].append(variant_tag)
                continue
            variant = {
                "tag": variant_tag,
                "fx": os.path.join(folder, name),
                "compat": compat_path,
                "compat_from": compat_from,
                "archive": path,
                "duplicates": [],
                "macros": [],
            }
            seen[key] = variant
            variants.append(variant)
    return variants, archives


def synthetic_variants():
    """port/graphics/fx/tests/effects/*.fx. A first line `// gate-macros: A=1 B=2`
    names macros passed to both tools."""
    variants = []
    for path in sorted(glob.glob(os.path.join(FX_DIR, "tests", "effects", "*.fx"))):
        macros = []
        with open(path, encoding="latin-1") as handle:
            first = handle.readline().strip()
        if first.startswith("// gate-macros:"):
            for item in first.split(":", 1)[1].split():
                name, _, value = item.partition("=")
                macros.append((name, value or "1"))
        variants.append({
            "tag": "tests/" + os.path.basename(path)[:-3],
            "fx": path,
            "compat": None,
            "compat_from": None,
            "archive": None,
            "duplicates": [],
            "macros": macros,
        })
    return variants


# ---- build -----------------------------------------------------------------------


def run(command, **kwargs):
    return subprocess.run(command, capture_output=True, text=True, errors="replace", **kwargs)


def build(build_dir):
    configure = run(["cmake", "-S", FX_DIR, "-B", build_dir, "-A", "Win32"])
    if configure.returncode != 0:
        print(configure.stdout + configure.stderr)
        fail("cmake configure failed")
    started = time.time()
    result = run(["cmake", "--build", build_dir, "--config", "Release", "--", "-m", "-v:m"])
    if result.returncode != 0:
        print(result.stdout + result.stderr)
        fail("build failed")
    warnings = [line for line in result.stdout.splitlines() if ": warning C" in line]
    print("build: %s (%.1f s, %d compiler warnings)" % (build_dir, time.time() - started, len(warnings)))
    for line in warnings[:20]:
        print("  " + line.strip())


def tool(build_dir, name):
    path = os.path.join(build_dir, "Release", name + ".exe")
    if not os.path.isfile(path):
        fail("missing " + path + " (build first, or drop --no-build)")
    return path


def arm64_check(ndk, work):
    """Compiles the library and fxmeta for aarch64-linux-android26 with the NDK
    clang (-Wall -Wextra -Werror) and links fxmeta: the front end has no
    Windows dependency."""
    clang = None
    for candidate in glob.glob(os.path.join(ndk, "toolchains", "llvm", "prebuilt", "*", "bin", "clang++.exe")):
        clang = candidate
    if clang is None:
        print("arm64 check: skipped, no NDK clang under " + ndk)
        return True
    out_dir = os.path.join(work, "arm64")
    os.makedirs(out_dir, exist_ok=True)
    sources = sorted(glob.glob(os.path.join(FX_DIR, "src", "*.cpp"))) + [os.path.join(FX_DIR, "tools", "fxmeta.cpp")]
    objects = []
    ok = True
    for source in sources:
        obj = os.path.join(out_dir, os.path.splitext(os.path.basename(source))[0] + ".o")
        result = run([clang, "--target=aarch64-linux-android26", "-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra",
                      "-Wshadow", "-Werror", "-I", os.path.join(FX_DIR, "include"), "-c", source, "-o", obj])
        if result.returncode != 0:
            ok = False
            print("arm64 check: " + os.path.basename(source) + " FAILED")
            print(result.stdout + result.stderr)
        objects.append(obj)
    if ok:
        binary = os.path.join(out_dir, "fxmeta")
        result = run([clang, "--target=aarch64-linux-android26", "-static-libstdc++", "-o", binary] + objects)
        if result.returncode != 0:
            ok = False
            print("arm64 check: link FAILED")
            print(result.stdout + result.stderr)
        else:
            print("arm64 check: %d sources compiled for aarch64-linux-android26 (-Wall -Wextra -Wshadow -Werror), "
                  "fxmeta linked: %s (%d bytes; %s)" % (len(sources), binary, os.path.getsize(binary),
                                                      os.path.relpath(clang, ndk)))
    return ok


# ---- comparison ------------------------------------------------------------------


class Diff:
    def __init__(self):
        self.lines = []
        self.counts = {}

    def count(self, key, n=1):
        self.counts[key] = self.counts.get(key, 0) + n

    def add(self, where, message):
        self.lines.append(where + ": " + message)


def compare_desc(diff, where, oracle, mine):
    for field in PARAM_FIELDS:
        if oracle.get(field) != mine.get(field):
            diff.add(where, "%s: d3dx %r, fxmeta %r" % (field, oracle.get(field), mine.get(field)))


def compare_annotations(diff, where, oracle, mine):
    if len(oracle) != len(mine):
        diff.add(where, "annotation count: d3dx %d, fxmeta %d" % (len(oracle), len(mine)))
        return
    for a, b in zip(oracle, mine):
        here = where + " <" + str(a.get("name")) + ">"
        diff.count("annotations")
        compare_desc(diff, here, a, b)
        for field in ("value", "string"):
            if a.get(field) != b.get(field):
                diff.add(here, "%s: d3dx %r, fxmeta %r" % (field, a.get(field), b.get(field)))


def compare_shader(diff, where, oracle_pass, my_pass, key, samplers=frozenset()):
    has_oracle = key in oracle_pass
    has_mine = key in my_pass
    if has_oracle != has_mine:
        diff.add(where, "%s assigned: d3dx %s, fxmeta %s" % (key, has_oracle, has_mine))
        return
    if not has_oracle:
        return
    a, b = oracle_pass[key], my_pass[key]
    diff.count("shaders")
    if (a is None) != (b is None):
        diff.add(where, "%s null: d3dx %s, fxmeta %s" % (key, a is None, b is None))
        return
    if a is None:
        return
    # An asm block has no entry point; its debug info names none.
    for field in ("version",) if b.get("asm") else ("version", "entry"):
        if a.get(field) != b.get(field):
            diff.add(where, "%s %s: d3dx %r, fxmeta %r" % (key, field, a.get(field), b.get(field)))
    # Uniform arguments are folded into the bytecode, except sampler
    # arguments, whose parameter the constant table names.
    for argument in b.get("arguments", []):
        if argument in samplers:
            diff.count("sampler arguments")
            if argument not in a.get("samplers", []):
                diff.add(where, "%s argument %s: not among the shader's samplers %s" % (key, argument, a.get("samplers")))


def compare(oracle, mine):
    diff = Diff()
    # Parameters.
    po, pm = oracle["parameters"], mine["parameters"]
    if [p["name"] for p in po] != [p["name"] for p in pm]:
        diff.add("parameters", "names/order differ: d3dx %s, fxmeta %s" % ([p["name"] for p in po], [p["name"] for p in pm]))
    by_name = {p["name"]: p for p in pm}
    sampler_declared = sum(1 for p in pm if "sampler" in p)
    diff.count("samplers declared", sampler_declared)
    for a in po:
        b = by_name.get(a["name"])
        where = "parameter " + a["name"]
        if b is None:
            continue
        diff.count("parameters")
        compare_desc(diff, where, a, b)
        compare_annotations(diff, where, a.get("annotations", []), b.get("annotations", []))
        for field in ("value", "string"):
            if a.get(field) != b.get(field):
                diff.add(where, "%s: d3dx %r, fxmeta %r" % (field, a.get(field), b.get(field)))
        if a.get("value") is not None:
            diff.count("values")
        members_a, members_b = a.get("members", []), b.get("members", [])
        if len(members_a) != len(members_b):
            diff.add(where, "member count: d3dx %d, fxmeta %d" % (len(members_a), len(members_b)))
        for ma, mb in zip(members_a, members_b):
            compare_desc(diff, where + "." + str(ma.get("name")), ma, mb)
        if "sampler" in a:
            diff.count("samplers used")
            if a["sampler"].get("inconsistent"):
                diff.add(where, "D3DX applied different sampler states in different passes")
            if "sampler" not in b:
                diff.add(where, "sampler_state: d3dx has one, fxmeta none")
            else:
                for field in ("texture", "states"):
                    if a["sampler"].get(field) != b["sampler"].get(field):
                        diff.add(where, "sampler %s: d3dx %r, fxmeta %r" % (field, a["sampler"].get(field), b["sampler"].get(field)))
    # Techniques.
    sampler_names = frozenset(p["name"] for p in pm if 10 <= p.get("type", 0) <= 14)
    to, tm = oracle["techniques"], mine["techniques"]
    if len(to) != len(tm):
        diff.add("techniques", "count: d3dx %d, fxmeta %d" % (len(to), len(tm)))
    for ti, (a, b) in enumerate(zip(to, tm)):
        where = "technique %d %s" % (ti, a.get("name"))
        diff.count("techniques")
        if a.get("name") != b.get("name"):
            diff.add(where, "name: fxmeta %r" % b.get("name"))
        compare_annotations(diff, where, a.get("annotations", []), b.get("annotations", []))
        if a.get("valid") != b.get("valid"):
            diff.add(where, "valid: d3dx %r, fxmeta %r" % (a.get("valid"), b.get("valid")))
        if len(a["passes"]) != len(b["passes"]):
            diff.add(where, "pass count: d3dx %d, fxmeta %d" % (len(a["passes"]), len(b["passes"])))
        for pi, (pa, pb) in enumerate(zip(a["passes"], b["passes"])):
            here = "%s pass %d %s" % (where, pi, pa.get("name"))
            diff.count("passes")
            if pa.get("name") != pb.get("name"):
                diff.add(here, "name: fxmeta %r" % pb.get("name"))
            compare_annotations(diff, here, pa.get("annotations", []), pb.get("annotations", []))
            sa = [(s["op"], s["index"], s["state"], s.get("value"), s.get("texture")) for s in pa["states"]]
            sb = [(s["op"], s["index"], s["state"], s.get("value"), s.get("texture")) for s in pb["states"]]
            diff.count("pass states", len(sa))
            if sa != sb:
                diff.add(here, "states: d3dx %s, fxmeta %s" % (sa, sb))
            compare_shader(diff, here, pa, pb, "vertexShader", sampler_names)
            compare_shader(diff, here, pa, pb, "pixelShader", sampler_names)
    if oracle.get("validTechniques") != mine.get("validTechniques"):
        for profile in sorted(set(oracle.get("validTechniques", {})) | set(mine.get("validTechniques", {}))):
            if oracle["validTechniques"].get(profile) != mine["validTechniques"].get(profile):
                diff.add("validTechniques", profile + " order differs")
    for profile, names in oracle.get("validTechniques", {}).items():
        diff.count("valid " + profile, len(names))
    unexpected = set(oracle.get("unexpectedDeviceCalls", [])) - {"GetDeviceCaps", "ValidateDevice"}
    if unexpected:
        diff.add("fake device", "D3DX called unmodelled device methods: %s" % sorted(unexpected))
    return diff


# ---- main ------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--work", default=os.path.join(REPO, "buildstage", "fx-gate"),
                        help="extracted effects and JSON (default buildstage/fx-gate)")
    parser.add_argument("--build", default=os.path.join(REPO, "buildstage", "fx", "win32"),
                        help="CMake build directory (default buildstage/fx/win32)")
    parser.add_argument("--no-build", action="store_true", help="use the tools already built in --build")
    parser.add_argument("--scfa", default=DEFAULT_SCFA, help="SCFA gamedata folder")
    parser.add_argument("--faf", default=DEFAULT_FAF, help="FAF gamedata folder")
    parser.add_argument("--only", nargs="*", help="only variants whose tag contains one of these")
    parser.add_argument("--hal-caps", action="store_true",
                        help="D3DX validity for 'all' uses this PC's real adapter caps (no device is created)")
    parser.add_argument("--arm64-check", action="store_true", help="also compile the library with the NDK for arm64")
    parser.add_argument("--ndk", default=os.environ.get("ANDROID_NDK_ROOT", DEFAULT_NDK))
    parser.add_argument("--show", type=int, default=12, help="differences printed per variant")
    args = parser.parse_args()

    work = os.path.abspath(args.work)
    try:
        inside_repo = os.path.commonpath([work.lower(), REPO.lower()]) == REPO.lower()
    except ValueError:  # another drive
        inside_repo = False
    if inside_repo and not work.lower().startswith(os.path.join(REPO, "buildstage").lower()):
        fail("--work must be outside the repository or under buildstage/ (game data never goes into the repo)")
    os.makedirs(work, exist_ok=True)
    build_dir = os.path.abspath(args.build)

    if not args.no_build:
        build(build_dir)
    dumper = tool(build_dir, "fxd3dx_dump")
    fxmeta = tool(build_dir, "fxmeta")
    tests = run([tool(build_dir, "fx_tests")])
    print("unit tests: " + tests.stdout.strip().splitlines()[-1] if tests.stdout.strip() else "unit tests: no output")
    ok = tests.returncode == 0

    if args.arm64_check:
        ok = arm64_check(args.ndk, work) and ok

    variants, archives = collect_corpus(args.scfa, args.faf, work)
    print("archives with effects: " + ", ".join(
        "%s (%d .fx)" % (os.path.basename(path), sum(1 for n in effects if n.endswith(".fx"))) for path, effects in archives))
    variants += synthetic_variants()
    if args.only:
        variants = [v for v in variants if any(text in v["tag"] for text in args.only)]
    game_count = sum(1 for v in variants if not v["tag"].startswith("tests/"))
    json_dir = os.path.join(work, "json")
    os.makedirs(json_dir, exist_ok=True)

    totals = {}
    failed = []
    print()
    print("%-34s %-6s %5s %5s %6s %6s %5s %5s %5s  %s" % ("variant", "result", "tech", "pass", "states", "params", "vals", "ann", "smp", "valid all/sm2/sm1"))
    for variant in variants:
        stem = variant["tag"].replace("/", "__")
        common = []
        if variant["compat"]:
            common += ["--compat", variant["compat"]]
        for name, value in variant["macros"]:
            common += ["-D", name + "=" + value]
        oracle_path = os.path.join(json_dir, stem + ".d3dx.json")
        mine_path = os.path.join(json_dir, stem + ".fxmeta.json")
        dump_args = [dumper] + common + ["--out", oracle_path, variant["fx"]]
        if args.hal_caps:
            dump_args.insert(1, "--hal-caps")
        oracle_run = run(dump_args)
        mine_run = run([fxmeta] + common + ["--out", mine_path, variant["fx"]])
        if variant["macros"]:
            variant["duplicates"].append("macros " + " ".join(n + "=" + v for n, v in variant["macros"]))
        if oracle_run.returncode != 0 or mine_run.returncode != 0:
            failed.append(variant["tag"])
            print("%-34s %-6s d3dx exit %d, fxmeta exit %d" % (variant["tag"], "ERROR", oracle_run.returncode, mine_run.returncode))
            for text in (oracle_run.stderr, mine_run.stderr):
                for line in text.strip().splitlines()[:args.show]:
                    print("    " + line)
            continue
        with open(oracle_path, encoding="utf-8") as handle:
            oracle = json.load(handle)
        with open(mine_path, encoding="utf-8") as handle:
            mine = json.load(handle)
        diff = compare(oracle, mine)
        for key, value in diff.counts.items():
            totals[key] = totals.get(key, 0) + value
        c = diff.counts
        valid = "%d/%d/%d" % (c.get("valid all", 0), c.get("valid sm2", 0), c.get("valid sm1", 0))
        result = "PASS" if not diff.lines else "FAIL"
        print("%-34s %-6s %5d %5d %6d %6d %5d %5d %2d/%-2d  %s%s" % (
            variant["tag"], result, c.get("techniques", 0), c.get("passes", 0), c.get("pass states", 0),
            c.get("parameters", 0), c.get("values", 0), c.get("annotations", 0), c.get("samplers used", 0),
            c.get("samplers declared", 0), valid,
            ("  (= " + ", ".join(variant["duplicates"]) + ")") if variant["duplicates"] else ""))
        if diff.lines:
            failed.append(variant["tag"])
            for line in diff.lines[:args.show]:
                print("    " + line)
            if len(diff.lines) > args.show:
                print("    ... %d more" % (len(diff.lines) - args.show))
        mine_warnings = [line for line in mine_run.stderr.splitlines() if line.strip()]
        for line in mine_warnings[:3]:
            print("    fxmeta: " + line)

    print()
    print("variants: %d (%d from the game archives, %d synthetic), failed: %d" % (
        len(variants), game_count, len(variants) - game_count, len(failed)))
    print("compared: %d techniques (validity on 3 device profiles), %d passes, %d pass states, %d shader slots "
          "(%d sampler arguments), %d parameters, %d default values, %d annotations, %d of %d sampler_state blocks "
          "(the rest no pass uses)" % (
              totals.get("techniques", 0), totals.get("passes", 0), totals.get("pass states", 0),
              totals.get("shaders", 0), totals.get("sampler arguments", 0), totals.get("parameters", 0),
              totals.get("values", 0), totals.get("annotations", 0), totals.get("samplers used", 0),
              totals.get("samplers declared", 0)))
    print("JSON: " + json_dir)
    if failed or not ok:
        print("GATE FAILED: " + ", ".join(failed) if failed else "GATE FAILED (unit tests or arm64 check)")
        return 1
    print("GATE PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
