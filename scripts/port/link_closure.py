#!/usr/bin/env python3
"""Link closure of the headless replay runner, computed from the Win32 Debug objects (M3b gate).

The Android runner (port/engine/runner, docs/port/android-roadmap.md M3b) links the code that
`main.exe /headlessreplay` runs on Windows - `moho::HEADLESS_RunReplay` and everything it
reaches - into `libfafengine.so`. Which translation units (TUs) that is cannot be read from the
arm64 build: a quarter of the engine does not compile there yet, and a TU that is missing from an
ELF link fails silently when it only registers something (a Lua binder, a reflected type, a
console variable) from a static initialiser. So the closure is computed on the build that works,
Debug|Win32, whose objects reference exactly what each TU needs:

  1. Read every object main.vcxproj builds for Debug|Win32 (ObjectFileName overrides resolved),
     plus WildMagic's Foundation objects (dependencies/WildMagic3p8/Foundation/Debug, the
     gitignored third-party build), and the symbol tables of the libraries the link uses.
  2. Roots: the TU that defines HEADLESS_RunReplay, and every linkable TU whose static initialisers
     register something (REG_KINDS: Lua binders, reflected types, console commands/variables,
     serializer helpers, prefetch kinds, resource factories), plus, in the default variant, every
     other non-user-side TU with a static initialiser.
  3. The user side (UI, render, audio, movie, wx, live networking, the user session; see
     `stub_policy`) may be replaced by runner-only stubs, the sim side never. A minimum cut over
     symbols finds the fewest user-side symbols whose stubs keep every user-side TU that cannot be
     linked (it does not compile for arm64, or calls wx/D3D/DirectSound/Winsock) out of the link.
  4. The closure is every TU reachable from the roots with those symbols cut, linked as whole
     objects (no archive extraction, no --gc-sections): every undefined symbol of a closure TU must
     be defined by another closure TU, a stub or a system library.

Outputs:
  port/engine/runner/closure.txt   the closure, one TU per line (src/sdk-relative, as main.vcxproj
                                   lists them), engine TUs in project order (MSVC's link order and
                                   so its static-initialiser order), then the third-party TUs
  <report-dir>/report.md           roots, the variants compared, the closure by subsystem, the TUs in
  <report-dir>/closure.json        it that do not compile for arm64, the references that cross into
                                   stubbed TUs (what the stubs must provide), library use, unresolved
                                   symbols and the duplicate definitions MSVC's /FORCE hides

usage:
  python scripts/port/link_closure.py                 # recompute closure.txt and the report
  python scripts/port/link_closure.py --check         # exit 1 if closure.txt is not up to date
  python scripts/port/link_closure.py --variant init  # another root variant (lua, reg, init)

It needs the Debug|Win32 objects (build main.vcxproj Debug|Win32 first) and an arm64 sweep for
the compile status (default buildstage/engine-sweep/m3b, the sweep after the M3b ports; M2's was
buildstage/engine-sweep/final). Parsed objects and demangled names are cached under
buildstage/runner/cache. build_runner.py imports this module to map the arm64 link's undefined
symbols back to the TU that defines them on Windows.
"""

import argparse
import collections
import concurrent.futures
import datetime
import glob
import hashlib
import json
import os
import pickle
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import engine_sweep as es  # noqa: E402  (the project reader and path helpers)

REPO_ROOT = es.REPO_ROOT
SDK_DIR = es.DEFAULT_SDK
VCXPROJ = os.path.join(SDK_DIR, "main.vcxproj")
CONFIG = "Debug|Win32"
DEFAULT_OBJDIR = os.path.join(REPO_ROOT, "buildstage", "main", "Win32", "Debug")
DEFAULT_SWEEP = os.path.join(REPO_ROOT, "buildstage", "engine-sweep", "m3b")
DEFAULT_CLOSURE = os.path.join(REPO_ROOT, "port", "engine", "runner", "closure.txt")
DEFAULT_REPORT_DIR = os.path.join(REPO_ROOT, "buildstage", "runner", "closure")
CACHE_DIR = os.path.join(REPO_ROOT, "buildstage", "runner", "cache")
WM3_DIR = os.path.join(REPO_ROOT, "dependencies", "WildMagic3p8", "Foundation")
WM3_PREFIX = "../../dependencies/WildMagic3p8/Foundation/"
CACHE_VERSION = 4

# The entry point. HeadlessReplay.cpp defines it; WinMain only branches into it.
ROOT_SYMBOL = "?HEADLESS_RunReplay@moho@@YAHXZ"

MSB = es.MSB


def fwd(p):
    return p.replace("\\", "/")


# ------------------------------------------------------------------------------------------------
# COFF objects and libraries
# ------------------------------------------------------------------------------------------------

BIGOBJ_CLSID = bytes.fromhex("c7a1bad1eebaa94baf20faf66aa4dcb8")
SCN_LNK_COMDAT = 0x1000
SCN_LNK_REMOVE = 0x800
SCN_LNK_INFO = 0x200


def read_coff(path):
    """The link-relevant facts of one COFF object (i386/x64, regular or /bigobj).

    defs:      {name: comdat selection} for external definitions (0 = not COMDAT, -1 = common)
    undef:     external symbols the object references without defining
    weak:      weak externals (resolve to their default when nothing defines them)
    init:      static-initialiser sections (.CRT$XC*/.CRT$XI*), with a COMDAT flag
    defaultlib: libraries named by `#pragma comment(lib)` (.drectve /DEFAULTLIB)
    vft:       vftables (??_7) the object defines or references
    td_uses:   RTTI type descriptors (??_R0) referenced from anything but MSVC's own RTTI records
               (??_R1-??_R4): typeid, dynamic_cast, throw and catch
    """
    with open(path, "rb") as f:
        data = f.read()
    sig1, sig2 = struct.unpack_from("<HH", data, 0)
    if sig1 == 0 and sig2 == 0xFFFF:
        if data[12:28] != BIGOBJ_CLSID:
            raise ValueError(f"{path}: an import or anonymous object, not a COFF object")
        nsec, symptr, nsym = struct.unpack_from("<III", data, 44)
        hdr, symsize, bigobj = 56, 20, True
    else:
        _machine, nsec, _ts, symptr, nsym, opthdr, _chars = struct.unpack_from("<HHIIIHH", data, 0)
        hdr, symsize, bigobj = 20 + opthdr, 18, False
    strtab_off = symptr + nsym * symsize
    strtab = data[strtab_off:]

    def long_name(off):
        end = strtab.index(b"\0", off)
        return strtab[off:end].decode("latin-1")

    sections = []
    for i in range(nsec):
        o = hdr + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("latin-1")
        if name.startswith("/") and name[1:].isdigit():
            name = long_name(int(name[1:]))
        _vs, _va, rawsize, rawptr, relptr, _ln, nrel, _nln, chars = struct.unpack_from("<IIIIIIHHI", data, o + 8)
        if chars & 0x01000000 and nrel == 0xFFFF:  # IMAGE_SCN_LNK_NRELOC_OVFL: the count is the first entry
            nrel = struct.unpack_from("<I", data, relptr + 4)[0] - 1
            relptr += 10
        relocs = {struct.unpack_from("<I", data, relptr + r * 10 + 4)[0] for r in range(nrel)}
        sections.append([name, chars, rawsize, rawptr, 0, relocs])  # name, chars, size, data, comdat sel, relocs

    syms = []
    by_index = {}
    i = 0
    while i < nsym:
        o = symptr + i * symsize
        raw = data[o:o + 8]
        if raw[:4] == b"\0\0\0\0":
            name = long_name(struct.unpack_from("<I", raw, 4)[0])
        else:
            name = raw.rstrip(b"\0").decode("latin-1")
        if bigobj:
            value, secnum, _typ, sclass, naux = struct.unpack_from("<IiHBB", data, o + 8)
        else:
            value, secnum, _typ, sclass, naux = struct.unpack_from("<IhHBB", data, o + 8)
        syms.append((name, secnum, value, sclass))
        by_index[i] = (name, secnum)
        if sclass == 3 and naux >= 1 and value == 0 and secnum > 0:
            # Section definition: its aux record carries the COMDAT selection.
            ao = o + symsize
            selection = struct.unpack_from("<B", data, ao + 14)[0]
            sec = sections[secnum - 1]
            if sec[1] & SCN_LNK_COMDAT and sec[4] == 0:
                sec[4] = selection
        i += 1 + naux

    defs, undef, weak = {}, set(), set()
    for name, secnum, value, sclass in syms:
        if sclass == 2:
            if secnum > 0:
                defs.setdefault(name, sections[secnum - 1][4])
            elif secnum == 0:
                if value:
                    defs.setdefault(name, -1)
                else:
                    undef.add(name)
            else:
                defs.setdefault(name, 0)  # absolute
        elif sclass == 105:
            weak.add(name)
    undef -= set(defs)
    weak -= set(defs)
    init = []
    defaultlib = []
    for name, chars, size, ptr, _sel, _relocs in sections:
        if name.startswith((".CRT$XC", ".CRT$XI")) and not chars & SCN_LNK_REMOVE:
            init.append((name, bool(chars & SCN_LNK_COMDAT)))
        if name == ".drectve" and size:
            text = data[ptr:ptr + size].decode("latin-1", "replace")
            for m in re.finditer(r'[/-]defaultlib:(?:"([^"]+)"|(\S+))', text, re.I):
                lib = (m.group(1) or m.group(2)).strip()
                if lib.lower().endswith(".lib"):
                    lib = lib[:-4]
                defaultlib.append(lib)
    # What the Itanium ABI turns into references to a class's key-function TU (see Graph).
    vft = {n for n, _sec, _v, sc in syms if n.startswith("??_7") and sc in (2, 3)}
    rtti_record = [False] * len(sections)
    for n, sec, _v, _sc in syms:
        if sec > 0 and n.startswith(("??_R1", "??_R2", "??_R3", "??_R4")):
            rtti_record[sec - 1] = True
    td_uses = set()
    for idx, sec in enumerate(sections):
        if rtti_record[idx] or sec[0].startswith(".debug$"):
            continue
        for r in sec[5]:
            target = by_index.get(r)
            if target is not None and target[0].startswith("??_R0"):
                td_uses.add(target[0])
    return {"defs": defs, "undef": sorted(undef), "weak": sorted(weak), "init": init,
            "defaultlib": sorted(set(defaultlib)), "vft": sorted(vft), "td_uses": sorted(td_uses)}


