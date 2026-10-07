#!/usr/bin/env python3
"""Build the Android headless replay runner: libfafengine.so plus faf_headless_runner (M3b).

Compiles the TUs of port/engine/runner/closure.txt (written by link_closure.py) with the arm64
sweep's per-TU command (engine_sweep.py: main.vcxproj's Release|x64 defines and include
directories, port/engine/shim, port/engine/compile_flags.txt), plus `-fPIC -ffunction-sections
-fdata-sections -g`; the runner's own sources (port/engine/runner/*.cpp); and the WildMagic
Foundation TUs the closure lists, from the local, gitignored dependencies/WildMagic3p8. Then links

  libfafengine.so       every closure object whole (no archive, no --gc-sections, so every static
                        initialiser is in), -shared -Wl,--no-undefined, libc++ static, -lz -llog,
                        plus port/engine/lowarena/LowArenaThreads.cpp and
                        -Wl,--wrap=pthread_create,pthread_join,pthread_detach (arena thread stacks)
  faf_headless_runner   RunnerMain.cpp's `main` plus the low arena (port/engine/lowarena: LowArena.cpp,
                        dlmalloc), which exports the malloc family and the lowarena_* hooks, loads
                        libfafengine.so below 2 GB and calls `faf_headless_main` on an arena stack
                        (M3c; FAF_LOWARENA=0 at run time turns the arena off); plus the replay input
                        (ReplayFile.cpp: .fafreplay decoding with the vendored zstd decoder in
                        port/third_party/zstd and the system libz, /replayinfo, /convertreplay) and
                        the crash reporter (RunnerCrash.cpp), release 0.4.0

With --probe it also links libfafarenaprobe.so (port/engine/lowarena/probe), a stand-in for the
engine library that checks where the arena puts heap, stacks, TLS, image and file views.

and reports what stops the link: compile failures, undefined symbols grouped by the TU that defines
them on Windows (their owner, from the Win32 objects through link_closure.py) and by the TUs that
reference them, and duplicate symbols with both definitions.

Compiling is incremental: a TU is skipped while its command and every file it includes (clang's
dependency file) are unchanged, failures included. Several builds can run side by side in their
own --out directories; a new directory is seeded from the default one (--seed), copying every
object whose command and sources match, so it does not start from zero.

usage:
  python scripts/port/build_runner.py [--abi arm64-v8a|x86_64] [--jobs N] [--out DIR]
  python scripts/port/build_runner.py --files moho/sim/Sim.cpp "moho/net/*"   # recompile these, relink
  python scripts/port/build_runner.py --link-only                            # relink what is there

Output (default buildstage/runner/<abi>):
  libfafengine.so, faf_headless_runner
  report.md, results.json   compile failures, undefined and duplicate symbols, link errors
  link.log                  the linker's complete output
  obj/, logs/               per-TU objects, dependency files, stamps and compiler logs

Exit code: 0 when both binaries linked, 1 when something failed to compile or link, 2 for usage
errors.
"""

import argparse
import collections
import concurrent.futures
import datetime
import fnmatch
import glob
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine_sweep as es  # noqa: E402
import link_closure as lc  # noqa: E402

REPO_ROOT = es.REPO_ROOT
SDK_DIR = es.DEFAULT_SDK
RUNNER_DIR = os.path.join(REPO_ROOT, "port", "engine", "runner")
ABIS = {"arm64-v8a": "aarch64-linux-android26", "x86_64": "x86_64-linux-android26"}
BUILD_FLAGS = ["-fPIC", "-ffunction-sections", "-fdata-sections", "-g"]
STAMP_VERSION = 1
LIB_NAME = "libfafengine.so"
EXE_NAME = "faf_headless_runner"
PROBE_NAME = "libfafarenaprobe.so"

# The low arena (port/engine/lowarena/README.md). Its own sources are plain port code, compiled with
# their own flags (no engine defines, no shim): LowArena.cpp and the vendored dlmalloc go into the
# executable, LowArenaThreads.cpp (the pthread wrappers) into the engine library. The probe is engine-
# side code (it uses the shim's VirtualAlloc/MapViewOfFile), compiled like the runner's sources.
LOWARENA_DIR = os.path.join(REPO_ROOT, "port", "engine", "lowarena")
LOWARENA_EXE_SOURCES = ["LowArena.cpp", "LowArenaHeap.c"]
LOWARENA_LIB_SOURCES = ["LowArenaThreads.cpp"]
LOWARENA_PROBE_SOURCE = "probe/LowArenaProbe.cpp"
LOWARENA_FLAGS = ["-O2", "-g", "-fno-omit-frame-pointer", "-fPIC", "-Wall", "-Wextra", "-ffunction-sections", "-fdata-sections"]
# Executable-only sources of port/engine/runner (release 0.4.0): plain port code like the low arena
# (own flags, no engine defines, no shim), never compiled into libfafengine.so. Every other *.cpp in
# port/engine/runner is a runner source of the engine library.
RUNNER_EXE_SOURCES = ["ReplayFile.cpp", "RunnerCrash.cpp"]
# The zstd decoder (port/third_party/zstd: zstd 1.5.7's lib/common and lib/decompress, BSD licence),
# linked into the executable for .fafreplay bodies. C, no assembly (ZSTD_DISABLE_ASM: the x86-64
# Huffman loop in huf_decompress_amd64.S stays out), no legacy formats, no tracing hooks.
ZSTD_DIR = os.path.join(REPO_ROOT, "port", "third_party", "zstd", "lib")
ZSTD_SOURCES = ["common/debug.c", "common/entropy_common.c", "common/error_private.c", "common/fse_decompress.c",
                "common/xxhash.c", "common/zstd_common.c", "decompress/huf_decompress.c", "decompress/zstd_ddict.c",
                "decompress/zstd_decompress.c", "decompress/zstd_decompress_block.c"]
