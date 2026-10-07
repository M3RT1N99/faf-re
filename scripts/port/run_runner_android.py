#!/usr/bin/env python3
"""Run the Android headless replay runner on a device or emulator over adb (M3c).

One command plays one or more replays with faf_headless_runner on an attached device and brings
everything back to the host:

  python scripts/port/run_runner_android.py --replay buildstage/replays-m3/T1-26675870.scfareplay
  python scripts/port/run_runner_android.py --abi arm64-v8a --replay T1.scfareplay T2.scfareplay
  python scripts/port/run_runner_android.py --serial <phone> --replay T1.scfareplay --dry-run
  python scripts/port/run_runner_android.py --replay T1.scfareplay -- /headlessprogress 100

From Git Bash, prefix MSYS_NO_PATHCONV=1, or extra runner options such as `/headlessprogress` are
rewritten into Windows paths before Python sees them (the script stops when it sees one).

What it does, in order:

 1. Build: nothing, unless --build is given; then `build_runner.py --abi <abi> --out <build dir>`.
 2. Binaries: copies of faf_headless_runner and libfafengine.so from the build directory, stripped
    of debug info (buildstage/runner-runs/_push/<abi>/), are pushed to <device dir>/<abi>/ when
    their sha256 differs from what is there. The unstripped files in the build directory are the
    symbols; a crash is symbolised against them right away, and the .so is kept by build id under
    the run's symbols/ directory so the crash can be symbolised again later.
 3. Game data (skipped with --skip-data): the files the Windows runner mounts through
    `/init init_faf.lua`, pushed into the app's M1 layout (port/data/gamedata.json) under
    --data-root, verified by sha256 and recorded in <data root>/.fafre-runner-data.json:
      - faf/bin/init_faf.lua and every faf/gamedata/*.nx2 that init_faf.lua's allowedAssetsNxy
        allows, from the FAF client (--faf-path, default C:\\ProgramData\\FAForever);
      - every scfa/gamedata/*.scd that its allowedAssetsScd allows, from the SCFA install
        (--scfa-path, default fa_path from the FAF client's fa_path.lua);
      - the replays' maps and the lobby's current map (vault first, then SCFA, as init_faf.lua
        mounts them);
      - /preferences/Game.prefs with only the PreGameData block of the host's Game.prefs, and the
        vault mods its IconReplacements name: FAF's Blueprints.lua reads both while it loads the
        blueprints (LoadPreGameData, FindCustomStrategicIcons, CurrentMapDir), in the Windows
        runner as in the game, so they are part of what the sim starts from;
      - faf/fa_path.lua for this root, and empty scfa/{movies,sounds,fonts} and vault/{maps,mods}
        directories, which init_faf.lua mounts and the runner never reads.
    Everything else in the user's vault is left out (only mods a replay activates reach the sim;
    use --vault-mod NAME for those). Nothing on the device is deleted unless --prune is given.
 4. Replays: pushed to <device dir>/replays/ (lower-case names).
 5. Run: `faf_headless_runner /headlessreplay <replay> /init <data root>/faf/bin/init_faf.lua
    /log ... /headlesssummary ... [extra args]` in <device dir>/<abi>/ with FAF_LOWARENA=<0|1>,
    FAF_ENGINE_LIB=<the pushed .so> and FAF_KNOWN_FOLDERS=<device dir>/home (the Windows known
    folders: LOCAL_APPDATA is home/AppData/Local), plus any --env NAME=VALUE. Output is streamed
    to the console.
 6. Pull: the summary JSON, the engine log, the registry dump (--registry-dump) and the logcat
    since the start into buildstage/runner-runs/<serial>/<abi>/<tag>-<name>.*; stdout is the
    .out file. <tag> defaults to la1 or la0 (--lowarena).
 7. Crash (killed by a signal): the new tombstone (when the shell user can read
    /data/tombstones) or the logcat crash buffer, symbolised with the NDK's llvm-symbolizer
    (.crash.txt, .crash.sym.txt). On the host-side timeout, `debuggerd -b` stacks (.hang.txt),
    then the process is killed.
 8. Compare (--windows-ref FILE|DIR): exit, end, game-over beat, checkpoint chain, the first
    diverging checkpoint (beat 0 is the blueprint checksum), Lua error counts and the registry
    block against a Windows summary JSON (.compare.json).

Device layout (--device-dir, default /data/local/tmp/fafre-runner):
  <abi>/          faf_headless_runner, libfafengine.so
  data/           the game data root (--data-root)
  home/           FAF_KNOWN_FOLDERS
  replays/        the pushed replays
  runs/<abi>/     the runner's output files (pulled after each run)

--dry-run prints every adb command it would run, with the exact runner command line; it queries a
device that is attached (read-only) and otherwise assumes an empty one, so the plan for a phone
can be checked before it is plugged in.

Exit code: 0 when every replay played to its end (runner exit 0), 1 when a run failed, 2 for
usage or setup errors.
"""

import argparse
import datetime
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

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, "..", ".."))
sys.path.insert(0, SCRIPT_DIR)

ABIS = ("arm64-v8a", "x86_64")
LIB_NAME = "libfafengine.so"
EXE_NAME = "faf_headless_runner"
DEFAULT_DEVICE_DIR = "/data/local/tmp/fafre-runner"
DATA_MANIFEST = ".fafre-runner-data.json"
GAMEDATA_JSON = os.path.join(REPO_ROOT, "port", "data", "gamedata.json")
BUILD_RUNNER = os.path.join(SCRIPT_DIR, "build_runner.py")
DEFAULT_OUT = os.path.join(REPO_ROOT, "buildstage", "runner-runs")
DEFAULT_FAF_PATH = r"C:\ProgramData\FAForever"
PREFS_DIR = "Gas Powered Games/Supreme Commander Forged Alliance"  # under LOCAL_APPDATA, as init_faf.lua
FREE_SPACE_MARGIN = 512 * 1024 * 1024
DATA_SCHEMA = 1

EXIT_MEANING = {
    0: "the replay played to its end",
    1: "bad arguments or setup",
    2: "the scenario failed to load",
    3: "the sim failed or the engine died",
    4: "no progress within the timeout",
}
SIGNALS = {1: "SIGHUP", 2: "SIGINT", 4: "SIGILL", 5: "SIGTRAP", 6: "SIGABRT", 7: "SIGBUS", 8: "SIGFPE",
           9: "SIGKILL", 11: "SIGSEGV", 13: "SIGPIPE", 15: "SIGTERM", 31: "SIGSYS"}
PID_MARK = "__FAFRE_PID__"
EXIT_MARK = "__FAFRE_EXIT__"


class Failure(Exception):
    """A setup or device error that ends the script with exit code 2."""


def say(text=""):
    print(text, flush=True)


def fmt_cmd(argv):
    return subprocess.list2cmdline(argv) if os.name == "nt" else shlex.join(argv)


def q(path):
    """Quote one word for the device shell (mksh)."""
    return shlex.quote(path)


def human(size):
    size = float(size)
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.0f} {unit}" if unit == "B" else f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} GB"


def safe_name(text):
    return re.sub(r"[^A-Za-z0-9._-]", "_", text)


def now_utc():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def read_text(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def git_head():
    try:
        out = subprocess.run(["git", "-C", REPO_ROOT, "rev-parse", "--short", "HEAD"], stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, timeout=30).stdout.decode().strip()
        dirty = subprocess.run(["git", "-C", REPO_ROOT, "status", "--porcelain", "--untracked-files=no"],
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=60).stdout.strip()
        return out + ("+dirty" if dirty else "")
    except (OSError, subprocess.SubprocessError):
        return "?"


# --------------------------------------------------------------------------------------------------
# Host tools


def find_adb(arg):
    candidates = [arg, os.environ.get("ADB")]
    for sdk in (os.environ.get("ANDROID_SDK_ROOT"), os.environ.get("ANDROID_HOME"), r"C:\Android\sdk"):
        if sdk:
            candidates.append(os.path.join(sdk, "platform-tools", "adb.exe" if os.name == "nt" else "adb"))
    candidates.append(shutil.which("adb"))
    for candidate in filter(None, candidates):
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)
    return None