def read_lib_symbols(path):
    """Public symbols of an MSVC .lib (static or import library): its first linker member."""
    with open(path, "rb") as f:
        if f.read(8) != b"!<arch>\n":
            return set()
        hdr = f.read(60)
        if len(hdr) < 60 or hdr[:16].strip() != b"/":
            return set()
        size = int(hdr[48:58].strip() or b"0")
        member = f.read(size)
    n = struct.unpack_from(">I", member, 0)[0]
    names = member[4 + 4 * n:].split(b"\0")[:n]
    return {s.decode("latin-1") for s in names}


def _parse_one(job):
    tu, path = job
    return tu, read_coff(path)


# ------------------------------------------------------------------------------------------------
# What to read
# ------------------------------------------------------------------------------------------------

def project_objects(objdir):
    """[(tu, object path)] for every ClCompile item main.vcxproj builds in Debug|Win32."""
    project = es.Project(VCXPROJ, CONFIG)
    built = {tu for tu, _ in project.items}
    names = {}
    for group in project.root.iter(MSB + "ItemGroup"):
        if not project.condition(group):
            continue
        for item in group.findall(MSB + "ClCompile"):
            inc = item.get("Include")
            if not inc or not project.condition(item):
                continue
            tu = fwd(project.expand(inc))
            for child in item.findall(MSB + "ObjectFileName"):
                if project.condition(child) and child.text:
                    names[tu] = fwd(child.text.replace("$(IntDir)", "")).lstrip("/")
    out = []
    for tu, _meta in project.items:
        obj = names.get(tu) or (os.path.splitext(os.path.basename(tu))[0] + ".obj")
        out.append((tu, os.path.join(objdir, obj)))
    return project, out, built


def wm3_objects():
    """[(tu, object path)] for WildMagic Foundation's Debug objects; the TU is the source path
    relative to src/sdk, the way main.vcxproj spells the boost.thread sources."""
    objdir = os.path.join(WM3_DIR, "Debug")
    if not os.path.isdir(objdir):
        return []
    sources = {}
    for path in glob.glob(os.path.join(WM3_DIR, "**", "*.cpp"), recursive=True):
        rel = fwd(os.path.relpath(path, WM3_DIR))
        if rel.split("/")[0] in ("Debug", "Release"):
            continue
        sources.setdefault(os.path.splitext(os.path.basename(path))[0].lower(), rel)
    out = []
    for obj in sorted(glob.glob(os.path.join(objdir, "*.obj"))):
        stem = os.path.splitext(os.path.basename(obj))[0].lower()
        if stem in sources:
            out.append((WM3_PREFIX + sources[stem], obj))
    return out


def link_libraries(project, defaultlibs):
    """[(name, path)] of the libraries the Debug|Win32 link searches, in search order: the project's
    AdditionalDependencies, the toolset's default libraries, then every /DEFAULTLIB the objects
    name. Only used to say which library defines a symbol main.exe imports."""
    deps = []
    for group in project.root.iter(MSB + "ItemDefinitionGroup"):
        if not project.condition(group):
            continue
        for link in group.findall(MSB + "Link"):
            for child in link.findall(MSB + "AdditionalDependencies"):
                if project.condition(child):
                    deps += [d for d in project.expand(child.text).split(";") if d and "%(" not in d]
    deps += ["kernel32.lib", "user32.lib", "gdi32.lib", "winspool.lib", "comdlg32.lib", "advapi32.lib",
             "shell32.lib", "ole32.lib", "oleaut32.lib", "uuid.lib"]
    # The debug CRT, which MSVCRTD.lib's own /DEFAULTLIBs pull in.
    deps += ["vcruntimed.lib", "ucrtd.lib", "msvcprtd.lib", "MSVCRTD.lib", "OLDNAMES.lib"]
    deps += [d + ".lib" for d in defaultlibs]
    dirs = [os.path.join(REPO_ROOT, "dependencies", "DXSDK_D3DX", "lib", "x86"),
            os.path.join(REPO_ROOT, "dependencies", "wxWindows-2.4.2", "lib"),
            os.path.join(WM3_DIR, "Debug"),
            os.path.join(REPO_ROOT, "dependencies", "WildMagic3p8", "Renderers", "Dx9Renderer", "Debug"),
            os.path.join(REPO_ROOT, "dependencies", "WildMagic3p8", "Applications", "Debug")]
    kits = sorted(glob.glob(r"C:/Program Files (x86)/Windows Kits/10/Lib/10.*"))
    if kits:
        dirs += [os.path.join(kits[-1], "um", "x86"), os.path.join(kits[-1], "ucrt", "x86")]
    for vs in sorted(glob.glob(r"C:/Program Files*/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*"), reverse=True):
        dirs.append(os.path.join(vs, "lib", "x86"))
        break
    out, seen = [], set()
    for dep in deps:
        name = os.path.splitext(os.path.basename(dep))[0]
        if name.lower() in seen:
            continue
        seen.add(name.lower())
        for d in dirs:
            p = os.path.join(d, name + ".lib")
            if os.path.isfile(p):
                out.append((name, p))
                break
    return out


# ------------------------------------------------------------------------------------------------
# Database (cached)
# ------------------------------------------------------------------------------------------------

def _write_cache(path, obj):
    """Writes a cache pickle atomically; several builds may run at once, and a cache that cannot be
    written is only slower next time."""
    tmp = f"{path}.{os.getpid()}.tmp"
    try:
        with open(tmp, "wb") as f:
            pickle.dump(obj, f, protocol=pickle.HIGHEST_PROTOCOL)
        os.replace(tmp, path)
    except OSError:
        try:
            os.remove(tmp)
        except OSError:
            pass


def _stat_key(path):
    st = os.stat(path)
    return (st.st_size, st.st_mtime_ns)