ZSTD_FLAGS = ["-O2", "-g", "-fPIC", "-ffunction-sections", "-fdata-sections", "-DZSTD_DISABLE_ASM=1",
              "-DZSTD_LEGACY_SUPPORT=0", "-DZSTD_TRACE=0"]
# What the executable exports so that libc, libc++ and libfafengine.so bind to it (lld leaves an
# executable's symbols out of .dynsym unless asked). Not -rdynamic: that would also export the
# executable's static libc++, which the engine library would then bind to instead of its own.
EXE_EXPORTS = ["malloc", "free", "calloc", "realloc", "reallocarray", "memalign", "posix_memalign",
               "aligned_alloc", "valloc", "pvalloc", "malloc_usable_size", "lowarena_*"]
LIB_WRAPS = ["pthread_create", "pthread_join", "pthread_detach"]

# Wild Magic 3.8's Wm3System.cpp includes <sys/timeb.h> for ftime() on every platform but Apple.
# Bionic has neither; this stand-in is generated into the build directory and searched first for
# the WildMagic TUs only (System::GetTime is not on the sim path; it only has to compile and link).
WM3_TIMEB = """\
#pragma once
/* Generated by scripts/port/build_runner.py: <sys/timeb.h> for Wild Magic 3.8 on bionic. */
#include <time.h>
struct timeb {
  time_t time;
  unsigned short millitm;
  short timezone;
  short dstflag;
};
static inline int ftime(struct timeb* tb) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  tb->time = ts.tv_sec;
  tb->millitm = (unsigned short)(ts.tv_nsec / 1000000);
  tb->timezone = 0;
  tb->dstflag = 0;
  return 0;
}
"""


# Wild Magic 3.8 calls members of a dependent base class unqualified (Query2Filtered::ToLine calls
# Query2's Det2), which only MSVC's lookup accepts. The build directory gets copies of these headers
# with `this->` added, searched before Wild Magic's own (a quoted include of the .inl then finds the
# patched copy beside the patched header). Nothing else changes; dependencies/ stays untouched.
WM3_OVERLAY = {
    "RationalArithmetic/Wm3Query2Filtered.h": None,
    "RationalArithmetic/Wm3Query2Filtered.inl": (r"(?<![\w.>:])(Det[234])\(", r"this->\1("),
    "RationalArithmetic/Wm3Query3Filtered.h": None,
    "RationalArithmetic/Wm3Query3Filtered.inl": (r"(?<![\w.>:])(Det[234])\(", r"this->\1("),
}


def fwd(p):
    return p.replace("\\", "/")


def norm(p):
    return fwd(os.path.normpath(p))


# ------------------------------------------------------------------------------------------------
# Commands
# ------------------------------------------------------------------------------------------------

class Unit:
    """One compile: a closure TU, a WildMagic TU or a runner source."""

    def __init__(self, tu, kind, src, obj, cmd):
        self.tu, self.kind, self.src, self.obj, self.cmd = tu, kind, src, obj, cmd
        self.log = None


def set_target(cmd, triple):
    out = [a for a in cmd if not a.startswith("--target=")]
    out.insert(1, f"--target={triple}")
    return out


def with_deps(cmd, obj):
    return cmd + ["-MD", "-MF", obj + ".d", "-MT", "obj"]


class ProjectView:
    """main.vcxproj's items plus extra sources compiled with the project's default settings."""

    def __init__(self, project, extra):
        self._p = project
        self.items = list(project.items) + [(tu, dict(project.defaults)) for tu in extra]
        self.dir = project.dir

    def split_list(self, value):
        return self._p.split_list(value)

    def include_dirs(self, meta):
        return self._p.include_dirs(meta)


def wm3_include_dirs(project):
    raw = project.props.get("Wm3IncludePath", "")
    return [norm(d) for d in raw.split(";") if d.strip() and "WildMagic3p8" in d and "Renderers" not in d
            and "Applications" not in d]