def find_ndk_tools(ndk_arg):
    import engine_sweep as es  # the same NDK lookup as build_runner.py
    clang, _ = es.find_clang(ndk_arg, None)
    bindir = os.path.dirname(clang)
    ext = ".exe" if os.name == "nt" else ""
    tools = {}
    for name in ("llvm-strip", "llvm-symbolizer", "llvm-readelf"):
        path = os.path.join(bindir, name + ext)
        if not os.path.isfile(path):
            raise Failure(f"{path} not found")
        tools[name] = path
    return tools


def build_id(readelf, path):
    try:
        out = subprocess.run([readelf, "-n", path], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=120).stdout.decode("utf-8", "replace")
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r"Build ID:\s*([0-9a-fA-F]+)", out)
    return m.group(1).lower() if m else None


class HostHashes:
    """sha256 of host files, cached by path, size and mtime (the game data is 4 GB)."""

    def __init__(self, path):
        self.path = path
        self.dirty = False
        try:
            with open(path, "r", encoding="utf-8") as f:
                self.cache = json.load(f)
        except (OSError, ValueError):
            self.cache = {}

    def get(self, path):
        st = os.stat(path)
        key = os.path.normcase(os.path.abspath(path))
        hit = self.cache.get(key)
        if hit and hit["size"] == st.st_size and hit["mtime_ns"] == st.st_mtime_ns:
            return hit["sha256"]
        digest = hashlib.sha256()
        with open(path, "rb") as f:
            for block in iter(lambda: f.read(8 << 20), b""):
                digest.update(block)
        sha = digest.hexdigest()
        self.cache[key] = {"size": st.st_size, "mtime_ns": st.st_mtime_ns, "sha256": sha}
        self.dirty = True
        return sha

    def save(self):
        if self.dirty:
            write_file(self.path, json.dumps(self.cache, indent=0, sort_keys=True))
            self.dirty = False


# --------------------------------------------------------------------------------------------------
# adb


class Adb:
    """adb for one device. query() runs even in a dry run (read-only, only when the device is
    attached); do() prints the command in a dry run and runs it otherwise."""

    def __init__(self, exe, serial, dry_run, attached):
        self.exe = exe or "adb"
        self.serial = serial
        self.dry_run = dry_run
        self.attached = attached

    def argv(self, *args):
        return [self.exe, "-s", self.serial, *args]

    def query(self, *args, timeout=300):
        if not self.attached:
            return None, ""
        try:
            p = subprocess.run(self.argv(*args), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=timeout)
        except subprocess.TimeoutExpired:
            return None, ""
        return p.returncode, p.stdout.decode("utf-8", "replace").replace("\r\n", "\n")

    def sh_query(self, script, timeout=300):
        return self.query("shell", script, timeout=timeout)

    def do(self, *args, timeout=None, quiet=False):
        cmd = self.argv(*args)
        if self.dry_run:
            say("  [dry-run] " + fmt_cmd(cmd))
            return ""
        p = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
        out = p.stdout.decode("utf-8", "replace").replace("\r\n", "\n")
        if p.returncode != 0:
            raise Failure(f"{fmt_cmd(cmd)} failed ({p.returncode}): {out.strip()[-800:]}")
        if not quiet and out.strip():
            for line in out.strip().splitlines()[-3:]:
                say("    " + line.strip())
        return out

    def sh(self, script, timeout=None, quiet=True):
        return self.do("shell", script, timeout=timeout, quiet=quiet)

    def push(self, sources, dest, timeout=None):
        return self.do("push", *sources, dest, timeout=timeout, quiet=False)