def load_database(objdir=DEFAULT_OBJDIR, jobs=None, quiet=False):
    """{'objs': {tu: info}, 'libs': {libname: set}, 'order': [tu...], 'built': set, 'missing': [...]}

    The pickles under buildstage/runner/cache are this script's own cache (plain dicts, sets and
    strings it wrote itself, keyed by object path, size and mtime); nothing else writes there."""
    os.makedirs(CACHE_DIR, exist_ok=True)
    project, items, built = project_objects(objdir)
    items += wm3_objects()
    cache_path = os.path.join(CACHE_DIR, "win32-objects.pickle")
    cache = {}
    try:
        with open(cache_path, "rb") as f:
            loaded = pickle.load(f)
        if loaded.get("version") == CACHE_VERSION:
            cache = loaded["objs"]
    except (OSError, pickle.PickleError, EOFError, KeyError, AttributeError):
        cache = {}
    objs, todo, missing = {}, [], []
    for tu, path in items:
        if not os.path.isfile(path):
            missing.append((tu, path))
            continue
        key = (fwd(os.path.abspath(path)), _stat_key(path))
        hit = cache.get(key[0])
        if hit and hit[0] == key[1]:
            objs[tu] = hit[1]
        else:
            todo.append((tu, path))
    if todo:
        if not quiet:
            print(f"link_closure: reading {len(todo)} objects", file=sys.stderr)
        with concurrent.futures.ProcessPoolExecutor(max_workers=jobs or min(16, os.cpu_count() or 4)) as ex:
            for tu, info in ex.map(_parse_one, todo, chunksize=8):
                objs[tu] = info
    new_cache = {}
    for tu, path in items:
        if tu in objs:
            new_cache[fwd(os.path.abspath(path))] = (_stat_key(path), objs[tu])
    if todo or len(new_cache) != len(cache):
        _write_cache(cache_path, {"version": CACHE_VERSION, "objs": new_cache})

    defaultlibs = sorted({lib for info in objs.values() for lib in info["defaultlib"]})
    libs = {}
    lib_cache_path = os.path.join(CACHE_DIR, "win32-libs.pickle")
    try:
        with open(lib_cache_path, "rb") as f:
            lib_cache = pickle.load(f)
    except (OSError, pickle.PickleError, EOFError):
        lib_cache = {}
    lib_list = [(n, p) for n, p in link_libraries(project, defaultlibs) if n != "Foundation"]
    lib_changed = False
    for name, path in lib_list:
        key = (fwd(os.path.abspath(path)), _stat_key(path))
        hit = lib_cache.get(key[0])
        if hit and hit[0] == key[1]:
            libs[name] = hit[1]
        else:
            libs[name] = read_lib_symbols(path)
            lib_cache[key[0]] = (key[1], libs[name])
            lib_changed = True
    if lib_changed:
        _write_cache(lib_cache_path, lib_cache)
    order = [tu for tu, _ in items if tu in objs]
    return {"objs": objs, "libs": libs, "lib_order": [n for n, _ in lib_list], "order": order,
            "built": built, "missing": missing, "project": project}


# ------------------------------------------------------------------------------------------------
# Policy: what is user side (stubbable) and what is never stubbed
# ------------------------------------------------------------------------------------------------

# Never stubbed, whatever their directory says: the runner itself, the session loader it drives
# (rules, the session Lua state, CWldMap::MapLoad), and the essential TUs M3b ports (spec
# components P, W and N). Their arm64 port is a compile fix, never a stub.
PORT_P = {
    "gpg/core/utils/Global.cpp", "gpg/core/streams/FileStream.cpp", "moho/misc/FileWaitHandleSet.cpp",
    "moho/sim/CVFSImpl.cpp", "moho/path/PathTables.cpp", "moho/misc/StrSafeRuntime.cpp",
}
PORT_W = {
    "gpg/core/reflection/Reflection.cpp", "moho/sim/Sim.cpp", "moho/sim/SimDriver.cpp", "moho/misc/StatItem.cpp",
    "moho/sim/CArmyStats.cpp", "moho/console/CConCommand.cpp", "moho/misc/PausedThread.cpp",
    "moho/misc/ScrPauseEvent.cpp",
}
PORT_N = {
    "moho/sim/WldSessionInfo.cpp", "moho/net/CReplayClient.cpp", "moho/net/CLocalClient.cpp",
    "moho/misc/SessionStartup.cpp", "moho/misc/StartupHelpers.cpp", "moho/resource/ResourceManager.cpp",
    "lua/LuaObject.cpp", "moho/math/MathReflection.cpp", "gpg/core/utils/BoostWrappers.cpp",
    "moho/sim/CArmyLuaFunctionRegistrations.cpp", "moho/sim/CCommandLuaFunctionRegistrations.cpp",
    "moho/core/Thread.cpp",
}
# Ported during M3b integration (component I): user-side TUs whose code the runner executes.
# Common.cpp: CSimDriver measures its own dispatch speed with NetSpeeds (SimDriver.cpp:2024) and the
# client manager and net client keep send stamps in SSendStampBuffer; its sockets stay unused.
# Matrix.cpp: gpg::gal::Math::mul, which sim-side effect emitters call (CEfxEmitter.cpp:443, from
# CEffectImpl::SetBone and CEffectManagerImpl::Tick); off Windows it is the only function built.
PORT_I = {"moho/net/Common.cpp", "gpg/gal/Matrix.cpp"}
# IClientMgrUIInterface: HeadlessReplay.cpp derives its client-manager UI interface from it and
# relies on its no-op slots.
RUNNER_TUS = {"moho/app/HeadlessReplay.cpp", "moho/sim/CWldSessionLoaderImpl.cpp", "moho/net/IClientMgrUIInterface.cpp"}
NEVER_STUB = PORT_P | PORT_W | PORT_N | PORT_I | RUNNER_TUS

# Sim-side TUs that live in user-side directories: the sim creates or calls them, so they are
# linked for real. (Sound requests the sim forwards in its sync packets, camera/transform math,
# the sim's decal handles and debug overlay, the network convars the issue thread reads, and the
# effect emitters sim Lua creates.) WaveSystem and Cartographic parse sections of the .scmap
# stream in CWldTerrainRes::Load (WaveSystem::Load, Cartographic::ReadDecals): the stream is read
# sequentially, so a stub that consumed nothing would misplace every later section of the map. The
# same holds for the decal section (CWldTerrainRes::LoadTexturing -> CDecalManager::Load, which
# reads CDecalGroup and CWldTerrainDecal records).
SIM_SIDE = {
    "moho/terrain/water/WaveSystem.cpp", "moho/render/Cartographic.cpp",
    "moho/terrain/splat/CWldSplat.cpp", "moho/render/CWldTerrainDecal.cpp", "moho/render/CDecalGroup.cpp",
    "moho/audio/CSimSoundManager.cpp", "moho/audio/CSndParams.cpp", "moho/audio/CSndVar.cpp",
    "moho/audio/HSound.cpp", "moho/audio/ISoundManager.cpp", "moho/render/camera/VTransform.cpp",
    "moho/render/camera/GeomCamera3.cpp", "moho/render/CDecalHandle.cpp", "moho/render/CDecalBuffer.cpp",
    "moho/render/CDecalTypes.cpp", "moho/render/RDebugOverlay.cpp", "moho/net/NetConVars.cpp",
}
SIM_SIDE_PREFIXES = ("moho/effects/rendering/",)

# Classes whose key function (and with it the Itanium vtable and typeinfo) the runner's stand-ins
# define, because the class's own TU cannot be linked. A TU that needs the vtable or typeinfo of one
# of these does not depend on that TU; the arm64 link checks that the stand-in provides them.
#   RD3DTextureResource: RD3DTextureResource.cpp includes the D3D9 backend. The stand-ins
#   (port/engine/runner/HeadlessStubsTexture.cpp) give what the class does without a gal device,
#   so its RType (RD3DTextureResourceTypeInfo.cpp), the "d3d_textures" prefetch kind and the texture
#   factory (CD3DTextureResourceFactory.cpp) are registered as on Windows.
RUNNER_KEY_CLASSES = {"moho::RD3DTextureResource": "port/engine/runner/HeadlessStubsTexture.cpp"}

NET_LIVE = {"CGpgNetInterface", "CHostManager", "CLobby", "CLobbyTypeInfo", "CNetDatagramSocketImpl", "CNetTCPBuf",
            "CNetTCPConnection", "CNetTCPConnector", "CNetTCPServerImpl", "CNetUDPConnection", "CNetUDPConnector",
            "CDiscoveryService", "CDiscoveryServiceTypeInfo", "INetNATTraversalProvider",
            "INetNATTraversalProviderTypeInfo", "INetNATTraversalProviderWeakPtrReflection", "Common",
            "INetTCPServer", "INetTCPSocket", "INetDatagramSocket", "NetConVars", "IClientMgrUIInterface"}