def build_units(args, project, closure, clang, triple, out_dir):
    flag_args, drop_defines, drop_includes = es.read_flags_file(es.DEFAULT_FLAGS)
    flag_args = [a for a in flag_args if not a.startswith("--target=")]
    shim = os.path.realpath(es.DEFAULT_SHIM)
    built = {tu for tu, _ in project.items}
    engine = [t for t in closure if not t.startswith(lc.WM3_PREFIX)]
    wm3 = [t for t in closure if t.startswith(lc.WM3_PREFIX)]
    missing = [t for t in engine if t not in built]
    if missing:
        raise SystemExit("build_runner: closure TUs that main.vcxproj does not build (re-run link_closure.py): "
                         + ", ".join(missing[:10]))
    runner_srcs = sorted(p for p in glob.glob(os.path.join(RUNNER_DIR, "*.cpp"))
                         if os.path.basename(p) not in RUNNER_EXE_SOURCES)
    runner_tus = ["../../" + fwd(os.path.relpath(p, REPO_ROOT)) for p in runner_srcs]
    probe_tus = (["../../" + fwd(os.path.relpath(os.path.join(LOWARENA_DIR, LOWARENA_PROBE_SOURCE), REPO_ROOT))]
                 if getattr(args, "probe", False) else [])
    view = ProjectView(project, runner_tus + probe_tus)
    commands = es.build_commands(view, engine + runner_tus + probe_tus, clang, flag_args, drop_defines,
                                 drop_includes, shim, out_dir, args.error_limit, BUILD_FLAGS + list(args.extra))
    units = []
    for tu in engine:
        cmd = set_target(commands[tu], triple)
        units.append(Unit(tu, "engine", cmd[cmd.index("-c") + 1], cmd[cmd.index("-o") + 1], cmd))
    for tu in runner_tus:
        base = set_target(commands[tu], triple)
        src = base[base.index("-c") + 1]
        obj = base[base.index("-o") + 1]
        units.append(Unit(tu, "runner", src, obj, base))
        if os.path.basename(src) == "RunnerMain.cpp":
            exe_obj = obj[:-2] + ".exe.o"
            cmd = list(base)
            cmd[cmd.index("-o") + 1] = exe_obj
            cmd.append("-DFAF_RUNNER_EXECUTABLE")
            units.append(Unit(tu + "#exe", "runner-exe", src, exe_obj, cmd))
    for tu in probe_tus:
        cmd = set_target(commands[tu], triple)
        units.append(Unit(tu, "probe", cmd[cmd.index("-c") + 1], cmd[cmd.index("-o") + 1], cmd))
    for name in RUNNER_EXE_SOURCES:
        src = norm(os.path.join(RUNNER_DIR, name))
        tu = "../../" + fwd(os.path.relpath(src, REPO_ROOT))
        obj = norm(os.path.join(out_dir, "obj", "port", "engine", "runner", name + ".o"))
        cmd = [fwd(clang), f"--target={triple}", "-std=c++20", "-c", src, "-o", obj,
               f"-ferror-limit={args.error_limit}", "-fno-color-diagnostics", "-fdiagnostics-absolute-paths",
               "-I" + norm(RUNNER_DIR), "-I" + norm(ZSTD_DIR)] + LOWARENA_FLAGS + list(args.extra)
        units.append(Unit(tu, "runner-exe-src", src, obj, cmd))
    for name in ZSTD_SOURCES:
        src = norm(os.path.join(ZSTD_DIR, name))
        tu = "../../" + fwd(os.path.relpath(src, REPO_ROOT))
        obj = norm(os.path.join(out_dir, "obj", "port", "third_party", "zstd", name + ".o"))
        cmd = [fwd(clang), f"--target={triple}", "-x", "c", "-std=c11", "-c", src, "-o", obj,
               f"-ferror-limit={args.error_limit}", "-fno-color-diagnostics", "-fdiagnostics-absolute-paths",
               "-I" + norm(ZSTD_DIR)] + ZSTD_FLAGS + list(args.extra)
        units.append(Unit(tu, "zstd", src, obj, cmd))
    for kind, names in (("lowarena-exe", LOWARENA_EXE_SOURCES), ("lowarena-lib", LOWARENA_LIB_SOURCES)):
        for name in names:
            src = norm(os.path.join(LOWARENA_DIR, name))
            tu = "../../" + fwd(os.path.relpath(src, REPO_ROOT))
            obj = norm(os.path.join(out_dir, "obj", "port", "engine", "lowarena", name + ".o"))
            lang = ["-x", "c", "-std=c11"] if name.endswith(".c") else ["-std=c++20"]
            cmd = [fwd(clang), f"--target={triple}"] + lang + ["-c", src, "-o", obj,
                   f"-ferror-limit={args.error_limit}", "-fno-color-diagnostics", "-fdiagnostics-absolute-paths",
                   "-I" + norm(LOWARENA_DIR)] + LOWARENA_FLAGS + list(args.extra)
            units.append(Unit(tu, kind, src, obj, cmd))
    wm3_inc = os.path.join(out_dir, "wm3-include")
    wm3_dirs = wm3_include_dirs(project)
    for tu in wm3:
        src = norm(os.path.join(SDK_DIR, tu))
        obj = norm(os.path.join(out_dir, "obj", es.artifact_rel(tu) + ".o"))
        cmd = [fwd(clang), f"--target={triple}", "-c", src, "-o", obj, f"-ferror-limit={args.error_limit}",
               "-fno-color-diagnostics", "-fdiagnostics-absolute-paths", "-I" + norm(wm3_inc)]
        cmd += ["-I" + d for d in wm3_dirs]
        cmd += flag_args + BUILD_FLAGS + ["-w"] + list(args.extra)  # -w: Wild Magic's own warnings
        units.append(Unit(tu, "wm3", src, obj, cmd))
    for u in units:
        u.cmd = with_deps(u.cmd, u.obj)
        u.log = os.path.join(out_dir, "logs", es.artifact_rel(u.tu.split("#")[0]) +
                             (".exe" if u.kind == "runner-exe" else "") + ".log")
    return units, wm3_inc


def write_if_changed(path, text, encoding="utf-8"):
    try:
        with open(path, encoding=encoding, newline="") as f:
            if f.read() == text:
                return
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding=encoding, newline="") as f:
        f.write(text)


def write_wm3_overlay(wm3_inc):
    """The generated include directory searched first by the Wild Magic TUs (only rewritten when its
    content changes, so the incremental check sees no change)."""
    write_if_changed(os.path.join(wm3_inc, "sys", "timeb.h"), WM3_TIMEB)
    for rel, patch in WM3_OVERLAY.items():
        src = os.path.join(lc.WM3_DIR, rel)
        try:
            with open(src, encoding="latin-1", newline="") as f:
                text = f.read()
        except OSError:
            continue
        if patch:
            text, n = re.subn(patch[0], patch[1], text)
            if n == 0:
                print(f"build_runner: Wild Magic overlay: no match in {rel}", file=sys.stderr)
        write_if_changed(os.path.join(wm3_inc, os.path.basename(rel)), text, "latin-1")


# ------------------------------------------------------------------------------------------------
# Incremental compile
# ------------------------------------------------------------------------------------------------

def cmd_hash(cmd, out_dir):
    """The command with this build's directory abstracted, so another --out can reuse the result."""
    o = norm(out_dir)
    text = "\n".join(a.replace(o, "<out>") for a in cmd)
    return hashlib.sha256(f"v{STAMP_VERSION}\n{text}".encode("utf-8")).hexdigest()