def list_devices(adb_exe):
    if not adb_exe:
        return {}
    try:
        out = subprocess.run([adb_exe, "devices"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=60).stdout.decode("utf-8", "replace")
    except (OSError, subprocess.SubprocessError):
        return {}
    devices = {}
    for line in out.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2:
            devices[parts[0]] = parts[1]
    return devices


DEVICE_PROPS = ("ro.product.cpu.abilist", "ro.build.version.sdk", "ro.build.version.release",
                "ro.product.manufacturer", "ro.product.model", "ro.soc.model", "ro.hardware",
                "ro.build.fingerprint", "ro.dalvik.vm.native.bridge", "ro.kernel.qemu")


def device_info(adb):
    script = "; ".join(f"echo \"{p}=$(getprop {p})\"" for p in DEVICE_PROPS)
    script += "; echo \"kernel=$(uname -r)\"; echo \"pagesize=$(getconf PAGESIZE 2>/dev/null)\""
    script += "; echo \"memtotal_kb=$(sed -n 's/^MemTotal: *\\([0-9]*\\).*/\\1/p' /proc/meminfo)\""
    rc, out = adb.sh_query(script)
    info = {}
    for line in out.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            info[key.strip()] = value.strip()
    return info


def free_bytes(adb, path):
    """Free space of the file system that holds `path` (or its nearest existing parent)."""
    probe = path
    script = f"p={q(probe)}; while [ ! -d \"$p\" ] && [ \"$p\" != / ]; do p=${{p%/*}}; [ -z \"$p\" ] && p=/; done; df -k \"$p\" | tail -n 1"
    rc, out = adb.sh_query(script)
    fields = out.split()
    if rc != 0 or len(fields) < 4 or not fields[3].isdigit():
        return None
    return int(fields[3]) * 1024


# --------------------------------------------------------------------------------------------------
# Game data


def parse_lua_strings(text):
    """name = "value" assignments of a small Lua file such as fa_path.lua."""
    return {m.group(1): m.group(2) for m in re.finditer(r"^\s*(\w+)\s*=\s*\"([^\"]*)\"", text, re.M)}


def allowed_assets(init_text, table):
    """The lower-case names init_faf.lua's `table["name"] = true` lines allow (comments ignored)."""
    allowed = {}
    for line in init_text.splitlines():
        code = line.split("--", 1)[0]
        m = re.match(r"\s*%s\[\"([^\"]+)\"\]\s*=\s*(true|false)" % re.escape(table), code)
        if m:
            allowed[m.group(1).lower()] = m.group(2) == "true"
    return {name for name, ok in allowed.items() if ok}


def lua_block(text, name):
    """The text of the top-level `name = { ... }` assignment, braces matched outside strings."""
    m = re.search(r"^%s\s*=\s*\{" % re.escape(name), text, re.M)
    if not m:
        return None
    i = m.end() - 1
    depth = 0
    quote = None
    while i < len(text):
        c = text[i]
        if quote:
            if c == "\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in "'\"":
            quote = c
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[m.start():i + 1]
        i += 1
    return None


def find_child(directory, name):
    """The entry of `directory` whose name equals `name` ignoring case (NTFS semantics)."""
    try:
        for entry in os.listdir(directory):
            if entry.lower() == name.lower():
                return os.path.join(directory, entry)
    except OSError:
        pass
    return None


def replay_map_dir(replay_path):
    """The map directory name (/maps/<dir>/...) from the replay header, or the sidecar JSON."""
    sidecar = replay_path + ".json"
    if os.path.isfile(sidecar):
        try:
            with open(sidecar, "r", encoding="utf-8") as f:
                m = re.match(r"/maps/([^/]+)/", json.load(f).get("map", ""), re.I)
            if m:
                return m.group(1)
        except (OSError, ValueError):
            pass
    with open(replay_path, "rb") as f:
        head = f.read(1 << 16)
    m = re.search(rb"/maps/([^/\x00]+)/[^\x00]*?\.scmap", head, re.I)
    return m.group(1).decode("utf-8", "replace") if m else None


class DataFile:
    __slots__ = ("src", "rel", "size", "group", "sha256")

    def __init__(self, src, rel, group=None):
        self.src = src
        self.rel = rel
        self.size = os.path.getsize(src)
        self.group = group
        self.sha256 = None


class DataPlan:
    def __init__(self):
        self.files = []          # DataFile
        self.groups = {}         # rel dir -> host dir, pushed as a whole
        self.generated = {}      # absolute device path -> bytes
        self.empty_dirs = []     # rel dirs
        self.notes = []
        self.sources = {}
        self.pinned = {}         # port/data/gamedata.json's FAF entries by dest

    def add_file(self, src, rel):
        self.files.append(DataFile(src, rel))

    def add_dir(self, src_dir, rel_dir):
        if rel_dir in self.groups:
            return
        self.groups[rel_dir] = src_dir
        for base, _, names in os.walk(src_dir):
            for name in sorted(names):
                src = os.path.join(base, name)
                rel = rel_dir + "/" + os.path.relpath(src, src_dir).replace(os.sep, "/")
                self.files.append(DataFile(src, rel, group=rel_dir))

    def total(self):
        return sum(f.size for f in self.files)


def build_data_plan(args, replays, data_root, device_dir):
    plan = DataPlan()
    faf = args.faf_path
    init = os.path.join(faf, "bin", "init_faf.lua")
    fa_path_file = os.path.join(faf, "fa_path.lua")
    if not os.path.isfile(init):
        raise Failure(f"{init} not found (--faf-path)")
    fa_path_vars = parse_lua_strings(read_text(fa_path_file)) if os.path.isfile(fa_path_file) else {}
    scfa = args.scfa_path or fa_path_vars.get("fa_path")
    vault = args.vault_path or fa_path_vars.get("custom_vault_path")
    if not scfa or not os.path.isdir(os.path.join(scfa, "gamedata")):
        raise Failure(f"SCFA install not found ({scfa!r}); pass --scfa-path")
    plan.sources = {"faf": os.path.abspath(faf), "scfa": os.path.abspath(scfa),
                    "vault": os.path.abspath(vault) if vault else None}
    init_text = read_text(init)
    nxy = allowed_assets(init_text, "allowedAssetsNxy")
    scd = allowed_assets(init_text, "allowedAssetsScd")
    if not nxy or not scd:
        raise Failure(f"{init}: could not read allowedAssetsNxy/allowedAssetsScd")

    plan.add_file(init, "faf/bin/init_faf.lua")
    for name in sorted(os.listdir(os.path.join(faf, "gamedata")), key=str.lower):
        if name.lower().endswith(".nx2") and name.lower() in nxy:
            plan.add_file(os.path.join(faf, "gamedata", name), "faf/gamedata/" + name)
    for name in sorted(os.listdir(os.path.join(scfa, "gamedata")), key=str.lower):
        if name.lower().endswith(".scd") and name.lower() in scd:
            plan.add_file(os.path.join(scfa, "gamedata", name), "scfa/gamedata/" + name)

    # FAF files against the pinned manifest: a different FAF version is not an error (the Windows
    # runner reads the same install), but it is worth knowing.
    try:
        with open(GAMEDATA_JSON, "r", encoding="utf-8") as f:
            manifest = json.load(f)["faf"]
        pinned = {e["dest"]: e for e in manifest["files"]}
        pinned_version = manifest.get("version")
    except (OSError, ValueError, KeyError):
        pinned, pinned_version = {}, None
    plan.pinned = pinned
    for f in plan.files:
        entry = pinned.get(f.rel)
        if entry and entry.get("size") != f.size:
            plan.notes.append(f"{f.rel}: {f.size} bytes, port/data/gamedata.json pins {entry.get('size')} "
                              f"(FAF {pinned_version})")

    # The lobby's pre-game data, which Blueprints.lua reads from /preferences/Game.prefs.
    prefs_text = None
    prefs_path = args.host_prefs
    if prefs_path is None:
        local = os.environ.get("LOCALAPPDATA")
        prefs_path = os.path.join(local, *PREFS_DIR.split("/"), "Game.prefs") if local else None
    icon_mods = []
    current_map = None
    if prefs_path and prefs_path.lower() != "none":
        if os.path.isfile(prefs_path):
            block = lua_block(read_text(prefs_path), "PreGameData")
            if block:
                prefs_text = ("-- Written by scripts/port/run_runner_android.py: the PreGameData block of\n"
                              "-- the host's Game.prefs, the only part FAF's Blueprints.lua reads.\n" + block + "\n")
                icon_mods = re.findall(r"Location\s*=\s*['\"]/mods/([^'\"]+)['\"]", block)
                m = re.search(r"CurrentMapDir\s*=\s*['\"]/maps/([^'\"/]+)['\"]", block)
                current_map = m.group(1) if m else None
            else:
                plan.notes.append(f"{prefs_path} has no PreGameData block")
        else:
            plan.notes.append(f"host prefs {prefs_path} not found: no Game.prefs on the device")
    prefs_dev = f"{device_dir}/home/AppData/Local/{PREFS_DIR}/Game.prefs"
    if prefs_text is not None:
        plan.generated[prefs_dev] = prefs_text.encode("utf-8")
    plan.sources["prefs"] = prefs_path

    # Maps: the replays' and the lobby's current map. init_faf.lua mounts the vault's maps before
    # the install's, and the first mount wins.
    maps = []
    for replay in replays:
        name = replay_map_dir(replay)
        if not name:
            raise Failure(f"{replay}: no /maps/<dir>/... path in the header")
        maps.append((name, os.path.basename(replay)))
    if current_map:
        maps.append((current_map, "Game.prefs PreGameData.CurrentMapDir"))
    for name, why in maps:
        found = None
        if vault:
            found = find_child(os.path.join(vault, "maps"), name)
            if found:
                plan.add_dir(found, "vault/maps/" + os.path.basename(found))
        if not found:
            found = find_child(os.path.join(scfa, "maps"), name)
            if found:
                plan.add_dir(found, "scfa/maps/" + os.path.basename(found))
        if not found:
            raise Failure(f"map {name} ({why}) is neither in the vault nor in {scfa}/maps")

    for name in icon_mods + list(args.vault_mod):
        found = find_child(os.path.join(vault, "mods"), name) if vault else None
        if not found:
            raise Failure(f"vault mod {name!r} not found in {vault}/mods")
        plan.add_dir(found, "vault/mods/" + os.path.basename(found))

    fa_path_lua = (
        "-- Written by scripts/port/run_runner_android.py for the headless runner's data root.\n"
        f"fa_path = \"{data_root}/scfa\"\n"
        f"custom_vault_path = \"{data_root}/vault\"\n"
        f"GameType = \"{fa_path_vars.get('GameType', 'faf')}\"\n"
        f"GameVersion = \"{fa_path_vars.get('GameVersion', '')}\"\n"
        f"ClientVersion = \"{fa_path_vars.get('ClientVersion', '')}\"\n"
        "ForceAffinity = false\n"
    )
    plan.generated[f"{data_root}/faf/fa_path.lua"] = fa_path_lua.encode("utf-8")
    plan.empty_dirs = ["scfa/maps", "scfa/movies", "scfa/sounds", "scfa/fonts", "vault/maps", "vault/mods"]
    return plan


def device_files(adb, root):
    rc, out = adb.sh_query(f"[ -d {q(root)} ] && find {q(root)} -type f -exec stat -c '%s %n' {{}} + 2>/dev/null; true")
    files = {}
    for line in out.splitlines():
        size, _, path = line.partition(" ")
        if size.isdigit() and path:
            files[path] = int(size)
    return files


def device_sha256(adb, paths=(), dirs=()):
    """sha256 of device files: explicit paths in chunks, directories with find."""
    result = {}
    scripts = []
    chunk = []
    for path in paths:
        chunk.append(q(path))
        if sum(len(c) + 1 for c in chunk) > 6000:
            scripts.append("sha256sum " + " ".join(chunk))
            chunk = []
    if chunk:
        scripts.append("sha256sum " + " ".join(chunk))
    for d in dirs:
        scripts.append(f"find {q(d)} -type f -exec sha256sum {{}} +")
    for script in scripts:
        rc, out = adb.sh_query(script + " 2>/dev/null; true", timeout=3600)
        for line in out.splitlines():
            m = re.match(r"([0-9a-f]{64})\s+(.+)$", line.strip())
            if m:
                result[m.group(2)] = m.group(1)
    return result


def sync_data(args, adb, plan, data_root, out_dir, hashes):
    say(f"== Game data -> {adb.serial}:{data_root}")
    by_kind = {}
    for f in plan.files:
        kind = f.rel.split("/")[0] + "/" + (f.rel.split("/")[1] if "/" in f.rel else "")
        by_kind.setdefault(kind, [0, 0])
        by_kind[kind][0] += 1
        by_kind[kind][1] += f.size
    for kind, (count, size) in sorted(by_kind.items()):
        say(f"   {kind:<16} {count:5d} files  {human(size):>9}")
    say(f"   {'total':<16} {len(plan.files):5d} files  {human(plan.total()):>9}")
    for note in plan.notes:
        say(f"   note: {note}")

    if adb.attached or not adb.dry_run:
        say("   hashing host files (cached in buildstage/runner-runs/_cache) ...")
        for f in plan.files:
            f.sha256 = hashes.get(f.src)
            entry = plan.pinned.get(f.rel)
            if entry and entry.get("sha256") and entry.get("size") == f.size and entry["sha256"] != f.sha256:
                say(f"   note: {f.rel}: sha256 differs from port/data/gamedata.json")
        hashes.save()  # a host-side cache, also in a dry run

    present = device_files(adb, data_root)
    rc, manifest_text = adb.sh_query(f"cat {q(data_root + '/' + DATA_MANIFEST)} 2>/dev/null")
    try:
        manifest = json.loads(manifest_text).get("files", {}) if manifest_text.strip() else {}
    except ValueError:
        manifest = {}

    ok, verify, push = [], [], []
    for f in plan.files:
        dev_path = f"{data_root}/{f.rel}"
        entry = manifest.get(f.rel)
        if present.get(dev_path) != f.size:
            push.append(f)
        elif entry and entry.get("sha256") == f.sha256 and entry.get("size") == f.size and not args.verify_data:
            ok.append(f)
        else:
            verify.append(f)
    if verify:
        say(f"   verifying {len(verify)} files on the device by sha256 ...")
        singles = [f"{data_root}/{f.rel}" for f in verify if f.group is None]
        groups = sorted({f.group for f in verify if f.group is not None})
        dev_sha = device_sha256(adb, singles, [f"{data_root}/{g}" for g in groups])
        for f in verify:
            (ok if dev_sha.get(f"{data_root}/{f.rel}") == f.sha256 else push).append(f)

    # Archives init_faf.lua would mount that the host install does not have (or does not allow)
    # change the first-mount-wins file set. Other maps and mods do not: Windows mounts the whole
    # maps directory and vault too, and a run for other replays leaves their maps here.
    extra = []
    planned = {f"{data_root}/{f.rel}" for f in plan.files}
    for path in present:
        rel = path[len(data_root) + 1:]
        if path not in planned and rel.count("/") == 2 and rel.rsplit("/", 1)[0] in ("faf/gamedata", "scfa/gamedata"):
            extra.append(path)
    if extra:
        say(f"   {len(extra)} files on the device that the Windows runner does not mount"
            f"{' (deleting: --prune)' if args.prune else ' (left; --prune deletes them)'}:")
        for path in sorted(extra)[:20]:
            say(f"     {path}")
        if len(extra) > 20:
            say(f"     ... {len(extra) - 20} more")

    push_bytes = sum(f.size for f in push)
    say(f"   on the device and verified: {len(ok)} files; to push: {len(push)} files, {human(push_bytes)}")
    if push and not adb.attached:
        say(f"   free space: not known (no device); needs {human(push_bytes)} plus {human(FREE_SPACE_MARGIN)} "
            f"on the file system of {data_root}")
    if push and adb.attached:
        free = free_bytes(adb, data_root)
        if free is not None:
            say(f"   free on the device: {human(free)}")
            if free < push_bytes + FREE_SPACE_MARGIN:
                raise Failure(f"not enough space on the device: {human(push_bytes)} to push plus "
                              f"{human(FREE_SPACE_MARGIN)} margin, {human(free)} free")

    if args.prune and extra:
        for i in range(0, len(extra), 50):
            adb.sh("rm -rf " + " ".join(q(p) for p in extra[i:i + 50]))

    dirs = {f"{data_root}/{d}" for d in plan.empty_dirs}
    dirs |= {f"{data_root}/{f.rel.rsplit('/', 1)[0]}" for f in plan.files if f.group is None}
    dirs |= {f"{data_root}/{g.rsplit('/', 1)[0]}" for g in plan.groups}
    dirs |= {p.rsplit("/", 1)[0] for p in plan.generated}
    adb.sh("mkdir -p " + " ".join(q(d) for d in sorted(dirs)))

    started = time.time()
    pushed_groups = sorted({f.group for f in push if f.group is not None})
    for f in push:
        if f.group is None:
            say(f"   push {f.rel} ({human(f.size)})")
            adb.push([f.src], f"{data_root}/{f.rel}", timeout=7200)
    for group in pushed_groups:
        say(f"   push {group}/ ({human(sum(f.size for f in plan.files if f.group == group))})")
        adb.push([plan.groups[group]], f"{data_root}/{group.rsplit('/', 1)[0]}/", timeout=7200)
    if push and not adb.dry_run:
        seconds = time.time() - started
        say(f"   pushed {human(push_bytes)} in {seconds:.0f} s")
        dev_sha = device_sha256(adb, [f"{data_root}/{f.rel}" for f in push if f.group is None],
                                [f"{data_root}/{g}" for g in pushed_groups])
        bad = [f.rel for f in push if dev_sha.get(f"{data_root}/{f.rel}") != f.sha256]
        if bad:
            raise Failure(f"sha256 differs on the device after the push: {', '.join(bad[:10])}")
        say(f"   verified {len(push)} pushed files by sha256")

    # Generated files (fa_path.lua, Game.prefs): small, pushed every time.
    gen_dir = os.path.join(out_dir, "_data", safe_name(adb.serial))
    for dev_path, data in sorted(plan.generated.items()):
        host = os.path.join(gen_dir, dev_path.lstrip("/").replace("/", os.sep))
        say(f"   write {dev_path} ({len(data)} bytes)")
        if adb.dry_run:
            for line in data.decode("utf-8", "replace").splitlines()[:12]:
                say(f"     | {line}")
        else:
            write_file(host, data)
        adb.push([host], dev_path)

    # The device manifest keeps the entries of earlier runs' files that are still there (other maps).
    pruned = set(extra) if args.prune else set()
    files_out = {rel: entry for rel, entry in manifest.items()
                 if f"{data_root}/{rel}" in present and f"{data_root}/{rel}" not in pruned
                 and present[f"{data_root}/{rel}"] == entry.get("size")}
    files_out.update({f.rel: {"size": f.size, "sha256": f.sha256} for f in plan.files})
    manifest_out = {
        "schema": DATA_SCHEMA,
        "tool": "scripts/port/run_runner_android.py",
        "updated": now_utc(),
        "sources": plan.sources,
        "total_bytes": sum(e["size"] for e in files_out.values()),
        "files": files_out,
        "generated": {p: hashlib.sha256(d).hexdigest() for p, d in sorted(plan.generated.items())},
    }
    if push or verify or manifest != files_out or args.verify_data:
        host_manifest = os.path.join(gen_dir, DATA_MANIFEST)
        if not adb.dry_run:
            write_file(host_manifest, json.dumps(manifest_out, indent=1, sort_keys=True))
        adb.push([host_manifest], f"{data_root}/{DATA_MANIFEST}")
    digest = hashlib.sha256(json.dumps(manifest_out["files"], sort_keys=True).encode()).hexdigest()[:16]
    return {"files": len(plan.files), "bytes": plan.total(), "pushed_files": len(push), "pushed_bytes": push_bytes,
            "manifest_digest": digest, "sources": plan.sources, "notes": plan.notes}


# --------------------------------------------------------------------------------------------------
# Binaries


def prepare_binaries(abi, build_dir, tools, out_dir, dry_run):
    """Stripped copies of the runner binaries for the device; the unstripped ones stay the symbols."""
    cache = os.path.join(out_dir, "_push", abi)
    result = {}
    for name in (EXE_NAME, LIB_NAME):
        src = os.path.join(build_dir, name)
        dst = os.path.join(cache, name)
        if not os.path.isfile(src):
            if dry_run:
                say(f"   (dry run: {src} does not exist yet)")
                result[name] = {"unstripped": src, "stripped": dst, "stripped_size": 0, "sha256": None, "build_id": None}
                continue
            raise Failure(f"{src} not found: build it (scripts/port/build_runner.py --abi {abi}) or pass --build")
        st = os.stat(src)
        stamp_path = dst + ".stamp.json"
        key = {"src": os.path.abspath(src), "size": st.st_size, "mtime_ns": st.st_mtime_ns}
        try:
            with open(stamp_path, "r", encoding="utf-8") as f:
                stamp = json.load(f)
        except (OSError, ValueError):
            stamp = {}
        cmd = [tools["llvm-strip"], "--strip-debug", "-o", dst, src]
        if stamp.get("key") != key or not os.path.isfile(dst):
            if dry_run:
                say(f"  [dry-run] {fmt_cmd(cmd)}")
                stamp = {"sha256": None}
            else:
                os.makedirs(cache, exist_ok=True)
                p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if p.returncode != 0:
                    raise Failure(f"{fmt_cmd(cmd)}: {p.stdout.decode('utf-8', 'replace')}")
                with open(dst, "rb") as f:
                    digest = hashlib.sha256(f.read()).hexdigest()
                stamp = {"key": key, "sha256": digest}
                write_file(stamp_path, json.dumps(stamp, indent=1))
        result[name] = {
            "unstripped": os.path.abspath(src),
            "unstripped_size": st.st_size,
            "unstripped_mtime": datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds"),
            "stripped": dst,
            "stripped_size": os.path.getsize(dst) if os.path.isfile(dst) and stamp.get("sha256") else 0,
            "sha256": stamp.get("sha256"),
            "build_id": build_id(tools["llvm-readelf"], src),
        }
    return result


def push_binaries(adb, binaries, bin_dir):
    say(f"== Binaries -> {adb.serial}:{bin_dir}")
    dev_sha = device_sha256(adb, [f"{bin_dir}/{name}" for name in binaries])
    adb.sh(f"mkdir -p {q(bin_dir)}")
    for name, b in binaries.items():
        dev_path = f"{bin_dir}/{name}"
        tag = f"build id {b['build_id']}" if b["build_id"] else "no build id"
        if b["sha256"] and dev_sha.get(dev_path) == b["sha256"]:
            say(f"   {name}: up to date ({human(b['stripped_size'])}, {tag})")
            continue
        size = f"{human(b['stripped_size'])} stripped" if b["sha256"] else "stripped copy not made yet"
        say(f"   push {name} ({size}, {tag}; symbols: {b['unstripped']})")
        adb.push([b["stripped"]], dev_path, timeout=1800)
    adb.sh(f"chmod 755 {q(bin_dir + '/' + EXE_NAME)}")


# --------------------------------------------------------------------------------------------------
# Crash reports


FRAME_RE = re.compile(r"^\s*#(\d+)\s+pc\s+([0-9a-fA-F]+)\s+(\S+)(.*)$")


def symbolize(text, objects, symbolizer):
    """Appends file:line (with inlined frames) to every backtrace frame in one of `objects`
    ({basename: unstripped host path}); returns the annotated text and the number of frames."""
    lines = text.splitlines()
    wanted = {}
    for line in lines:
        m = FRAME_RE.match(line)
        if m and os.path.basename(m.group(3)) in objects:
            wanted.setdefault(os.path.basename(m.group(3)), set()).add(int(m.group(2), 16))
    resolved = {}
    for base, addresses in wanted.items():
        ordered = sorted(addresses)
        cmd = [symbolizer, f"--obj={objects[base]}", "--inlines", "--demangle", "--functions=linkage",
               "--output-style=LLVM"]
        try:
            p = subprocess.run(cmd, input=("\n".join(f"0x{a:x}" for a in ordered) + "\n").encode(),
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
            blocks = p.stdout.decode("utf-8", "replace").replace("\r\n", "\n").strip("\n").split("\n\n")
        except (OSError, subprocess.SubprocessError) as e:
            blocks = [f"?? ({e})"] * len(ordered)
        roots = (REPO_ROOT.replace("\\", "/") + "/", REPO_ROOT + os.sep)
        for address, block in zip(ordered, blocks):
            parts = block.strip().splitlines()
            frames = []
            for i in range(0, len(parts) - 1, 2):
                location = parts[i + 1]
                for root in roots:
                    location = location.replace(root, "")
                frames.append(f"{parts[i]} at {location}")
            resolved[(base, address)] = frames
    out = []
    count = 0
    for line in lines:
        out.append(line)
        m = FRAME_RE.match(line)
        if m:
            frames = resolved.get((os.path.basename(m.group(3)), int(m.group(2), 16)))
            if frames:
                count += 1
                for i, frame in enumerate(frames):
                    out.append(f"{'':10}{'inlined into ' if i < len(frames) - 1 else '-> '}{frame}")
    return "\n".join(out) + "\n", count


def crash_block_for_pid(logcat_text, pid):
    """The tombstone printed to the crash buffer for `pid`, if any."""
    if not pid:
        return None
    blocks = re.split(r"(?m)^.*\*\*\* \*\*\* \*\*\* \*\*\* \*\*\* \*\*\*.*$", logcat_text)
    for block in blocks:
        if re.search(r"pid: %s, tid:" % re.escape(str(pid)), block):
            return block
    return None


def strip_logcat_prefix(text):
    """Drops the threadtime prefix so the frames parse (`... F DEBUG   :       #00 pc ...`)."""
    out = []
    for line in text.splitlines():
        m = re.match(r"^\d\d-\d\d \d\d:\d\d:\d\d\.\d+\s+\d+\s+\d+\s+\w\s+[^:]*?:\s?(.*)$", line)
        out.append(m.group(1) if m else line)
    return "\n".join(out)


def list_tombstones(adb):
    rc, out = adb.sh_query("ls -l --full-time /data/tombstones 2>/dev/null || ls -l /data/tombstones 2>/dev/null")
    entries = {}
    for line in out.splitlines():
        parts = line.split()
        if parts and parts[-1].startswith("tombstone_") and not parts[-1].endswith(".pb"):
            entries[parts[-1]] = " ".join(parts[5:-1])
    return entries


# --------------------------------------------------------------------------------------------------
# One run


def windows_reference(ref, name):
    if not ref:
        return None
    if os.path.isfile(ref):
        return ref
    prefix = name.split("-")[0]
    for candidate in (f"{name}.json", f"{prefix}-final.json", f"{prefix}-a.json", f"{prefix}.json"):
        path = os.path.join(ref, candidate)
        if os.path.isfile(path):
            return path
    matches = sorted(p for p in os.listdir(ref) if p.startswith(prefix + "-") and p.endswith(".json")
                     and not p.endswith(".proc.json"))
    return os.path.join(ref, matches[0]) if matches else None


def flatten(prefix, value, out):
    if isinstance(value, dict):
        for key, item in value.items():
            flatten(f"{prefix}.{key}" if prefix else key, item, out)
    else:
        out[prefix] = value
    return out


def compare_with_windows(android, windows):
    fields = ("exit_code", "reached_end", "end_reason", "game_over", "game_over_beat", "last_beat",
              "beats_in_replay", "checkpoints_reached", "checkpoint_chain_fnv1a", "checksum_mismatches",
              "first_mismatch_beat", "lua_errors_load", "lua_errors_sim", "assertions", "warnings")
    result = {"fields": {}}
    for key in fields:
        a, w = android.get(key), windows.get(key)
        result["fields"][key] = {"android": a, "windows": w, "equal": a == w}
    a_cp = {c["beat"]: c.get("sim") for c in android.get("checkpoints", [])}
    w_cp = {c["beat"]: c.get("sim") for c in windows.get("checkpoints", [])}
    beats = sorted(set(a_cp) | set(w_cp))
    diverging = [b for b in beats if a_cp.get(b) != w_cp.get(b)]
    result["checkpoints"] = {
        "android": len(a_cp), "windows": len(w_cp), "equal": len(beats) - len(diverging),
        "first_diverging_beat": diverging[0] if diverging else None,
        "beat0_equal": (a_cp.get(0) == w_cp.get(0)) if (0 in a_cp or 0 in w_cp) else None,
    }
    if diverging:
        b = diverging[0]
        result["checkpoints"]["first_diverging"] = {"beat": b, "android": a_cp.get(b), "windows": w_cp.get(b)}
    a_reg = flatten("", android.get("registry", {}), {})
    w_reg = flatten("", windows.get("registry", {}), {})
    if w_reg:
        result["registry"] = {k: {"android": a_reg.get(k), "windows": w_reg.get(k)}
                              for k in sorted(set(a_reg) | set(w_reg)) if a_reg.get(k) != w_reg.get(k)}
    else:
        result["registry"] = "no registry block in the Windows summary"
    return result


def run_one(args, adb, ctx, replay, index):
    name = os.path.splitext(os.path.basename(replay))[0]
    tag = ctx["tag"]
    stem = f"{tag}-{name}"
    host_dir = ctx["host_dir"]
    dev_runs = ctx["dev_runs"]
    dev = {ext: f"{dev_runs}/{stem}{ext}" for ext in (".out", ".json", ".log", ".registry.txt")}
    dev_replay = f"{ctx['device_dir']}/replays/{os.path.basename(replay).lower()}"
    host = {ext: os.path.join(host_dir, stem + ext) for ext in
            (".out", ".json", ".log", ".registry.txt", ".logcat.txt", ".crash.txt", ".crash.sym.txt",
             ".hang.txt", ".compare.json", ".meta.json", ".device.out")}

    runner_args = ["/headlessreplay", dev_replay, "/init", f"{ctx['data_root']}/faf/bin/init_faf.lua",
                   "/log", dev[".log"], "/headlesssummary", dev[".json"]]
    if args.registry_dump:
        runner_args += ["/headlessregistry", dev[".registry.txt"]]
    runner_args += ctx["extra"]
    env = {"FAF_LOWARENA": str(args.lowarena), "FAF_ENGINE_LIB": f"{ctx['bin_dir']}/{LIB_NAME}",
           "FAF_KNOWN_FOLDERS": f"{ctx['device_dir']}/home"}
    for item in args.env:
        key, _, value = item.partition("=")
        env[key] = value
    env_text = " ".join(f"{k}={q(v)}" for k, v in env.items())
    runner_cmd = f"{env_text} ./{EXE_NAME} " + " ".join(q(a) for a in runner_args)
    script = (f"cd {q(ctx['bin_dir'])} && mkdir -p {q(dev_runs)} && rm -f "
              + " ".join(q(p) for p in dev.values())
              + f" && ( {runner_cmd} </dev/null & p=$!; echo \"{PID_MARK} $p\"; wait $p; echo \"{EXIT_MARK} $?\" ) 2>&1"
              + f" | tee {q(dev['.out'])}")

    say(f"== Run {index}: {name} ({ctx['abi']}{', ARM translation' if ctx['translated'] else ''}, "
        f"FAF_LOWARENA={args.lowarena}, tag {tag})")
    say(f"   runner: cd {ctx['bin_dir']} && {runner_cmd}")
    if adb.dry_run:
        say("  [dry-run] " + fmt_cmd(adb.argv("shell", script)))
        for ext in (".json", ".log") + ((".registry.txt",) if args.registry_dump else ()):
            say("  [dry-run] " + fmt_cmd(adb.argv("pull", dev[ext], host[ext])))
        say(f"  [dry-run] {fmt_cmd(adb.argv('shell', 'logcat -d -v threadtime -b main,system,crash -T <start>'))}"
            f" > {host['.logcat.txt']}")
        return {"name": name, "dry_run": True}

    os.makedirs(host_dir, exist_ok=True)
    for path in host.values():
        if os.path.exists(path):
            os.remove(path)
    rc, start_stamp = adb.sh_query("date '+%m-%d %H:%M:%S.000'")
    start_stamp = start_stamp.strip()
    tombstones_before = list_tombstones(adb)

    started = time.time()
    pid = None
    exit_status = None
    lines = []
    timed_out = threading.Event()
    proc = subprocess.Popen(adb.argv("shell", script), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)

    def watchdog():
        while proc.poll() is None:
            if time.time() - started > args.timeout:
                timed_out.set()
                if not pid:  # the runner never started: end the adb shell
                    say(f"   host timeout after {args.timeout} s before the runner started")
                    proc.kill()
                    return
                # Stacks of every thread: `debuggerd -b` where the shell may use it (root), else
                # SIGABRT, whose tombstone lists all threads; SIGKILL if that does not end it.
                rc2, stacks = adb.sh_query(f"debuggerd -b {pid} 2>&1", timeout=300)
                if rc2 == 0 and "pid:" in stacks:
                    write_file(host[".hang.txt"], stacks)
                    say(f"   host timeout after {args.timeout} s: stacks from debuggerd -b, killing")
                    adb.sh_query(f"kill -9 {pid}")
                    return
                say(f"   host timeout after {args.timeout} s: SIGABRT for a tombstone with every thread")
                adb.sh_query(f"kill -6 {pid}")
                for _ in range(30):
                    if proc.poll() is not None:
                        return
                    time.sleep(1)
                adb.sh_query(f"kill -9 {pid}")
                return
            time.sleep(2)

    threading.Thread(target=watchdog, daemon=True).start()
    try:
        with open(host[".out"], "w", encoding="utf-8", newline="\n") as out:
            for raw in proc.stdout:
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                if line.startswith(PID_MARK):
                    pid = int(line.split()[1])
                    say(f"   device pid {pid}")
                    continue
                if line.startswith(EXIT_MARK):
                    exit_status = int(line.split()[1])
                    continue
                lines.append(line)
                out.write(line + "\n")
                out.flush()
                say("   | " + line)
        proc.wait()
    except KeyboardInterrupt:
        if pid:
            adb.sh_query(f"kill -9 {pid}")
        proc.kill()
        raise
    wall = time.time() - started

    if exit_status is None:
        # The adb connection ended before the shell did: take the device's copy of stdout.
        adb.query("pull", dev[".out"], host[".device.out"])
    for ext in (".json", ".log") + ((".registry.txt",) if args.registry_dump else ()):
        rc, out = adb.query("pull", dev[ext], host[ext])
        if rc != 0 and os.path.exists(host[ext]):
            os.remove(host[ext])
    rc, logcat = adb.sh_query(f"logcat -d -v threadtime -b main,system,crash -T {q(start_stamp)}")
    write_file(host[".logcat.txt"], logcat)

    signal = exit_status - 128 if exit_status is not None and exit_status > 128 else None
    crash = {}
    if signal is not None or timed_out.is_set():
        crash_text = None
        source = None
        after = list_tombstones(adb)
        for tomb in sorted(after):
            if after[tomb] != tombstones_before.get(tomb):
                rc, text = adb.sh_query(f"cat /data/tombstones/{tomb}", timeout=300)
                if rc == 0 and pid and re.search(r"pid: %d, tid:" % pid, text):
                    crash_text, source = text, f"/data/tombstones/{tomb}"
        if crash_text is None:
            rc, crashbuf = adb.sh_query(f"logcat -d -v threadtime -b crash -T {q(start_stamp)}")
            block = crash_block_for_pid(crashbuf, pid)
            if block:
                crash_text, source = strip_logcat_prefix(block), "logcat -b crash"
        if crash_text is None and os.path.exists(host[".hang.txt"]):
            crash_text, source = read_text(host[".hang.txt"]), "debuggerd -b"
        if crash_text is not None:
            write_file(host[".crash.txt"], crash_text)
            objects = {LIB_NAME: ctx["binaries"][LIB_NAME]["unstripped"],
                       EXE_NAME: ctx["binaries"][EXE_NAME]["unstripped"]}
            annotated, frames = symbolize(crash_text, objects, ctx["tools"]["llvm-symbolizer"])
            write_file(host[".crash.sym.txt"], annotated)
            # crash_dump cannot read libraries under /data/local/tmp (SELinux), so the frames carry no
            # BuildId; the pushed .so was checked by sha256 against the stripped copy of these symbols.
            ids = set(re.findall(r"libfafengine\.so.*?\(BuildId: ([0-9a-f]+)\)", crash_text))
            expected = ctx["binaries"][LIB_NAME]["build_id"]
            crash = {"source": source, "symbolized_frames": frames, "symbols_build_id": expected,
                     "build_id_check": (("matches" if expected in ids else f"differs: {sorted(ids)}") if ids
                                        else "not in the tombstone; the pushed .so matched by sha256")}
            m = re.search(r"Abort message: '(.*)'", crash_text)
            if m:
                crash["abort_message"] = m.group(1)
            m = re.search(r"signal \d+ \((\w+)\), code [-\d]+ \(([^)]*)\), fault addr (\S+)", crash_text)
            if m:
                crash["signal"], crash["code"], crash["fault_addr"] = m.group(1), m.group(2), m.group(3)
            if bid := ctx["binaries"][LIB_NAME]["build_id"]:
                keep = os.path.join(host_dir, "symbols", bid, LIB_NAME)
                if not os.path.isfile(keep):
                    os.makedirs(os.path.dirname(keep), exist_ok=True)
                    shutil.copy2(ctx["binaries"][LIB_NAME]["unstripped"], keep)
                crash["symbols"] = keep
        else:
            crash = {"source": None, "note": "no tombstone, crash-buffer entry or stack dump found for the pid"}

    summary = None
    if os.path.isfile(host[".json"]):
        try:
            with open(host[".json"], "r", encoding="utf-8") as f:
                summary = json.load(f)
        except ValueError as e:
            summary = {"_unreadable": str(e)}
    result_line = next((l for l in reversed(lines) if "] RESULT " in l), None)
    # LowArena's own report (high-water mark at exit, segments), whatever its exact format.
    arena_lines = [l for l in lines if re.search(r"low.?arena|high.?water", l, re.I)][-20:]

    comparison = None
    ref = windows_reference(args.windows_ref, name)
    if ref and summary and "_unreadable" not in summary:
        with open(ref, "r", encoding="utf-8") as f:
            comparison = compare_with_windows(summary, json.load(f))
        comparison["windows_summary"] = os.path.abspath(ref)
        write_file(host[".compare.json"], json.dumps(comparison, indent=1))

    meta = {
        "tool": "scripts/port/run_runner_android.py",
        "started_utc": datetime.datetime.fromtimestamp(started, datetime.timezone.utc).isoformat(timespec="seconds"),
        "host_wall_seconds": round(wall, 3),
        "repo_head": ctx["git"],
        "serial": adb.serial,
        "device": ctx["device"],
        "abi": ctx["abi"],
        "translated": ctx["translated"],
        "lowarena": args.lowarena,
        "replay": os.path.abspath(replay),
        "replay_sha256": hashlib.sha256(open(replay, "rb").read()).hexdigest(),
        "device_replay": dev_replay,
        "command": runner_cmd,
        "cwd": ctx["bin_dir"],
        "pid": pid,
        "exit_status": exit_status,
        "exit_meaning": (SIGNALS.get(signal, f"signal {signal}") if signal is not None
                         else EXIT_MEANING.get(exit_status, "connection lost" if exit_status is None else "?")),
        "host_timeout": timed_out.is_set(),
        "result_line": result_line,
        "arena_lines": arena_lines,
        "binaries": ctx["binaries"],
        "data": ctx["data"],
        "crash": crash or None,
        "windows_reference": comparison["windows_summary"] if comparison else None,
        "files": {ext: path for ext, path in host.items() if os.path.exists(path)},
    }
    write_file(host[".meta.json"], json.dumps(meta, indent=1))

    say(f"   exit {exit_status} ({meta['exit_meaning']}), {wall:.1f} s on the host")
    if result_line:
        say("   " + result_line.strip())
    for line in arena_lines[-3:]:
        say("   " + line.strip())
    if summary and "_unreadable" not in summary:
        reg = summary.get("registry")
        if reg:
            say(f"   registry {json.dumps(reg)}")
    if crash:
        say(f"   crash: {json.dumps({k: v for k, v in crash.items() if k != 'symbols'})}")
        if os.path.exists(host[".crash.sym.txt"]):
            shown = 0
            for line in read_text(host[".crash.sym.txt"]).splitlines():
                if FRAME_RE.match(line) or line.startswith(" " * 10):
                    say("   " + line.rstrip())
                    shown += 1
                    if shown >= 40:
                        break
    if comparison:
        c = comparison["checkpoints"]
        diff = [k for k, v in comparison["fields"].items() if not v["equal"]]
        say(f"   vs Windows ({os.path.relpath(comparison['windows_summary'], REPO_ROOT)}): "
            f"checkpoints {c['equal']}/{max(c['android'], c['windows'])} equal, first diverging beat "
            f"{c['first_diverging_beat']}, beat 0 {'equal' if c['beat0_equal'] else 'differs' if c['beat0_equal'] is not None else '-'}; "
            f"differing fields: {', '.join(diff) or 'none'}; registry: "
            f"{'equal' if comparison['registry'] == {} else comparison['registry'] if isinstance(comparison['registry'], str) else ', '.join(comparison['registry'])}")
    say(f"   files: {host_dir}{os.sep}{stem}.*")
    reached = bool(summary and summary.get("reached_end")) and exit_status == 0
    return {"name": name, "exit": exit_status, "meaning": meta["exit_meaning"], "reached_end": reached,
            "wall": round(wall, 1), "summary": summary, "crash": crash or None, "comparison": comparison,
            "result_line": result_line, "arena_lines": arena_lines}


# --------------------------------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="Arguments after `--` are passed to the runner unchanged.")
    ap.add_argument("--replay", nargs="+", action="extend", default=[], metavar="FILE",
                    help=".scfareplay file(s), each played in its own run")
    ap.add_argument("--abi", choices=ABIS, help="default: the device's primary ABI (arm64-v8a without a device)")
    ap.add_argument("--serial", help="adb serial (default: $ANDROID_SERIAL or the only attached device)")
    ap.add_argument("--lowarena", type=int, choices=(0, 1), default=1, help="FAF_LOWARENA for the runner (default 1)")
    ap.add_argument("--data-root", help="game data root on the device (default <device dir>/data)")
    ap.add_argument("--device-dir", default=DEFAULT_DEVICE_DIR, help=f"runner directory on the device (default {DEFAULT_DEVICE_DIR})")
    ap.add_argument("--tag", help="name prefix of the output files (default la1 or la0)")
    ap.add_argument("--build", action="store_true", help="run build_runner.py --abi <abi> --out <build dir> first")
    ap.add_argument("--build-dir", help="where the binaries are (default buildstage/runner/<abi>)")
    ap.add_argument("--build-jobs", type=int, help="--jobs for build_runner.py (default: its own)")
    ap.add_argument("--out-dir", default=DEFAULT_OUT, help="host output root (default buildstage/runner-runs)")
    ap.add_argument("--dry-run", action="store_true", help="print what would be pushed and run; change nothing")
    ap.add_argument("--skip-data", action="store_true", help="do not check or push the game data")
    ap.add_argument("--data-only", action="store_true", help="sync the game data (for --replay's maps) and stop")
    ap.add_argument("--verify-data", action="store_true", help="check every data file on the device by sha256")
    ap.add_argument("--prune", action="store_true", help="delete data files on the device the Windows runner would not mount")
    ap.add_argument("--registry-dump", action="store_true", help="also write and pull /headlessregistry")
    ap.add_argument("--env", action="append", default=[], metavar="NAME=VALUE",
                    help="extra environment variable for the runner (repeatable)")
    ap.add_argument("--windows-ref", metavar="FILE|DIR", help="Windows summary JSON (or a directory of them) to compare with")
    ap.add_argument("--timeout", type=int, default=7200, help="host-side limit per run in seconds (default 7200)")
    ap.add_argument("--faf-path", default=DEFAULT_FAF_PATH, help=f"FAF client data (default {DEFAULT_FAF_PATH})")
    ap.add_argument("--scfa-path", help="SCFA install (default: fa_path from <faf path>/fa_path.lua)")
    ap.add_argument("--vault-path", help="FAF vault (default: custom_vault_path from fa_path.lua)")
    ap.add_argument("--host-prefs", help="Game.prefs whose PreGameData goes to the device (default: the host "
                                         "profile's; 'none' for no Game.prefs)")
    ap.add_argument("--vault-mod", action="append", default=[], metavar="NAME", help="also copy this vault mod")
    ap.add_argument("--adb", help="adb executable")
    ap.add_argument("--ndk", help="Android NDK directory")
    argv = sys.argv[1:]
    extra = []
    if "--" in argv:
        cut = argv.index("--")
        argv, extra = argv[:cut], argv[cut + 1:]
    args = ap.parse_args(argv)

    for arg in extra:
        if re.match(r"^[A-Za-z]:[/\\].*[/\\]Git[/\\]", arg):
            ap.error(f"runner argument {arg!r} looks like an MSYS path conversion of a /option: "
                     "run with MSYS_NO_PATHCONV=1")
    for item in args.env:
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", item):
            ap.error(f"--env {item!r}: expected NAME=VALUE")
    if not args.replay and not args.data_only:
        ap.error("--replay is required")
    replays = [os.path.abspath(r) for r in args.replay]
    for replay in replays:
        if not os.path.isfile(replay):
            ap.error(f"{replay}: no such file")

    adb_exe = find_adb(args.adb)
    if not adb_exe and not args.dry_run:
        raise Failure("adb not found (pass --adb)")
    devices = list_devices(adb_exe)
    serial = args.serial or os.environ.get("ANDROID_SERIAL")
    if not serial:
        ready = [s for s, state in devices.items() if state == "device"]
        if len(ready) != 1:
            raise Failure(f"pass --serial (attached: {devices or 'none'})")
        serial = ready[0]
    attached = devices.get(serial) == "device"
    if not attached and not args.dry_run:
        raise Failure(f"device {serial} is not attached ({devices.get(serial, 'not listed')}); "
                      f"attached: {devices or 'none'}")
    adb = Adb(adb_exe, serial, args.dry_run, attached)

    info = device_info(adb) if attached else {}
    abilist = [a for a in info.get("ro.product.cpu.abilist", "").split(",") if a]
    abi = args.abi or (abilist[0] if abilist else "arm64-v8a")
    if abi not in ABIS:
        raise Failure(f"the device's primary ABI {abi} is not one the runner builds; pass --abi")
    if abilist and abi not in abilist:
        raise Failure(f"{serial} does not run {abi} (abilist {','.join(abilist)})")
    translated = bool(abilist) and abi != abilist[0]
    device_dir = args.device_dir.rstrip("/")
    data_root = (args.data_root or f"{device_dir}/data").rstrip("/")
    tag = args.tag or f"la{args.lowarena}"
    host_dir = os.path.join(args.out_dir, safe_name(serial), abi)

    say(f"run_runner_android: {serial} "
        + (f"({info.get('ro.product.manufacturer', '')} {info.get('ro.product.model', '')}, Android "
           f"{info.get('ro.build.version.release', '?')} / API {info.get('ro.build.version.sdk', '?')}, "
           f"abilist {','.join(abilist)}, kernel {info.get('kernel', '?')}, page {info.get('pagesize', '?')}, "
           f"RAM {human(int(info.get('memtotal_kb') or 0) * 1024)})" if attached else "(not attached: assuming an empty device)"))
    say(f"   abi {abi}{' under ARM translation (' + (info.get('ro.dalvik.vm.native.bridge') or '?') + ')' if translated else ''}"
        f", device dir {device_dir}, data root {data_root}, output {host_dir}")
    if args.dry_run:
        say("   DRY RUN: adb commands that would change the device are printed, not run")

    hashes = HostHashes(os.path.join(args.out_dir, "_cache", "host-sha256.json"))
    data = None
    if not args.skip_data:
        plan = build_data_plan(args, replays, data_root, device_dir)
        data = sync_data(args, adb, plan, data_root, args.out_dir, hashes)
    if args.data_only:
        return 0

    build_dir = os.path.abspath(args.build_dir or os.path.join(REPO_ROOT, "buildstage", "runner", abi))
    if args.build:
        cmd = [sys.executable, BUILD_RUNNER, "--abi", abi, "--out", build_dir]
        if args.build_jobs:
            cmd += ["--jobs", str(args.build_jobs)]
        say(f"== Build (below-normal priority): {fmt_cmd(cmd)}")
        if args.dry_run:
            say("  [dry-run] " + fmt_cmd(cmd))
        else:
            # The compilers inherit the below-normal priority class on Windows.
            flags = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0) if os.name == "nt" else 0
            if subprocess.run(cmd, creationflags=flags).returncode != 0:
                raise Failure("build_runner.py failed")
    tools = find_ndk_tools(args.ndk)
    binaries = prepare_binaries(abi, build_dir, tools, args.out_dir, args.dry_run)
    bin_dir = f"{device_dir}/{abi}"
    push_binaries(adb, binaries, bin_dir)

    say(f"== Replays -> {serial}:{device_dir}/replays")
    adb.sh(f"mkdir -p {q(device_dir + '/replays')} {q(device_dir + '/home')}")
    for replay in replays:
        adb.push([replay], f"{device_dir}/replays/{os.path.basename(replay).lower()}")
    hashes.save()

    ctx = {"abi": abi, "translated": translated, "tag": tag, "host_dir": host_dir, "device_dir": device_dir,
           "data_root": data_root, "bin_dir": bin_dir, "dev_runs": f"{device_dir}/runs/{abi}", "extra": extra,
           "binaries": binaries, "tools": tools, "device": info, "data": data, "git": git_head()}
    results = [run_one(args, adb, ctx, replay, i + 1) for i, replay in enumerate(replays)]
    if args.dry_run:
        return 0

    say("== Summary")
    failed = 0
    for r in results:
        s = r["summary"] or {}
        c = (r["comparison"] or {}).get("checkpoints", {})
        say(f"   {r['name']:<16} exit {r['exit']} ({r['meaning']}), reached_end {s.get('reached_end')}, "
            f"beats {s.get('last_beat')}/{s.get('beats_in_replay')}, chain {s.get('checkpoint_chain_fnv1a')}, "
            f"lua errors {s.get('lua_errors_load')}/{s.get('lua_errors_sim')}, {s.get('beats_per_second')} beats/s"
            + (f", first diverging checkpoint vs Windows: beat {c.get('first_diverging_beat')}" if c else ""))
        failed += 0 if r["reached_end"] else 1
    return 1 if failed else 0


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except (AttributeError, ValueError):
            pass
    try:
        sys.exit(main())
    except Failure as e:
        print(f"run_runner_android: {e}", file=sys.stderr, flush=True)
        sys.exit(2)