WX_DEBUG = {"ScrDebugWindow", "ScrFileCtrl", "ScrGotoDialog", "ScrSourceCtrl", "ScrWatchCtrl", "ScrPauseEvent",
            "ScrDebugHooks", "TreeData"}
# The user's session and its world mirror (CWldSession, UserUnit, UserEntity and what only they
# use), plus the disk watcher (live reload of edited files) and save-game requests, which only a
# user starts.
USER_SESSION = {
    "moho/sim/CWldSession.cpp": "the user's world session (WLD_GetActiveSession is null in the runner)",
    "moho/unit/core/UserUnit.cpp": "user-side unit mirror",
    "moho/unit/core/UserUnitTypeInfo.cpp": "reflection of the user-side unit mirror",
    "moho/entity/UserEntity.cpp": "user-side entity mirror",
    "moho/sim/UserArmy.cpp": "user-side army mirror",
    "moho/sim/IdleUnitSelector.cpp": "UI idle-unit selection",
    "moho/misc/TimeBar.cpp": "UI time bar",
    "moho/script/ScriptedDecal.cpp": "user-side decal",
    "moho/misc/CDiskWatch.cpp": "file-change watcher for live reload (dev tool, no events in a runner)",
    "moho/misc/CSaveGameRequestImpl.cpp": "save-game request, started by the user",
}
STUB_CATEGORIES = {
    "ui": "game UI (moho/ui)",
    "render": "renderer (moho/render, gpg/gal, mesh, terrain, particles, effect rendering)",
    "audio": "sound engine (moho/audio)",
    "movie": "movies (moho/movie, cri)",
    "app-wx": "application shell and wx (moho/app)",
    "wx-debug": "Lua debugger windows (wx)",
    "net-live": "live networking (sockets, lobby, GPGnet)",
    "user-session": "user session",
}
# Libraries whose symbols cannot exist on Android: a user-side TU that calls them directly is not
# linked even when it compiles (the shim declares some of these APIs, nothing defines them).
PLATFORM_LIBS = {"wxmswu", "d3d9", "d3dx9", "d3d10", "dxgi", "Dx9Renderer", "Dx9Application", "dsound",
                 "x3daudio", "ws2_32"}


def category(tu):
    """The user-side category of a TU, or None for core/sim code."""
    if tu.startswith("../"):
        return None
    if tu in USER_SESSION:
        return "user-session"
    base = os.path.splitext(os.path.basename(tu))[0]
    if tu.startswith("moho/ui/"):
        return "ui"
    if tu.startswith(("moho/render/", "gpg/gal/", "moho/mesh/", "moho/terrain/", "moho/particles/",
                      "moho/effects/rendering/")) or tu == "particles/SParticleBuffer.cpp":
        return "render"
    if tu.startswith("moho/audio/"):
        return "audio"
    if tu.startswith(("moho/movie/", "cri/")):
        return "movie"
    if tu.startswith("moho/app/"):
        return "app-wx"
    if tu.startswith("moho/misc/") and base in WX_DEBUG:
        return "wx-debug"
    if tu.startswith("moho/net/") and base in NET_LIVE:
        return "net-live"
    return None


def is_sim_side(tu):
    return tu in SIM_SIDE or tu.startswith(SIM_SIDE_PREFIXES)


def stubbable(tu):
    """True when the stub rule allows replacing this TU's symbols with runner-only stubs."""
    return category(tu) is not None and tu not in NEVER_STUB and not is_sim_side(tu)


def owner_component(tu, closure, arm_status):
    """Which M3b component answers for undefined symbols this TU defines on Windows."""
    if tu is None:
        return "P"
    if tu.startswith(WM3_PREFIX):
        return "G"
    if tu in PORT_P:
        return "P"
    if tu in PORT_W:
        return "W"
    if tu in PORT_N:
        return "N"
    if tu in PORT_I:
        return "I"
    if tu not in closure:
        return "S"
    st = arm_status.get(tu) or {}
    cls = (st.get("class") or "")
    if "wxWidgets" in cls:
        return "W"
    if "Win32" in cls or "Windows SDK" in cls:
        return "P"
    if st.get("ok") is False:
        return "N"
    return "I"


# ------------------------------------------------------------------------------------------------
# The symbol graph
# ------------------------------------------------------------------------------------------------

# What a static initialiser can register, by the symbol its TU references (Win32 mangling). Every
# kind fills a process-wide registry that nothing else fills, so a TU missing from the ELF link loses
# its entries without any other symptom:
#   lua         Lua binders, class binders and init forms (CScrLuaInitFormSet)
#   rtype       reflected types (PreRegisterRType, or a .CRT$XCL pre-registration section)
#   convar      console commands and variables: CConCommand's constructor (TConVar<T> inlines it),
#               RegisterConCommand, and CConFunc, whose constructor lives in CConCommand.cpp
#   serializer  serializer/construct helpers (gpg::SerHelperBase's constructor queues the helper;
#               SerHelperBase::InitNewHelpers binds its load/save/construct callbacks onto the RType
#               when the first archive is created)
#   prefetch    prefetch kinds (RES_RegisterPrefetchType: the names CPrefetchSet:Update accepts)
#   factory     resource factories (ResourceFactoryBase's constructor attaches the factory to the
#               resource manager, which loads and prefetches that resource type only through it)
REG_KINDS = (
    ("lua", re.compile(r"^\?\?0CScrLua(Binder|ClassBinder|BaseClassSpec|InitForm|InitFormSet)@moho@@")),
    ("rtype", re.compile(r"^\?PreRegisterRType@gpg@@")),
    ("convar", re.compile(r"^(\?\?0CConCommand@moho@@|\?RegisterConCommand@moho@@|\?\?0CConFunc@moho@@)")),
    ("serializer", re.compile(r"^\?\?0SerHelperBase@gpg@@")),
    ("prefetch", re.compile(r"^\?RES_RegisterPrefetchType@moho@@")),
    ("factory", re.compile(r"^\?\?0ResourceFactoryBase@moho@@")),
)
REG_ALL = frozenset(k for k, _ in REG_KINDS)