def parse_depfile(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return None
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    _, _, rest = text.partition(":")
    deps, cur, i = [], [], 0
    while i < len(rest):
        ch = rest[i]
        if ch == "\\" and i + 1 < len(rest) and rest[i + 1] in " #":
            cur.append(rest[i + 1])
            i += 2
            continue
        if ch == "$" and i + 1 < len(rest) and rest[i + 1] == "$":
            cur.append("$")
            i += 2
            continue
        if ch.isspace():
            if cur:
                deps.append("".join(cur))
                cur = []
        else:
            cur.append(ch)
        i += 1
    if cur:
        deps.append("".join(cur))
    return deps


class MtimeCache:
    def __init__(self, skip_prefix):
        self.cache = {}
        self.lock = threading.Lock()
        self.skip = skip_prefix.lower()

    def newest(self, deps):
        """Newest mtime over the dependencies (None when one is gone); toolchain headers skipped."""
        newest = 0
        for d in deps:
            if norm(d).lower().startswith(self.skip):
                continue
            m = self.cache.get(d)
            if m is None:
                try:
                    m = os.stat(d).st_mtime_ns
                except OSError:
                    return None
                with self.lock:
                    self.cache[d] = m
            newest = max(newest, m)
        return newest


def stamp_path(obj):
    return obj + ".stamp.json"


def up_to_date(unit, out_dir, mtimes, base=None):
    """The stamp of `unit` in `base` (default: this build) when its command and sources are unchanged."""
    obj = unit.obj if base is None else unit.obj.replace(norm(out_dir), norm(base))
    try:
        with open(stamp_path(obj), encoding="utf-8") as f:
            stamp = json.load(f)
    except (OSError, ValueError):
        return None
    # The hash abstracts the build directory, so a stamp from `base` matches this build's command.
    if stamp.get("hash") != cmd_hash(unit.cmd, out_dir):
        return None
    if stamp.get("ok") and not os.path.isfile(obj):
        return None
    deps = parse_depfile(obj + ".d")
    if not deps:
        return None
    newest = mtimes.newest(deps)
    if newest is None or newest > stamp.get("started_ns", 0):
        return None
    return stamp


def seed_from(unit, out_dir, seed, mtimes):
    stamp = up_to_date(unit, out_dir, mtimes, base=seed)
    if stamp is None:
        return None
    src_obj = unit.obj.replace(norm(out_dir), norm(seed))
    os.makedirs(os.path.dirname(unit.obj), exist_ok=True)
    for suffix in ("", ".d", ".stamp.json"):
        if os.path.isfile(src_obj + suffix):
            shutil.copy2(src_obj + suffix, unit.obj + suffix)
    src_log = unit.log.replace(norm(out_dir), norm(seed))
    if os.path.isfile(src_log):
        os.makedirs(os.path.dirname(unit.log), exist_ok=True)
        try:
            shutil.copy2(src_log, unit.log)
        except OSError:
            # The log is informational; a file a scanner holds open (WinError 32, seen on Windows)
            # must not stop the build.
            pass
    stamp["hash"] = cmd_hash(unit.cmd, out_dir)
    stamp["seeded_from"] = norm(seed)
    with open(stamp_path(unit.obj), "w", encoding="utf-8", newline="\n") as f:
        json.dump(stamp, f)
    return stamp


def compile_unit(unit, out_dir, namer, timeout):
    os.makedirs(os.path.dirname(unit.obj), exist_ok=True)
    os.makedirs(os.path.dirname(unit.log), exist_ok=True)
    for suffix in ("", ".stamp.json"):
        try:
            os.remove(unit.obj + suffix)
        except OSError:
            pass
    started = time.time_ns()
    t0 = time.monotonic()
    try:
        p = subprocess.run(unit.cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, cwd=SDK_DIR)
        rc, raw = p.returncode, p.stdout
    except subprocess.TimeoutExpired as e:
        rc, raw = None, (e.output or b"") + b"\nbuild_runner: timeout\n"
    seconds = time.monotonic() - t0
    text = raw.decode("utf-8", "replace").replace("\r\n", "\n")
    with open(unit.log, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# build_runner: {unit.tu}\n# command: {shlex.join(unit.cmd)}\n# exit: {rc}  seconds: {seconds:.1f}\n\n")
        f.write(text)
    errors, warnings, first, _sites, _idents, _w = es.parse_output(text, namer)
    ok = rc == 0 and errors == 0 and os.path.isfile(unit.obj)
    stamp = {
        "hash": cmd_hash(unit.cmd, out_dir), "ok": ok, "exit_code": rc, "errors": errors, "warnings": warnings,
        "first_error": None if ok else first, "class": None if ok else es.classify(first, rc, rc is None),
        "seconds": round(seconds, 2), "started_ns": started, "tu": unit.tu,
    }
    # Without a dependency file (clang writes none after a fatal error such as a missing header)
    # the stamp records the result but never counts as up to date, so the TU is compiled again.
    with open(stamp_path(unit.obj), "w", encoding="utf-8", newline="\n") as f:
        json.dump(stamp, f)
    return stamp


# ------------------------------------------------------------------------------------------------
# Link
# ------------------------------------------------------------------------------------------------

def tool(clang, name):
    d = os.path.dirname(clang)
    exe = name + (".exe" if os.name == "nt" else "")
    return fwd(os.path.join(d, exe))


def write_rsp(path, items):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for it in items:
            f.write('"' + fwd(it) + '"\n')


def lib_link_flags():
    """Link flags every engine-side shared library of the runner shares (libfafengine.so, the probe)."""
    return ["-Wl,--no-undefined", "-Wl,--error-limit=0", "-Wl,--no-demangle", "-Wl,--build-id",
            "-static-libstdc++", "-lz", "-llog", "-lm", "-ldl"] + [f"-Wl,--wrap={s}" for s in LIB_WRAPS]


def link(clang, triple, out_dir, objects, exe_objs, extra_ldflags, probe_objs=None):
    lib = os.path.join(out_dir, LIB_NAME)
    exe = os.path.join(out_dir, EXE_NAME)
    probe = os.path.join(out_dir, PROBE_NAME)
    for p in (lib, exe, probe):
        try:
            os.remove(p)
        except OSError:
            pass
    rsp = os.path.join(out_dir, "link-objects.rsp")
    write_rsp(rsp, objects)
    lib_cmd = [fwd(clang), f"--target={triple}", "-shared", "-fPIC", "-o", fwd(lib), f"-Wl,-soname,{LIB_NAME}",
               "@" + fwd(rsp)] + lib_link_flags() + list(extra_ldflags)
    t0 = time.monotonic()
    p = subprocess.run(lib_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=out_dir)
    lib_out = p.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
    lib_secs = time.monotonic() - t0
    # lld stops after duplicate definitions, before it reports undefined symbols. A second link
    # that tolerates duplicates (into a scratch file, deleted again) lists the undefined ones.
    probe_out, probe_cmd = "", None
    if p.returncode != 0 and "duplicate symbol: " in lib_out:
        probe = os.path.join(out_dir, "probe-" + LIB_NAME)
        probe_cmd = [a if a != fwd(lib) else fwd(probe) for a in lib_cmd] + ["-Wl,--allow-multiple-definition"]
        q = subprocess.run(probe_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=out_dir)
        probe_out = q.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
        try:
            os.remove(probe)
        except OSError:
            pass
    exe_cmd = ([fwd(clang), f"--target={triple}", "-o", fwd(exe)] + [fwd(o) for o in exe_objs or []]
               + ["-static-libstdc++", "-lz", "-ldl", "-Wl,--build-id"]
               + [f"-Wl,--export-dynamic-symbol={s}" for s in EXE_EXPORTS])
    q = subprocess.run(exe_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=out_dir) if exe_objs else None
    exe_out = (q.stdout.decode("utf-8", "replace").replace("\r\n", "\n") if q
               else "(RunnerMain.cpp, ReplayFile.cpp, RunnerCrash.cpp, a zstd or a port/engine/lowarena source did not compile)\n")
    arena_cmd, arena_out, arena_rc = None, "", None
    if probe_objs:
        arena_cmd = ([fwd(clang), f"--target={triple}", "-shared", "-fPIC", "-o", fwd(probe),
                      f"-Wl,-soname,{PROBE_NAME}"] + [fwd(o) for o in probe_objs] + lib_link_flags())
        r = subprocess.run(arena_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=out_dir)
        arena_out, arena_rc = r.stdout.decode("utf-8", "replace").replace("\r\n", "\n"), r.returncode
    with open(os.path.join(out_dir, "link.log"), "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# {shlex.join(lib_cmd)}\n# exit {p.returncode}, {lib_secs:.1f}s\n{lib_out}\n")
        if probe_cmd:
            f.write(f"# probe for undefined symbols past the duplicates:\n# {shlex.join(probe_cmd)}\n{probe_out}\n")
        f.write(f"# {shlex.join(exe_cmd)}\n# exit {q.returncode if q else '-'}\n{exe_out}\n")
        if arena_cmd:
            f.write(f"# {shlex.join(arena_cmd)}\n# exit {arena_rc}\n{arena_out}\n")
    return {"lib_ok": p.returncode == 0 and os.path.isfile(lib), "exe_ok": bool(q) and q.returncode == 0,
            "lib_output": lib_out, "probe_output": probe_out, "exe_output": exe_out, "lib_cmd": lib_cmd,
            "exe_cmd": exe_cmd, "lib_seconds": round(lib_secs, 1),
            "arena_probe_ok": (arena_rc == 0) if arena_cmd else None, "arena_probe_output": arena_out}


LLD_ERR = re.compile(r"^(?:\S*ld\.lld\S*|\S*ld): error: (.*)$")
REF_OBJ = re.compile(r"^>>>\s+(\S.*?\.o):\((.*)\)\s*$")
DEF_AT = re.compile(r"^>>> defined at (.*)$")


def parse_lld(text):
    undefined = collections.OrderedDict()   # mangled -> set(object paths)
    duplicates = collections.OrderedDict()  # mangled -> [definition texts]
    other = []
    cur_kind, cur = None, None
    pending_def = None
    for line in text.splitlines():
        m = LLD_ERR.match(line)
        if m:
            msg = m.group(1)
            if msg.startswith("undefined symbol: "):
                cur_kind, cur = "u", msg[len("undefined symbol: "):]
                undefined.setdefault(cur, set())
            elif msg.startswith("duplicate symbol: "):
                cur_kind, cur = "d", msg[len("duplicate symbol: "):]
                duplicates.setdefault(cur, [])
            else:
                cur_kind, cur = None, None
                other.append(msg)
            continue
        s = line.strip()
        if cur_kind == "u":
            m = REF_OBJ.match(s)
            if m:
                undefined[cur].add(m.group(1))
        elif cur_kind == "d":
            m = DEF_AT.match(s)
            if m:
                pending_def = m.group(1)
                continue
            m = REF_OBJ.match(s)
            if m and pending_def is not None:
                entry = (m.group(1), pending_def)
                if entry not in duplicates[cur]:
                    duplicates[cur].append(entry)
                pending_def = None
    return undefined, duplicates, other


def demangle(clang, names):
    names = list(names)
    if not names:
        return {}
    p = subprocess.run([tool(clang, "llvm-cxxfilt")], input="\n".join(names) + "\n", capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    out = p.stdout.splitlines()
    if len(out) != len(names):
        return {n: n for n in names}
    return dict(zip(names, out))


def undefined_references(clang, objects, names):
    """{mangled: {object}} for every object that references one of `names` (lld lists only three)."""
    names = set(names)
    if not names:
        return {}
    out = collections.defaultdict(set)
    nm = tool(clang, "llvm-nm")
    for i in range(0, len(objects), 200):
        chunk = objects[i:i + 200]
        p = subprocess.run([nm, "--undefined-only", "--no-sort", "--print-file-name", "--format=just-symbols"] +
                           [fwd(o) for o in chunk], capture_output=True, text=True, encoding="utf-8", errors="replace")
        for line in p.stdout.splitlines():
            obj, sep, sym = line.rpartition(": ")
            sym = sym.strip()
            if sep and sym in names:
                out[sym].add(obj)
    return out


# ------------------------------------------------------------------------------------------------
# Report
# ------------------------------------------------------------------------------------------------

def md(s):
    return str(s).replace("|", "\\|").replace("<", "&lt;").replace(">", "&gt;")


def code(s):
    return "`" + str(s).replace("`", "'").replace("|", "\\|") + "`"


def obj_to_tu(units, out_dir):
    m = {}
    for u in units:
        m[norm(u.obj).lower()] = u.tu
        m[norm(os.path.relpath(u.obj, out_dir)).lower()] = u.tu
    return lambda o: m.get(norm(o).lower(), m.get(norm(os.path.join(out_dir, o)).lower(), o))


def write_report(out_dir, meta, units, stamps, linkres, closure, graph):
    not_built = [u for u in units if stamps[u.tu].get("class") == "not built"]
    failed = [u for u in units if not stamps[u.tu]["ok"] and stamps[u.tu].get("class") != "not built"]
    ok_count = len(units) - len(failed) - len(not_built)
    to_tu = obj_to_tu(units, out_dir)
    closure_set = set(closure)
    status = {u.tu: {"ok": stamps[u.tu]["ok"], "class": stamps[u.tu].get("class")} for u in units}
    undefined, duplicates, other = ({}, {}, [])
    if linkres:
        undefined, duplicates, other = parse_lld(linkres["lib_output"])
        if linkres.get("probe_output"):
            undefined, _dups, other2 = parse_lld(linkres["probe_output"])
            other += [m for m in other2 if m not in other]
    clang = meta["clang_path"]
    dm = demangle(clang, list(undefined) + list(duplicates))
    objects = [u.obj for u in units if u.kind in ("engine", "wm3", "runner") and stamps[u.tu]["ok"]]
    refs = undefined_references(clang, objects, undefined) if undefined else {}
    for s in undefined:
        undefined[s] |= refs.get(s, set())
    index = lc.owner_index(graph) if graph is not None and undefined else None
    groups = collections.defaultdict(list)  # owner -> [(sym, demangled, referencing tus)]
    by_ref = collections.defaultdict(set)
    for s, objs in undefined.items():
        d = dm.get(s, s)
        owner = lc.lookup_owner(index, graph, d) if index else None
        rtus = sorted({to_tu(o) for o in objs})
        groups[owner].append((s, d, rtus))
        for t in rtus:
            by_ref[t].add(s)
    comp_of = {o: lc.owner_component(o, closure_set, {**(graph.arm if graph else {}), **status}) for o in groups}
    comp_totals = collections.Counter()
    for o, lst in groups.items():
        comp_totals[comp_of[o]] += len(lst)

    L = ["# Headless runner build\n", "Generated by `scripts/port/build_runner.py`; see `port/engine/runner/README.md`.\n"]
    lib_ok = bool(linkres and linkres["lib_ok"])
    exe_ok = bool(linkres and linkres["exe_ok"])
    L.append(f"**{meta['abi']}: {ok_count}/{len(units)} compile units OK, {len(failed)} failed"
             + (f", {len(not_built)} not built in this directory" if not_built else "") + "; "
             f"{LIB_NAME} {'linked' if lib_ok else 'not linked'} ({len(undefined)} undefined, "
             f"{len(duplicates)} duplicate symbols, {len(other)} other link errors); "
             f"{EXE_NAME} {'linked' if exe_ok else 'not linked'}.**\n")
    L.append("| | |\n|---|---|")
    for k in ("date", "git", "abi", "triple", "closure", "closure_tus", "clang", "jobs", "compiled", "reused",
              "seeded", "compile_seconds", "link_seconds", "command_line"):
        if meta.get(k) not in (None, ""):
            L.append(f"| {k} | {code(meta[k]) if k in ('command_line', 'closure') else md(meta[k])} |")
    L.append("")
    if comp_totals:
        L.append("Undefined symbols by owning component (spec M3b: P platform, W wx seam, N net/session/lua, "
                 "S stubs, G gate/third-party, I integration): " +
                 ", ".join(f"{k} {v}" for k, v in sorted(comp_totals.items())) + "\n")

    L.append("## Compile failures\n")
    if failed:
        L.append("| TU | owner | class | first error |\n|---|---|---|---|")
        for u in failed:
            st = stamps[u.tu]
            fe = st.get("first_error") or {}
            owner = lc.owner_component(u.tu, closure_set, status) if u.kind == "engine" else (
                "G" if u.kind in ("wm3", "zstd") else ("L" if u.kind in ("lowarena-exe", "lowarena-lib", "probe")
                                                       else "N" if u.kind == "runner-exe-src" else "S/G"))
            L.append(f"| `{u.tu}` | {owner} | {md(st.get('class') or '')} | "
                     f"{code(str(fe.get('file', '?')) + ':' + str(fe.get('line', 0)))} "
                     f"{md((fe.get('message') or '')[:120])} |")
        L.append("")
    else:
        L.append("None.\n")

    L.append("## Undefined symbols by owner\n")
    L.append("The owner is the TU that defines the symbol on Windows (Debug|Win32 objects); `(none)` means no "
             "engine object defines it there (a CRT or Win32 name the shim lacks, or a symbol only the arm64 "
             "code paths reference). In the closure and not compiled: the port of that TU brings it. "
             "Outside the closure: a runner-only stub (S) or a guard at the referencing site.\n")
    L.append("| owner | component | in closure | compiles | symbols |\n|---|---|---|---|---:|")
    order = sorted(groups, key=lambda o: (comp_of[o], -len(groups[o]), o or ""))
    for o in order:
        inc = "yes" if o in closure_set else "no"
        st = status.get(o) or ((graph.arm.get(o) if graph else None) or {})
        comp = "yes" if st.get("ok") else ("no" if st else "?")
        L.append(f"| {code(o or '(none)')} | {comp_of[o]} | {inc} | {comp} | {len(groups[o])} |")
    L.append("")
    for o in order:
        lst = groups[o]
        L.append(f"### {code(o or '(none)')} ({comp_of[o]}, {len(lst)})\n")
        for s, d, rtus in sorted(lst, key=lambda x: x[1]):
            L.append(f"- {code(d[:180])} <- {md(', '.join(os.path.basename(t) for t in rtus)[:200])}")
        L.append("")

    L.append("## Undefined symbols by referencing TU\n")
    L.append("| TU | undefined symbols |\n|---|---:|")
    for t, ss in sorted(by_ref.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        L.append(f"| `{t}` | {len(ss)} |")
    L.append("")

    L.append("## Duplicate symbols\n")
    if duplicates:
        for s, defs in duplicates.items():
            L.append(f"- {code(dm.get(s, s)[:160])}")
            for obj, where in defs:
                L.append(f"  - `{to_tu(obj)}`: {md(where)}")
        L.append("")
    else:
        L.append("None.\n")
    if other:
        L.append("## Other link errors\n")
        for msg in other[:100]:
            L.append(f"- {md(msg[:300])}")
        L.append("")
    if linkres and not exe_ok:
        L.append("## faf_headless_runner\n")
        L.append("```\n" + linkres["exe_output"][-3000:] + "\n```\n")

    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))
    results = {
        "meta": meta,
        "compile_units": len(units),
        "compile_ok": ok_count,
        "not_built": [u.tu for u in not_built],
        "compile_failures": [{"tu": u.tu, "class": stamps[u.tu].get("class"),
                              "first_error": stamps[u.tu].get("first_error")} for u in failed],
        "lib_linked": lib_ok,
        "exe_linked": exe_ok,
        "arena_probe_linked": linkres.get("arena_probe_ok") if linkres else None,
        "undefined": len(undefined),
        "duplicates": len(duplicates),
        "other_link_errors": other,
        "undefined_by_component": dict(comp_totals),
        "undefined_by_owner": {(o or "(none)"): {"component": comp_of[o], "in_closure": o in closure_set,
                                                 "symbols": [{"symbol": s, "demangled": d, "referenced_by": r}
                                                             for s, d, r in lst]}
                               for o, lst in groups.items()},
        "duplicate_symbols": [{"symbol": s, "demangled": dm.get(s, s),
                               "definitions": [{"tu": to_tu(o), "at": w} for o, w in defs]}
                              for s, defs in duplicates.items()],
        "units": {u.tu: {k: stamps[u.tu].get(k) for k in ("ok", "errors", "warnings", "class", "seconds")}
                  for u in units},
    }
    with open(os.path.join(out_dir, "results.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(results, f, indent=1)
    return results


# ------------------------------------------------------------------------------------------------
# main
# ------------------------------------------------------------------------------------------------

def select(units, patterns):
    keys = {u.tu: {u.tu.lower(), es.artifact_rel(u.tu.split("#")[0]).lower()} for u in units}
    chosen, missing = set(), []
    for pat in patterns:
        p = re.sub(r"^\./", "", fwd(pat).strip())
        forms = {p.lower(), re.sub(r"^(?:.*/)?src/sdk/", "", p, flags=re.I).lower()}
        hits = {u.tu for u in units if any(fnmatch.fnmatchcase(k, f) or k.startswith(f.rstrip("/") + "/")
                                           for k in keys[u.tu] for f in forms)}
        hits |= {u.tu for u in units if u.tu.split("#")[0] in hits}
        if not hits:
            missing.append(pat)
        chosen |= hits
    return chosen, missing


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--abi", choices=sorted(ABIS), default="arm64-v8a")
    ap.add_argument("--jobs", "-j", type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument("--out", help="build directory (default buildstage/runner/<abi>)")
    ap.add_argument("--seed", help="reuse up-to-date objects from this build directory (default: the default "
                                   "--out of the ABI, when --out is another directory)")
    ap.add_argument("--closure", default=lc.DEFAULT_CLOSURE, help="closure file (default port/engine/runner/closure.txt)")
    ap.add_argument("--files", nargs="+", metavar="GLOB",
                    help="recompile only these TUs (closure TUs, runner sources, WildMagic TUs; paths or globs "
                         "relative to src/sdk), then link with the objects already there")
    ap.add_argument("--force", action="store_true", help="recompile even when up to date")
    ap.add_argument("--link-only", action="store_true", help="do not compile, link what is there")
    ap.add_argument("--no-link", action="store_true", help="compile only")
    ap.add_argument("--no-owners", action="store_true", help="do not map undefined symbols to their Windows owner")
    ap.add_argument("--extra", action="append", default=[], metavar="ARG", help="extra compile argument")
    ap.add_argument("--ldflag", action="append", default=[], metavar="ARG", help="extra link argument for the .so")
    ap.add_argument("--probe", action="store_true",
                    help=f"also build {PROBE_NAME}, the low arena's placement probe (port/engine/lowarena/probe)")
    ap.add_argument("--error-limit", type=int, default=50, help="clang -ferror-limit per TU (default 50)")
    ap.add_argument("--timeout", type=int, default=900, help="seconds per TU")
    ap.add_argument("--ndk", help="Android NDK directory")
    args = ap.parse_args()

    triple = ABIS[args.abi]
    default_out = os.path.join(REPO_ROOT, "buildstage", "runner", args.abi)
    out_dir = os.path.abspath(args.out or default_out)
    seed = args.seed
    if seed is None and norm(out_dir).lower() != norm(default_out).lower() and os.path.isdir(default_out):
        seed = default_out
    seed = os.path.abspath(seed) if seed else None
    clang, ndk = es.find_clang(args.ndk, None)
    os.makedirs(out_dir, exist_ok=True)

    closure = lc.read_closure_file(args.closure)
    project = es.Project(lc.VCXPROJ, "Release|x64")
    units, wm3_inc = build_units(args, project, closure, clang, triple, out_dir)
    write_wm3_overlay(wm3_inc)

    selected = None
    if args.files:
        selected, missing = select(units, args.files)
        if missing:
            print("build_runner: --files matches nothing in the closure or runner: " + ", ".join(missing),
                  file=sys.stderr)
            return 2

    namer = es.PathNamer(SDK_DIR, REPO_ROOT, ndk, clang)
    toolchain_dir = norm(os.path.dirname(os.path.dirname(clang)))
    mtimes = MtimeCache(toolchain_dir)
    stamps, todo = {}, []
    reused = seeded = 0
    for u in units:
        st = None
        if not args.force and not (selected is not None and u.tu in selected):
            st = up_to_date(u, out_dir, mtimes)
            if st is not None:
                reused += 1
            elif seed and selected is None and not args.link_only:
                st = seed_from(u, out_dir, seed, mtimes)
                if st is not None:
                    seeded += 1
        if st is None and (args.link_only or (selected is not None and u.tu not in selected)):
            # Not compiled now: whatever this directory has (possibly stale), or nothing.
            try:
                with open(stamp_path(u.obj), encoding="utf-8") as f:
                    st = json.load(f)
                st["stale"] = True
            except (OSError, ValueError):
                st = {"ok": False, "class": "not built", "first_error": None, "stale": True}
        if st is None:
            todo.append(u)
        else:
            stamps[u.tu] = st

    print(f"build_runner: {args.abi}, {len(units)} compile units: {len(todo)} to compile, {reused} up to date"
          + (f", {seeded} from {norm(seed)}" if seeded else "") + f", {args.jobs} jobs", file=sys.stderr)
    t0 = time.monotonic()
    if todo:
        # Longest first: the last measured time, else the source size.
        def cost(u):
            try:
                with open(stamp_path(u.obj), encoding="utf-8") as f:
                    return (1, json.load(f).get("seconds", 0))
            except (OSError, ValueError):
                pass
            try:
                return (0, os.path.getsize(u.src))
            except OSError:
                return (0, 0)
        todo.sort(key=cost, reverse=True)
        done = [0, 0]
        lock = threading.Lock()
        step = max(10, len(todo) // 20)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futs = {pool.submit(compile_unit, u, out_dir, namer, args.timeout): u for u in todo}
            for fut in concurrent.futures.as_completed(futs):
                u = futs[fut]
                st = fut.result()
                with lock:
                    stamps[u.tu] = st
                    done[0] += 1
                    done[1] += bool(st["ok"])
                    if done[0] % step == 0 or done[0] == len(todo):
                        print(f"  [{done[0]:4d}/{len(todo)}] ok {done[1]:4d}  {time.monotonic() - t0:6.0f}s",
                              file=sys.stderr)
    compile_secs = time.monotonic() - t0

    linkres = None
    if not args.no_link:
        def built(kinds):
            return [u for u in units if u.kind in kinds]

        def all_ok(us):
            return bool(us) and all(stamps[u.tu].get("ok") and os.path.isfile(u.obj) for u in us)

        objects = [u.obj for u in built(("engine", "wm3", "runner", "lowarena-lib")) if stamps[u.tu].get("ok")
                   and os.path.isfile(u.obj)]
        exe_units = built(("runner-exe", "lowarena-exe", "runner-exe-src", "zstd"))
        exe_objs = [u.obj for u in exe_units] if all_ok(exe_units) else None
        probe_units = built(("probe", "lowarena-lib"))
        probe_objs = [u.obj for u in probe_units] if args.probe and all_ok(probe_units) else None
        linkres = link(clang, triple, out_dir, objects, exe_objs, args.ldflag, probe_objs)

    graph = None
    if linkres and not args.no_owners:
        try:
            graph = lc.load_graph()
        except (SystemExit, OSError, ValueError) as e:
            print(f"build_runner: no owner mapping ({e})", file=sys.stderr)
    dirty = es.git("status", "--porcelain", "--", "src/sdk", "port/engine", "scripts/port")
    meta = {
        "date": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "git": (es.git("rev-parse", "--short", "HEAD") or "?") + (f" + {len(dirty.splitlines())} changed files" if dirty else ""),
        "abi": args.abi, "triple": triple, "closure": norm(os.path.relpath(args.closure, REPO_ROOT)),
        "closure_tus": len(closure), "clang": es.clang_version(clang), "clang_path": fwd(clang), "jobs": args.jobs,
        "compiled": len(todo), "reused": reused, "seeded": seeded, "compile_seconds": round(compile_secs, 1),
        "link_seconds": linkres["lib_seconds"] if linkres else None,
        "command_line": shlex.join(["python", "scripts/port/build_runner.py"] + sys.argv[1:]),
    }
    res = write_report(out_dir, meta, units, stamps, linkres, closure, graph)
    failed = len(res["compile_failures"]) + len(res["not_built"])
    print(f"build_runner: {res['compile_ok']}/{len(units)} compile units OK ({len(res['compile_failures'])} failed"
          + (f", {len(res['not_built'])} not built" if res["not_built"] else "") + f", {compile_secs:.0f}s); "
          + (f"{LIB_NAME} {'linked' if res['lib_linked'] else 'NOT linked'}: {res['undefined']} undefined, "
             f"{res['duplicates']} duplicate, {len(res['other_link_errors'])} other errors; "
             f"{EXE_NAME} {'linked' if res['exe_linked'] else 'NOT linked'}"
             + ("" if res.get("arena_probe_linked") is None
                else f"; {PROBE_NAME} {'linked' if res['arena_probe_linked'] else 'NOT linked'}")
             if linkres else "not linked"),
          file=sys.stderr)
    if res["undefined_by_component"]:
        print("  undefined by component: " + ", ".join(f"{k} {v}" for k, v in sorted(res["undefined_by_component"].items())),
              file=sys.stderr)
    print(f"  report: {norm(os.path.join(out_dir, 'report.md'))}", file=sys.stderr)
    probe_ok = not args.probe or args.no_link or bool(res.get("arena_probe_linked"))
    return 0 if (failed == 0 and res["lib_linked"] and res["exe_linked"] and probe_ok) else 1


if __name__ == "__main__":
    sys.exit(main())