class Graph:
    def __init__(self, db, arm_status):
        self.db = db
        self.objs = db["objs"]
        self.order = db["order"]
        self.arm = arm_status
        self.defs = collections.defaultdict(list)  # name -> [(tu, sel)]
        for tu in self.order:
            for name, sel in self.objs[tu]["defs"].items():
                self.defs[name].append((tu, sel))
        self.ext = {}
        for lib in db["lib_order"]:
            for s in db["libs"].get(lib, ()):
                self.ext.setdefault(s, lib)
        self.ext_by_tu = collections.defaultdict(set)
        for tu in self.order:
            for s in self.objs[tu]["undef"]:
                if s not in self.defs and s in self.ext:
                    self.ext_by_tu[tu].add(self.ext[s])
        self.registers = {tu: self._registrations(tu) for tu in self.order}
        self.key_tu = self._key_function_tus()
        self.rtti = {tu: self._rtti_needs(tu) for tu in self.order}
        for tu in self.order:
            for pseudo, key in self.rtti[tu].items():
                if pseudo not in self.defs:
                    self.defs[pseudo] = [(key, 0)]
        # A user-side TU is unlinkable when it does not compile for arm64 or calls a platform
        # library, and also when it needs the vtable or typeinfo of a class whose key function lives
        # in an unlinkable TU: no stub can provide those.
        self._bad = {t for t in self.order if stubbable(t) and (not self.arm_ok(t) or self.platform(t))}
        changed = True
        while changed:
            changed = False
            for t in self.order:
                if t not in self._bad and stubbable(t) and any(k in self._bad for k in self.rtti[t].values()):
                    self._bad.add(t)
                    changed = True

    def _key_function_tus(self):
        """{class: TU} for every class with a non-inline virtual member function on Win32.

        MSVC emits a class's vftable and RTTI as COMDATs in every TU that needs them; the Itanium
        ABI emits the vtable and typeinfo only in the TU that defines the class's key function (its
        first non-inline virtual function), and every other TU references them there. So a TU that
        constructs a class or uses typeid/dynamic_cast/throw/catch on it depends, on Android, on
        the TU that defines the class's virtual functions - an edge the Win32 objects do not show.
        Several such TUs: the one named after the class, else the one defining the most of them."""
        cands = {}
        for tu in self.order:
            for name, sel in self.objs[tu]["defs"].items():
                if sel in (0, 1) and name.startswith("?") and not name.startswith("??_"):
                    cands.setdefault(name, []).append(tu)
        full = undname(cands, "0x0")
        qual = undname(cands, "0x1000")
        by_class = collections.defaultdict(collections.Counter)
        for name, tus in cands.items():
            f = full.get(name, "")
            if ": virtual " not in f or f.startswith("[thunk]"):
                continue
            q = qual.get(name, name)
            if "::" not in q:
                continue
            cls = norm_qual(q.rsplit("::", 1)[0])
            for t in tus:
                by_class[cls][t] += 1
        key = {}
        for cls, counter in by_class.items():
            last = strip_templates(cls).rsplit("::", 1)[-1]
            named = sorted(t for t in counter if os.path.splitext(os.path.basename(t))[0] == last)
            key[cls] = named[0] if named else sorted(counter, key=lambda t: (-counter[t], t))[0]
        return key

    def _rtti_needs(self, tu):
        """{pseudo symbol: key-function TU} for the vtables and typeinfos `tu` needs on Android."""
        info = self.objs[tu]
        names = list(info.get("vft", ())) + list(info.get("td_uses", ()))
        if not names:
            return {}
        q = undname(names, "0x1000")
        needs = {}
        for n in names:
            text = q.get(n, n)
            if n.startswith("??_7"):
                cls = text.split("::`vftable'")[0]
            else:
                cls = text.split(" `RTTI Type Descriptor'")[0].replace("const", "").rstrip(" *&")
            cls = norm_qual(cls)
            if cls in RUNNER_KEY_CLASSES:
                continue
            key = self.key_tu.get(cls)
            if key is not None and key != tu:
                needs[f"<vtable/typeinfo of {cls}>"] = key
        return needs

    def refs(self, tu):
        """Everything `tu` needs from other TUs: its undefined symbols and its RTTI needs."""
        return list(self.objs[tu]["undef"]) + list(self.rtti[tu])

    def arm_ok(self, tu):
        st = self.arm.get(tu)
        return True if st is None else bool(st.get("ok"))

    def platform(self, tu):
        return bool(self.ext_by_tu[tu] & PLATFORM_LIBS)

    def bad(self, tu):
        """A TU the runner must not link: user side, and it cannot be linked on Android."""
        return tu in self._bad

    def why_bad(self, tu):
        """Why `tu` cannot be linked, for the report ('' when it can)."""
        if tu not in self._bad:
            return "" if stubbable(tu) or tu.startswith("../") else "(not user side)"
        reasons = []
        if not self.arm_ok(tu):
            st = self.arm.get(tu) or {}
            reasons.append("does not compile for arm64" + (f" ({st['class']})" if st.get("class") else ""))
        if self.platform(tu):
            reasons.append("calls " + ", ".join(sorted(self.ext_by_tu[tu] & PLATFORM_LIBS)))
        rtti = sorted((p[len("<vtable/typeinfo of "):-1], k) for p, k in self.rtti[tu].items() if k in self._bad)
        if rtti:
            reasons.append("needs the vtable/typeinfo of " +
                           ", ".join(f"{c} (key function in {k})" for c, k in rtti))
        return "; ".join(reasons)

    def _registrations(self, tu):
        info = self.objs[tu]
        kinds = set()
        if not info["init"]:
            return kinds
        names = list(info["undef"]) + list(info["defs"])
        for kind, pattern in REG_KINDS:
            if any(pattern.match(n) for n in names):
                kinds.add(kind)
        if any(n.startswith(".CRT$XCL") for n, _c in info["init"]):
            kinds.add("rtype")
        return kinds

    def prefer(self, tu):
        return (stubbable(tu), not self.arm_ok(tu), self.platform(tu), tu)

    def pick(self, defs, included=()):
        tus = {d[0] for d in defs}
        for t in tus:
            if t in included:
                return t
        strong = [d[0] for d in defs if d[1] in (0, 1)]
        return min(strong or tus, key=self.prefer)

    def resolve(self, name):
        d = self.defs.get(name)
        if d:
            return "eng", d
        if name in self.ext:
            return "ext", self.ext[name]
        return "missing", None

    def closure(self, roots, cut=frozenset()):
        """TUs linked as whole objects: every undefined symbol of an included TU is resolved."""
        included, parent, work = set(), {}, []
        for t in roots:
            if t not in included:
                included.add(t)
                parent[t] = None
                work.append(t)
        while work:
            a = work.pop()
            for s in self.refs(a):
                if s in cut:
                    continue
                kind, v = self.resolve(s)
                if kind != "eng":
                    continue
                b = self.pick(v, included)
                if b not in included:
                    included.add(b)
                    parent[b] = (a, s)
                    work.append(b)
        return included, parent

    def roots(self, variant):
        """{root TU: why}. Every variant starts at HEADLESS_RunReplay and adds the static-initialiser
        TUs that can be linked: `lua` those that construct Lua binders, `reg` also those that
        register anything else (REG_KINDS: RTypes, console commands/variables, serializer helpers,
        prefetch kinds, resource factories), `init` also every other non-user-side TU with a static
        initialiser of any kind."""
        root_tu = self.defs.get(ROOT_SYMBOL)
        if not root_tu:
            raise SystemExit(f"link_closure: no object defines {ROOT_SYMBOL} (is HeadlessReplay.cpp built?)")
        kinds = {"lua"} if variant == "lua" else set(REG_ALL)
        out = {root_tu[0][0]: "entry"}
        for tu in self.order:
            # Third-party code (boost.thread, WildMagic) joins only where the engine needs it:
            # WildMagic's own static initialisers register its scene-graph classes, which the
            # engine never uses. A user-side TU that registers something is a root too when it can
            # be linked, so its registrations are the same as on Windows (the TypeInfo TUs of the
            # sim-side sound, decal and emitter types live in user-side directories). Only the TUs
            # that cannot be linked lose their registrations; the report lists them.
            if self.bad(tu) or tu in out or tu.startswith("../"):
                continue
            reg = self.registers[tu] & kinds
            if reg:
                out[tu] = "+".join(sorted(reg))
            elif variant == "init" and not stubbable(tu) and self.objs[tu]["init"]:
                out[tu] = "static-init"
        return out


class Dinic:
    def __init__(self):
        self.adj = collections.defaultdict(list)
        self.to, self.cap = [], []

    def add(self, u, v, c):
        self.adj[u].append(len(self.to)); self.to.append(v); self.cap.append(c)
        self.adj[v].append(len(self.to)); self.to.append(u); self.cap.append(0)

    def _bfs(self, s, t):
        self.level = {s: 0}
        q = collections.deque([s])
        while q:
            u = q.popleft()
            for e in self.adj[u]:
                if self.cap[e] > 0 and self.to[e] not in self.level:
                    self.level[self.to[e]] = self.level[u] + 1
                    q.append(self.to[e])
        return t in self.level

    def _dfs(self, s, t):
        """One augmenting path along the level graph, iteratively (graphs are deep)."""
        stack, path = [s], []
        while stack:
            u = stack[-1]
            if u == t:
                f = min(self.cap[e] for e in path)
                for e in path:
                    self.cap[e] -= f
                    self.cap[e ^ 1] += f
                return f
            advanced = False
            edges = self.adj[u]
            while self.it[u] < len(edges):
                e = edges[self.it[u]]
                v = self.to[e]
                if self.cap[e] > 0 and self.level.get(v, -1) == self.level[u] + 1:
                    stack.append(v)
                    path.append(e)
                    advanced = True
                    break
                self.it[u] += 1
            if not advanced:
                stack.pop()
                if path:
                    path.pop()
                    self.it[stack[-1]] += 1
        return 0

    def maxflow(self, s, t):
        flow = 0
        while self._bfs(s, t):
            self.it = collections.defaultdict(int)
            while True:
                f = self._dfs(s, t)
                if not f:
                    break
                flow += f
        return flow

    def reach(self, s):
        seen, work = {s}, [s]
        while work:
            u = work.pop()
            for e in self.adj[u]:
                if self.cap[e] > 0 and self.to[e] not in seen:
                    seen.add(self.to[e])
                    work.append(self.to[e])
        return seen


INF = 10 ** 9


def min_cut(g, roots):
    """The fewest symbols defined by stubbable TUs whose removal keeps every bad TU unreachable.

    Network: S -> root TU (inf), TU -> each symbol it references (inf), symbol -> the TU that
    would define it (1 when that TU is stubbable, so the cut is a stub; inf otherwise), bad TU -> T.
    A bad TU is stubbable, so every path into it ends in a capacity-1 edge and the cut exists."""
    D = Dinic()
    for r in roots:
        D.add("S", ("tu", r), INF)
    symdef = {}
    for a in g.order:
        for s in g.refs(a):
            kind, v = g.resolve(s)
            if kind != "eng":
                continue
            if s not in symdef:
                b = g.pick(v)
                symdef[s] = b
                if s.startswith("<"):
                    # A vtable/typeinfo cannot come from a stub: the class's key-function TU is
                    # linked whenever it can be, and the edge is cut only into an unlinkable one
                    # (then the referencing site needs a guard; the report lists these).
                    D.add(("sym", s), ("tu", b), 1 if g.bad(b) else INF)
                else:
                    D.add(("sym", s), ("tu", b), 1 if stubbable(b) else INF)
            D.add(("tu", a), ("sym", s), INF)
    bad = [t for t in g.order if g.bad(t)]
    for t in bad:
        D.add(("tu", t), "T", INF)
    flow = D.maxflow("S", "T")
    if flow >= INF:
        raise SystemExit("link_closure: a bad TU is reachable without a stubbable edge (policy error)")
    R = D.reach("S")
    cut = sorted(s for s, b in symdef.items() if ("sym", s) in R and ("tu", b) not in R)
    return flow, cut, symdef


def compute(g, variant):
    roots = g.roots(variant)
    flow, cut, symdef = min_cut(g, roots)
    cut_set = frozenset(cut)
    closure, parent = g.closure(roots, cut_set)
    leaked = sorted(t for t in closure if g.bad(t))
    if leaked:
        raise SystemExit("link_closure: the cut leaks bad TUs: " + ", ".join(leaked))
    return {"variant": variant, "roots": roots, "cut": cut, "flow": flow, "closure": closure,
            "parent": parent, "symdef": symdef}


# ------------------------------------------------------------------------------------------------
# Demangling and the owner index (shared with build_runner.py)
# ------------------------------------------------------------------------------------------------

def find_undname():
    for pat in (r"C:/Program Files*/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/undname.exe",
                r"C:/Program Files*/Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx86/x86/undname.exe"):
        hits = sorted(glob.glob(pat))
        if hits:
            return hits[-1]
    return None


_UNDNAME_CACHES = {}


def undname(names, flags="0x1000"):
    """{mangled: demangled} through MSVC's undname (0x1000: the qualified name only), cached."""
    names = sorted(set(n for n in names if n))
    if not names:
        return {}
    os.makedirs(CACHE_DIR, exist_ok=True)
    cache_path = os.path.join(CACHE_DIR, f"undname-{flags}.pickle")
    cache = _UNDNAME_CACHES.get(flags)
    if cache is None:
        try:
            with open(cache_path, "rb") as f:
                cache = pickle.load(f)
        except (OSError, pickle.PickleError, EOFError):
            cache = {}
        _UNDNAME_CACHES[flags] = cache
    todo = [n for n in names if n not in cache]
    tool = find_undname() if todo else None
    if todo and tool is None:
        # The closure depends on it (virtual functions, class names), so no silent fallback.
        raise SystemExit("link_closure: MSVC's undname.exe not found (Visual Studio 2022, VC tools)")
    for i in range(0, len(todo), 20000):
        chunk = todo[i:i + 20000]
        path = os.path.join(CACHE_DIR, f"undname-in.{os.getpid()}.txt")
        with open(path, "w", encoding="latin-1", newline="\n") as f:
            f.write("\n".join(chunk) + "\n")
        p = subprocess.run([tool, flags, path], capture_output=True, text=True, encoding="latin-1")
        os.remove(path)
        out = p.stdout.splitlines()
        if len(out) != len(chunk):
            raise SystemExit(f"link_closure: undname returned {len(out)} lines for {len(chunk)} names")
        for n, d in zip(chunk, out):
            cache[n] = d.strip()
    if todo:
        _write_cache(cache_path, cache)
    return {n: cache.get(n, n) for n in names}


_KEYWORDS = re.compile(r"\b(class|struct|enum|union)\s+")
_TEMPLATE = re.compile(r"<[^<>]*>")


def norm_qual(q):
    """A qualified name as both demanglers can agree on it: no class/struct keywords, no spaces,
    bool template arguments as 1/0."""
    q = _KEYWORDS.sub("", q).replace(" ", "")
    return q.replace("<true>", "<1>").replace(",true>", ",1>").replace("<false>", "<0>").replace(",false>", ",0>")


def strip_templates(q):
    prev = None
    while prev != q:
        prev, q = q, _TEMPLATE.sub("", q)
    return q


def qual_of_itanium(s):
    """The qualified name of an Itanium-demangled symbol (lld's spelling), without parameters."""
    for prefix in ("vtable for ", "typeinfo for ", "typeinfo name for ", "VTT for ", "construction vtable for ",
                   "guard variable for ", "TLS wrapper function for ", "thread-local initialization routine for "):
        if s.startswith(prefix):
            rest = s[len(prefix):]
            if prefix.startswith("vtable") or prefix.startswith("typeinfo") or prefix.startswith("VTT"):
                return rest + "::`vftable'"
            return rest
    depth = 0
    for j, ch in enumerate(s):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "(" and depth == 0 and not s[:j].endswith("operator"):
            return s[:j]
    return s


def owner_index(g):
    """Normalised qualified names of the Win32 definitions -> TUs, for mapping arm64 link errors to
    their Windows owner: strong definitions first, then COMDATs (explicit template instantiations
    are COMDAT on MSVC), each also without template arguments as a fallback."""
    names = [n for n in g.defs if not n.startswith("<")]
    dm = undname(names)
    maps = [collections.defaultdict(set) for _ in range(4)]  # strong exact/loose, any exact/loose
    for n in names:
        q = dm.get(n, n)
        if q.startswith("_") and not n.startswith("?"):
            q = q[1:].split("@")[0]  # C name: _name or _name@N (stdcall)
        nq = norm_qual(q)
        strong = {t for t, sel in g.defs[n] if sel in (0, 1, -1)}
        anyd = {t for t, _sel in g.defs[n]}
        maps[0][nq] |= strong
        maps[1][strip_templates(nq)] |= strong
        maps[2][nq] |= anyd
        maps[3][strip_templates(nq)] |= anyd
    return maps


def lookup_owner(index, g, demangled):
    """The Windows TU that defines an arm64 symbol (lld's demangled spelling), or None. A vtable or
    typeinfo belongs to the class's key-function TU."""
    q = norm_qual(qual_of_itanium(demangled))
    if q.endswith("::`vftable'"):
        key = g.key_tu.get(q[: -len("::`vftable'")])
        if key:
            return key
    for m, k in ((index[0], q), (index[1], strip_templates(q)), (index[2], q), (index[3], strip_templates(q))):
        tus = {t for t in m.get(k, ()) if t}
        if tus:
            return min(tus, key=lambda t: (stubbable(t), t))
    if q.endswith("::`vftable'"):
        cls = strip_templates(q[: -len("::`vftable'")]) + "::"
        tus = {t for k, v in index[1].items() if k.startswith(cls) for t in v}
        if tus:
            return min(tus, key=lambda t: (stubbable(t), t))
    return None


# ------------------------------------------------------------------------------------------------
# Inputs: compile status
# ------------------------------------------------------------------------------------------------

def load_arm_status(sweep):
    path = sweep if sweep.endswith(".json") else os.path.join(sweep, "results.json")
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    out = {}
    for r in data["tus"]:
        out[r["tu"]] = {"ok": r["ok"], "class": r.get("class"), "first_error": r.get("first_error")}
    return out, data.get("meta", {})


def read_closure_file(path):
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            s = line.split("#", 1)[0].strip()
            if s:
                out.append(s)
    return out


# ------------------------------------------------------------------------------------------------
# Report
# ------------------------------------------------------------------------------------------------

def md(s):
    return str(s).replace("|", "\\|").replace("<", "&lt;").replace(">", "&gt;")


def code(s):
    return "`" + str(s).replace("`", "'").replace("|", "\\|") + "`"


def subsystem(tu):
    parts = es.artifact_rel(tu).split("/")
    return "/".join(parts[:2]) if len(parts) > 2 else parts[0]


def write_outputs(g, results, chosen, args, arm_meta, closure_path, report_dir):
    res = results[chosen]
    closure = res["closure"]
    ordered = [t for t in g.order if t in closure and not t.startswith(WM3_PREFIX)]
    third = sorted(t for t in closure if t.startswith(WM3_PREFIX))
    header = [
        "# Link closure of the Android headless replay runner (libfafengine.so).",
        "# Generated by scripts/port/link_closure.py from the Debug|Win32 objects; do not edit by hand.",
        "# One TU per line, relative to src/sdk: the engine TUs in main.vcxproj order (MSVC's link and",
        "# static-initialiser order), then the third-party TUs (WildMagic Foundation).",
        f"# Roots: HEADLESS_RunReplay plus the static-initialiser TUs of variant '{chosen}'"
        f" ({len(res['roots'])} roots).",
        f"# {len(ordered)} engine TUs, {len(third)} third-party TUs, {len(res['cut'])} symbols cut into stubs.",
    ]
    text = "\n".join(header + ordered + third) + "\n"
    if args.check:
        try:
            with open(closure_path, encoding="utf-8") as f:
                current = f.read()
        except OSError:
            current = ""
        if current.replace("\r\n", "\n") != text:
            old = set(read_closure_file(closure_path)) if current else set()
            new = set(ordered + third)
            print(f"link_closure: {fwd(closure_path)} is out of date: +{len(new - old)} -{len(old - new)} TUs",
                  file=sys.stderr)
            for t in sorted(new - old)[:30]:
                print(f"  + {t}", file=sys.stderr)
            for t in sorted(old - new)[:30]:
                print(f"  - {t}", file=sys.stderr)
            return 1
        print(f"link_closure: {fwd(closure_path)} is up to date ({len(ordered)} + {len(third)} TUs)", file=sys.stderr)
        return 0
    os.makedirs(os.path.dirname(closure_path), exist_ok=True)
    with open(closure_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)

    os.makedirs(report_dir, exist_ok=True)
    cut = res["cut"]
    cut_set = set(cut)
    dm_full = undname(cut, "0x2")
    # Every reference from the closure that crosses into a TU outside it.
    crossing = collections.defaultdict(lambda: collections.defaultdict(set))  # target -> sym -> refs
    missing = collections.defaultdict(set)
    libuse = collections.defaultdict(lambda: collections.defaultdict(set))   # lib -> tu -> syms
    for a in closure:
        for s in g.refs(a):
            kind, v = g.resolve(s)
            if kind == "eng":
                if not any(t in closure for t, _sel in v):
                    crossing[g.pick(v)][s].add(a)
            elif kind == "ext":
                libuse[v][a].add(s)
            else:
                missing[s].add(a)
    dm_cross = undname([s for d in crossing.values() for s in d], "0x2")
    # Strong duplicates among closure TUs (MSVC links them only because of /FORCE).
    dups = []
    for name, defs in g.defs.items():
        strong = sorted({t for t, sel in defs if sel in (0, 1) and t in closure})
        if len(strong) > 1:
            dups.append((name, strong))
    dups.sort()
    dm_dups = undname([n for n, _ in dups], "0x2")

    non_compiling = [t for t in ordered if g.arm.get(t) is not None and not g.arm[t]["ok"]]
    unknown = [t for t in ordered + third if g.arm.get(t) is None]
    reg_left_out = [(t, g.registers[t]) for t in g.order if g.registers[t] and t not in closure]

    L = []
    L.append("# Headless runner link closure\n")
    L.append("Generated by `scripts/port/link_closure.py`; see `port/engine/runner/README.md`.\n")
    L.append(f"**{len(ordered)} engine TUs + {len(third)} WildMagic TUs** (variant `{chosen}`), "
             f"{len(cut)} symbols cut into stubs, {len(non_compiling)} closure TUs that do not compile for arm64 "
             f"in `{fwd(os.path.relpath(args.sweep, REPO_ROOT))}`.\n")
    L.append("| | |\n|---|---|")
    L.append(f"| date | {datetime.datetime.now().astimezone().isoformat(timespec='seconds')} |")
    L.append(f"| git | {md(es.git('rev-parse', '--short', 'HEAD'))} |")
    L.append(f"| objects | {code(fwd(os.path.relpath(args.objdir, REPO_ROOT)))} ({len(g.order)} read, "
             f"{len(g.db['missing'])} missing) |")
    L.append(f"| arm64 status | {code(fwd(os.path.relpath(args.sweep, REPO_ROOT)))} ({md(arm_meta.get('git', '?'))}) |")
    L.append(f"| libraries | {md(', '.join(g.db['lib_order']))} |")
    L.append("")
    if g.db["missing"]:
        L.append("Objects missing (build Debug|Win32 first): " +
                 ", ".join(code(t) for t, _ in g.db["missing"][:20]) + "\n")

    L.append("## Variants\n")
    L.append("Roots are `HEADLESS_RunReplay`'s TU plus the static-initialiser TUs outside the user side: "
             "`lua` those that construct Lua binders, `reg` also those that pre-register RTypes or "
             "construct console commands/variables (the spec's definition), `init` every TU with any "
             "static initialiser.\n")
    L.append("| variant | roots | closure TUs | cut symbols | not compiling | user-side TUs linked |\n"
             "|---|---:|---:|---:|---:|---:|")
    for v, r in results.items():
        c = r["closure"]
        L.append(f"| `{v}`{' (chosen)' if v == chosen else ''} | {len(r['roots'])} | {len(c)} | {len(r['cut'])} | "
                 f"{sum(1 for t in c if g.arm.get(t) is not None and not g.arm[t]['ok'])} | "
                 f"{sum(1 for t in c if category(t))} |")
    L.append("")
    for v, r in results.items():
        if v == chosen:
            continue
        extra = sorted(res["closure"] - r["closure"])
        fewer = sorted(r["closure"] - res["closure"])
        L.append(f"`{chosen}` vs `{v}`: +{len(extra)} / -{len(fewer)} TUs.")
        if fewer:
            L.append(f"<details><summary>In `{v}` but not in `{chosen}` ({len(fewer)})</summary>\n")
            L += [f"- `{t}` ({', '.join(sorted(g.registers[t])) or 'no registration'})" for t in fewer]
            L.append("\n</details>\n")
        if extra:
            L.append(f"<details><summary>In `{chosen}` but not in `{v}` ({len(extra)})</summary>\n")
            L += [f"- `{t}` ({', '.join(sorted(g.registers[t])) or 'no registration'})" for t in extra]
            L.append("\n</details>\n")
    L.append("")

    L.append("## Closure by subsystem\n")
    by_sub = collections.Counter(subsystem(t) for t in closure)
    L.append("| subsystem | TUs | not compiling |\n|---|---:|---:|")
    for sub, n in sorted(by_sub.items()):
        nc = sum(1 for t in closure if subsystem(t) == sub and g.arm.get(t) is not None and not g.arm[t]["ok"])
        L.append(f"| {sub} | {n} | {nc} |")
    L.append("")
    cats = collections.Counter(category(t) for t in closure if category(t))
    if cats:
        L.append("User-side TUs that compile and are linked for real (no stub needed): " +
                 ", ".join(f"{k} {v}" for k, v in sorted(cats.items())) + "\n")
        L.append("<details><summary>List</summary>\n")
        L += [f"- `{t}` ({category(t)})" for t in ordered if category(t)]
        L.append("\n</details>\n")

    L.append("## Closure TUs that do not compile for arm64\n")
    L.append("| TU | owner | first-error class | first error |\n|---|---|---|---|")
    for t in non_compiling:
        st = g.arm[t]
        fe = st.get("first_error") or {}
        L.append(f"| `{t}` | {owner_component(t, closure, g.arm)} | {md(st.get('class') or '')} | "
                 f"{code(str(fe.get('file', '?')) + ':' + str(fe.get('line', 0)))} {md((fe.get('message') or '')[:100])} |")
    L.append("")
    if unknown:
        L.append("Not in the sweep (status unknown, assumed to compile): " + ", ".join(code(t) for t in unknown) + "\n")

    rtti_cuts = sorted((s, tgt, sorted(r)) for tgt, d in crossing.items() for s, r in d.items() if s.startswith("<"))
    L.append("## Vtables and typeinfos of unlinkable classes\n")
    L.append("Closure TUs that construct one of these classes or use typeid/dynamic_cast/throw/catch on it. "
             "On Android its vtable and typeinfo exist only in the class's key-function TU, which cannot be "
             "linked; a stub cannot provide them. The referencing site needs a guard (or the class's TU a "
             "port).\n")
    if rtti_cuts:
        L.append("| class (key-function TU) | referenced by |\n|---|---|")
        for s, tgt, refs in rtti_cuts:
            L.append(f"| {code(s[len('<vtable/typeinfo of '):-1])} (`{tgt}`) | "
                     f"{md(', '.join(os.path.basename(x) for x in refs))} |")
        L.append("")
    else:
        L.append("None.\n")

    L.append("## References into stubbed TUs\n")
    L.append("What runner-only stubs (`port/engine/runner/HeadlessStubs*.cpp`) have to define, from the "
             "Win32 objects: every symbol a closure TU references whose only definitions are in TUs "
             "outside the closure. The arm64 link (`build_runner.py`) is authoritative; guards in the "
             "ported TUs remove some of these references, the min-cut's symbols are marked *cut*.\n")
    for tgt in sorted(crossing, key=lambda t: (-len(crossing[t]), t)):
        syms = crossing[tgt]
        L.append(f"### `{tgt}` ({category(tgt) or 'core'}, {'compiles' if g.arm_ok(tgt) else 'does not compile'}"
                 f"{', platform libs' if g.platform(tgt) else ''}): {len(syms)} symbols\n")
        for s in sorted(syms, key=lambda s: (-len(syms[s]), s)):
            refs = ", ".join(sorted(os.path.basename(x) for x in syms[s]))
            L.append(f"- {'*cut* ' if s in cut_set else ''}{code(dm_cross.get(s, s)[:160])} <- {md(refs[:200])}")
        L.append("")

    L.append("## Libraries the closure references on Windows\n")
    L.append("| library | TUs | symbols | e.g. |\n|---|---:|---:|---|")
    for lib in sorted(libuse, key=lambda l: -len(libuse[l])):
        tus = libuse[lib]
        allsyms = sorted({s for ss in tus.values() for s in ss})
        L.append(f"| {lib} | {len(tus)} | {len(allsyms)} | {md(', '.join(allsyms[:4])[:120])} |")
    L.append("")
    plat = sorted(t for t in closure if g.platform(t))
    if plat:
        L.append("Closure TUs that call a platform library directly on Windows (wx, D3D, DirectSound, "
                 "Winsock); their arm64 port must guard these calls or the shim must define them:\n")
        L += [f"- `{t}`: {', '.join(sorted(g.ext_by_tu[t] & PLATFORM_LIBS))}" for t in plat]
        L.append("")
    if missing:
        L.append(f"Symbols no object or library defines ({len(missing)}): " +
                 ", ".join(code(s) for s in sorted(missing)[:20]) + "\n")

    L.append("## Duplicate strong definitions inside the closure\n")
    L.append("MSVC links these only because of `/FORCE` (it keeps the first in link order); an ELF link "
             "rejects them.\n")
    for name, tus in dups:
        L.append(f"- {code(dm_dups.get(name, name)[:140])}: " + ", ".join(code(t) for t in tus))
    L.append("")

    L.append("## Registration TUs outside the closure\n")
    L.append("Every TU whose static initialisers register something (kinds: "
             + ", ".join(f"`{k}`" for k, _ in REG_KINDS) + "; see `REG_KINDS`) and that is not "
             "linked. They are all user side and cannot be linked on Android; their entries are "
             "missing from the Android runner's registry (`/headlessregistry`) and nowhere else.\n")
    L.append("| TU | registers | category | arm64 | why it cannot be linked |\n|---|---|---|---|---|")
    for t, reg in reg_left_out:
        L.append(f"| `{t}` | {', '.join(sorted(reg))} | {category(t) or 'core'} | "
                 f"{'ok' if g.arm_ok(t) else 'fails'}{', platform' if g.platform(t) else ''} | "
                 f"{md(g.why_bad(t))} |")
    L.append("")
    quiet_init = [t for t in g.order if g.objs[t]["init"] and t not in closure and not t.startswith("../")
                  and not g.registers[t] and not g.bad(t)]
    L.append("## Linkable TUs with static initialisers that register nothing\n")
    L.append("Outside the closure, linkable, with static initialisers that fill none of the registries "
             "above (header statics such as `VMatrix4::sIdentity`, render-only globals). Leaving them "
             "out changes no registry; nothing in the closure needs them.\n")
    L.append(", ".join(f"`{t}`" for t in quiet_init) + "\n" if quiet_init else "None.\n")

    with open(os.path.join(report_dir, "report.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))
    data = {
        "variant": chosen,
        "closure_tus": len(closure),
        "engine_tus": len(ordered),
        "third_party_tus": len(third),
        "cut_symbols": len(cut),
        "non_compiling": non_compiling,
        "variants": {v: {"roots": len(r["roots"]), "closure": len(r["closure"]), "cut": len(r["cut"])}
                     for v, r in results.items()},
        "roots": res["roots"],
        "cut": [{"symbol": s, "demangled": dm_full.get(s, s), "defined_in": res["symdef"][s]} for s in cut],
        "crossing": {t: {s: sorted(r) for s, r in d.items()} for t, d in crossing.items()},
        "duplicates": [{"symbol": n, "demangled": dm_dups.get(n, n), "tus": t} for n, t in dups],
        "rtti_cuts": [{"class": s[len("<vtable/typeinfo of "):-1], "key_tu": t, "referenced_by": r}
                      for s, t, r in rtti_cuts],
        "platform_tus": {t: sorted(g.ext_by_tu[t] & PLATFORM_LIBS) for t in plat},
        "missing": sorted(missing),
    }
    with open(os.path.join(report_dir, "closure.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(data, f, indent=1)
    print(f"link_closure: {len(ordered)} engine + {len(third)} third-party TUs (variant {chosen}), "
          f"{len(cut)} cut symbols, {len(non_compiling)} not compiling, {len(dups)} duplicate symbols", file=sys.stderr)
    print(f"  closure: {fwd(closure_path)}", file=sys.stderr)
    print(f"  report:  {fwd(os.path.join(report_dir, 'report.md'))}", file=sys.stderr)
    for v, r in results.items():
        print(f"  variant {v:5s}: {len(r['roots']):4d} roots, {len(r['closure']):4d} TUs, {len(r['cut']):4d} cut",
              file=sys.stderr)
    return 0


def load_graph(objdir=DEFAULT_OBJDIR, sweep=DEFAULT_SWEEP, quiet=True):
    """The Win32 graph with the arm64 compile status (for build_runner.py)."""
    db = load_database(objdir, quiet=quiet)
    arm, _meta = load_arm_status(sweep)
    return Graph(db, arm)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--objdir", default=DEFAULT_OBJDIR, help="Debug|Win32 object directory")
    ap.add_argument("--sweep", default=DEFAULT_SWEEP, help="arm64 sweep giving the compile status")
    ap.add_argument("--variant", choices=("lua", "reg", "init"), default="init",
                    help="root variant to write (default init: every non-user-side static initialiser)")
    ap.add_argument("--out", default=DEFAULT_CLOSURE, help="closure file (default port/engine/runner/closure.txt)")
    ap.add_argument("--report-dir", default=DEFAULT_REPORT_DIR, help="report directory (default buildstage/runner/closure)")
    ap.add_argument("--check", action="store_true", help="exit 1 if --out differs from the computed closure")
    ap.add_argument("--jobs", type=int, default=None)
    args = ap.parse_args()

    db = load_database(args.objdir, jobs=args.jobs)
    if not db["order"]:
        print("link_closure: no objects; build main.vcxproj Debug|Win32 first", file=sys.stderr)
        return 2
    arm, arm_meta = load_arm_status(args.sweep)
    g = Graph(db, arm)
    results = {}
    for v in ("lua", "reg", "init"):
        results[v] = compute(g, v)
    return write_outputs(g, results, args.variant, args, arm_meta, os.path.abspath(args.out),
                         os.path.abspath(args.report_dir))


if __name__ == "__main__":
    sys.exit(main())
