#!/usr/bin/env python3
"""Compile-only arm64 sweep of the engine: every TU main.vcxproj builds, through NDK clang.

The engine (src/sdk) has no Android build yet. This sweep measures how far it is
from one (docs/port/android-roadmap.md, W1, milestone M2): it compiles each
translation unit that src/sdk/main.vcxproj builds for Release|x64 with the NDK's
clang for aarch64-linux-android, compile-only (-c; nothing is linked), and
reports which TUs compile and where the others stop.

A TU is compiled with, in this order:
  - the defines, include directories and forced includes of the Release|x64
    configuration, read from main.vcxproj, per-item overrides included. The
    forced include platform/X64LayoutAsserts.h switches the x86 layout asserts
    off on every non-x86 target, so arm64 reports code errors, not layout facts;
  - -I port/engine/shim before the project's directories: the Android-only
    stand-ins for windows.h, intrin.h and friends (clang ignores the directory
    while it does not exist);
  - the arguments of port/engine/compile_flags.txt, appended last. That file
    also drops the Windows-only project defines and include directories.

Output (default buildstage/engine-sweep/latest, ignored by git):
  report.md      OK count, first-error classes, top first-error sites, per-subsystem table
  results.json   per TU: ok, exit code, error and warning counts, first error, class, seconds
  logs/<tu>.log  the clang command and its complete output, one file per TU

usage:
  python scripts/port/engine_sweep.py [--jobs N] [--out DIR] [--files GLOB ...]

  # Re-check a subset against the baseline, in a directory of its own:
  python scripts/port/engine_sweep.py --files "moho/sim/*" "gpg/core/containers/String.cpp" \\
      --out buildstage/engine-sweep/mine --compare buildstage/engine-sweep/baseline
  # Re-run what failed before, updating that sweep's results in place:
  python scripts/port/engine_sweep.py --rerun-failed buildstage/engine-sweep/mine \\
      --out buildstage/engine-sweep/mine --merge
  # Print the clang command of a TU instead of compiling it:
  python scripts/port/engine_sweep.py --files lua/LuaObject.cpp --show-command

--files takes TU paths relative to src/sdk or globs over them: '/' or '\\'
separators, an optional src/sdk/ prefix, case-insensitive, '*' also matches
'/' (so "moho/sim/*" includes subdirectories), a bare directory means
everything below it. The boost.thread sources the project lists as
../../dependencies/... also match as dependencies/.... A pattern that matches
no TU is an error, so a typo cannot pass as an empty sweep.

The exit code is 0 when the sweep ran, whatever it found; 1 with
--fail-on-regression when a TU that compiled in the --compare sweep no longer
does; 2 for usage errors.
"""

import argparse
import collections
import concurrent.futures
import datetime
import fnmatch
import hashlib
import json
import os
import platform
import posixpath
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time
import xml.etree.ElementTree as ET

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_SDK = os.path.join(REPO_ROOT, "src", "sdk")
DEFAULT_FLAGS = os.path.join(REPO_ROOT, "port", "engine", "compile_flags.txt")
DEFAULT_SHIM = os.path.join(REPO_ROOT, "port", "engine", "shim")
DEFAULT_OUT = os.path.join(REPO_ROOT, "buildstage", "engine-sweep", "latest")
DEFAULT_NDK_VERSION = "29.0.14206865"  # scripts/port/build_android.ps1 uses the same one
RESULTS_VERSION = 1

MSB = "{http://schemas.microsoft.com/developer/msbuild/2003}"


def fwd(path):
    return path.replace("\\", "/")


def norm(path):
    return fwd(os.path.normpath(path))


# --------------------------------------------------------------------------
# main.vcxproj: the subset of MSBuild evaluation this project uses
# --------------------------------------------------------------------------

class ProjectError(Exception):
    pass


class Project:
    """Evaluates one configuration of a .vcxproj far enough to compile its ClCompile items.

    Handles PropertyGroup properties in document order, ItemDefinitionGroup
    ClCompile defaults, per-item metadata with %(Name) inheritance, and the
    conditions this project uses ('a'=='b', 'a'!='b', true/false, exists(),
    joined by and/or). Imports (the VC toolset props) are not read; properties
    they would define come out empty, which is what a non-Windows build wants
    for $(IncludePath). An unsupported condition is an error, not a guess.
    """

    METADATA = ("PreprocessorDefinitions", "UndefinePreprocessorDefinitions",
                "AdditionalIncludeDirectories", "ForcedIncludeFiles", "ExcludedFromBuild")

    def __init__(self, path, config):
        self.path = os.path.abspath(path)
        self.dir = os.path.dirname(self.path)
        self.config = config
        if config.count("|") != 1:
            raise ProjectError(f"configuration must look like Release|x64, not {config!r}")
        configuration, plat = config.split("|")
        self.props = {
            "Configuration": configuration,
            "Platform": plat,
            "MSBuildProjectDirectory": self.dir,
            "MSBuildProjectFullPath": self.path,
            "MSBuildThisFileDirectory": self.dir + os.sep,
            "SolutionPath": "",
            "SolutionDir": "",
            # VC++ Directories inherited from the toolset (Windows SDK, MSVC
            # headers): none of them applies to an NDK build.
            "IncludePath": "",
        }
        self.unknown_props = set()
        try:
            self.root = ET.parse(self.path).getroot()
        except (OSError, ET.ParseError) as e:
            raise ProjectError(f"cannot read {self.path}: {e}")
        self._evaluate()

    def expand(self, text):
        text = text or ""
        for _ in range(16):
            new = re.sub(r"\$\(([A-Za-z_][\w.]*)\)", self._prop, text)
            if new == text:
                return new
            text = new
        raise ProjectError(f"property expansion does not terminate: {text!r}")

    def _prop(self, m):
        name = m.group(1)
        if name not in self.props:
            self.unknown_props.add(name)
        return self.props.get(name, "")

    def condition(self, element):
        cond = element.get("Condition")
        if cond is None or not cond.strip():
            return True
        text = self.expand(cond).strip()
        return any(all(self._term(t) for t in re.split(r"\s+and\s+", disj, flags=re.I))
                   for disj in re.split(r"\s+or\s+", text, flags=re.I))

    def _term(self, term):
        t = term.strip()
        while t.startswith("(") and t.endswith(")"):
            t = t[1:-1].strip()
        m = re.fullmatch(r"'([^']*)'\s*(==|!=)\s*'([^']*)'", t)
        if m:
            equal = m.group(1).strip().lower() == m.group(3).strip().lower()
            return equal if m.group(2) == "==" else not equal
        m = re.fullmatch(r"(!?)\s*exists\(\s*'([^']*)'\s*\)", t, flags=re.I)
        if m:
            p = m.group(2)
            found = os.path.exists(p if os.path.isabs(p) else os.path.join(self.dir, p))
            return found if not m.group(1) else not found
        if t.lower() in ("true", "false"):
            return t.lower() == "true"
        raise ProjectError(f"unsupported MSBuild condition term {term!r} in {self.path}")

    def _evaluate(self):
        # Pass 1: properties, in document order.
        for group in self.root.iter(MSB + "PropertyGroup"):
            if not self.condition(group):
                continue
            for prop in group:
                if isinstance(prop.tag, str) and self.condition(prop):
                    self.props[prop.tag.replace(MSB, "")] = self.expand(prop.text)
        # Pass 2: ClCompile defaults for this configuration.
        self.defaults = {k: "" for k in self.METADATA}
        # Microsoft.Cpp.props, which is not read, turns CharacterSet into these
        # defines; they come after the project's own (%(PreprocessorDefinitions)).
        self.defaults["PreprocessorDefinitions"] = {
            "unicode": "_UNICODE;UNICODE", "multibyte": "_MBCS"}.get(self.props.get("CharacterSet", "").lower(), "")
        for group in self.root.iter(MSB + "ItemDefinitionGroup"):
            if not self.condition(group):
                continue
            for cl in group.findall(MSB + "ClCompile"):
                if self.condition(cl):
                    self._apply_metadata(cl, self.defaults)
        # Pass 3: the items, in project order (which is load-bearing for MSVC's
        # static-init order; the report keeps it).
        self.items, self.excluded, self.skipped = [], [], []
        for group in self.root.iter(MSB + "ItemGroup"):
            if not self.condition(group):
                continue
            for item in group:
                tag = item.tag.replace(MSB, "") if isinstance(item.tag, str) else ""
                inc = item.get("Include")
                if not inc:
                    continue
                if tag == "None" and inc.lower().endswith((".c", ".cpp")):
                    self.skipped.append(fwd(inc))
                if tag != "ClCompile" or not self.condition(item):
                    continue
                meta = dict(self.defaults)
                self._apply_metadata(item, meta)
                rel = fwd(self.expand(inc))
                if meta["ExcludedFromBuild"].strip().lower() == "true":
                    self.excluded.append(rel)
                elif not rel.lower().endswith((".cpp", ".cc", ".cxx")):
                    self.skipped.append(rel)
                else:
                    self.items.append((rel, meta))

    def _apply_metadata(self, element, meta):
        for child in element:
            if not isinstance(child.tag, str):
                continue
            name = child.tag.replace(MSB, "")
            if name not in self.METADATA or not self.condition(child):
                continue
            value = self.expand(child.text)
            value = re.sub(r"%\(" + name + r"\)", lambda _m: meta.get(name, ""), value)
            meta[name] = value

    def include_dirs(self, meta):
        """AdditionalIncludeDirectories, then the IncludePath (VC++ Directories), as MSVC searches."""
        out, seen = [], set()
        for raw in (meta["AdditionalIncludeDirectories"] + ";" + self.props.get("IncludePath", "")).split(";"):
            raw = raw.strip()
            if not raw or "%(" in raw:
                continue
            p = norm(raw if os.path.isabs(raw) else os.path.join(self.dir, raw))
            if p.lower() not in seen:
                seen.add(p.lower())
                out.append(p)
        return out

    @staticmethod
    def split_list(value):
        return [v.strip() for v in value.split(";") if v.strip() and "%(" not in v]


# --------------------------------------------------------------------------
# compile_flags.txt
# --------------------------------------------------------------------------

def read_flags_file(path):
    args, drop_defines, drop_includes = [], [], []
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = re.sub(r"(^|\s)#.*$", "", raw).strip()
            if not line:
                continue
            m = re.fullmatch(r"(drop-define|drop-include)\s*:\s*(.+)", line)
            if m:
                (drop_defines if m.group(1) == "drop-define" else drop_includes).append(m.group(2).strip())
            elif line.startswith("-"):
                args.extend(shlex.split(line))
            else:
                raise SystemExit(f"{path}:{n}: neither a clang argument nor a directive: {line}")
    return args, drop_defines, drop_includes


# --------------------------------------------------------------------------
# toolchain
# --------------------------------------------------------------------------

def find_clang(ndk_arg, clang_arg):
    if clang_arg:
        if not os.path.isfile(clang_arg):
            raise SystemExit(f"--clang {clang_arg}: no such file")
        return os.path.abspath(clang_arg), None
    candidates = [ndk_arg] if ndk_arg else []
    if not ndk_arg:
        candidates += [os.environ.get("ANDROID_NDK_ROOT"), os.environ.get("ANDROID_NDK_HOME")]
        sdks = [os.environ.get("ANDROID_SDK_ROOT"), os.environ.get("ANDROID_HOME"), r"C:\Android\sdk"]
        for sdk in filter(None, sdks):
            candidates.append(os.path.join(sdk, "ndk", DEFAULT_NDK_VERSION))
        for sdk in filter(None, sdks):
            ndk_dir = os.path.join(sdk, "ndk")
            if os.path.isdir(ndk_dir):
                def version_key(name):
                    return [int(x) if x.isdigit() else -1 for x in name.split(".")]
                for name in sorted(os.listdir(ndk_dir), key=version_key, reverse=True):
                    candidates.append(os.path.join(ndk_dir, name))
    host = {"Windows": "windows-x86_64", "Linux": "linux-x86_64", "Darwin": "darwin-x86_64"}.get(platform.system())
    exe = "clang++.exe" if os.name == "nt" else "clang++"
    tried = []
    for ndk in filter(None, candidates):
        clang = os.path.join(ndk, "toolchains", "llvm", "prebuilt", host or "", "bin", exe)
        tried.append(ndk)
        if os.path.isfile(clang):
            return clang, os.path.abspath(ndk)
    raise SystemExit("Android NDK not found (tried: " + ", ".join(tried or ["nothing"]) +
                     "). Pass --ndk DIR or --clang PATH.")


def clang_version(clang):
    try:
        out = subprocess.run([clang, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=60).stdout.decode("utf-8", "replace")
        return out.splitlines()[0].strip() if out else "?"
    except (OSError, subprocess.SubprocessError) as e:
        return f"? ({e})"


def git(*args):
    try:
        return subprocess.run(["git", "-C", REPO_ROOT] + list(args), stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, timeout=60).stdout.decode("utf-8", "replace").strip()
    except (OSError, subprocess.SubprocessError):
        return ""


# --------------------------------------------------------------------------
# clang output
# --------------------------------------------------------------------------

DIAG = re.compile(r"^(?P<file>.+?):(?P<line>\d+):(?P<col>\d+): (?P<kind>fatal error|error|warning|note): (?P<msg>.*)$")
DRIVER_ERROR = re.compile(r"^(?:\S*clang\S*: )?(?P<kind>fatal error|error): (?P<msg>.*)$")
INCLUDED_FROM = re.compile(r"^(?:In file included from|\s+from) (?P<file>.+?):(?P<line>\d+)[:,]$")
WARNING_FLAG = re.compile(r"\[(-W[^,\]]+)[^\]]*\]$")
DID_YOU_MEAN = re.compile(r";\s*did you mean .*$")
IDENT = re.compile(r"(?:use of undeclared identifier|unknown type name|no (?:member|type|template) named|"
                   r"undeclared identifier|use of undeclared label) '([^']+)'")


class PathNamer:
    """Turns the absolute paths clang prints into short, stable names for the report."""

    def __init__(self, sdk_dir, repo_root, ndk_dir, clang):
        # The toolchain root holds both clang's own headers (lib/clang/N/include)
        # and the sysroot (sysroot/usr/include, libc++ under c++/v1).
        toolchain = os.path.dirname(os.path.dirname(clang))
        prefixes = [(sdk_dir, "src/sdk"), (repo_root, ""), (toolchain, "<ndk>" if ndk_dir else "<clang>")]
        # Longest first, so src/sdk wins over the repository root.
        self.prefixes = sorted(((norm(p).lower().rstrip("/") + "/", n) for p, n in prefixes),
                               key=lambda x: -len(x[0]))

    def __call__(self, path):
        p = norm(path)
        low = p.lower()
        for prefix, name in self.prefixes:
            if low.startswith(prefix):
                rest = p[len(prefix):]
                if name.startswith("<"):
                    rest = re.sub(r"^(?:lib/clang/\d+/include|sysroot/usr/include)/", "", rest)
                return f"{name}/{rest}" if name else rest
        return p


def parse_output(text, namer):
    """Error and warning counts, the first error, every error site, and undeclared names."""
    errors = warnings = 0
    first = None
    first_notes = []
    collecting_notes = False
    chain = []
    sites = set()
    idents = set()
    warning_sites = collections.defaultdict(set)
    for line in text.splitlines():
        m = INCLUDED_FROM.match(line)
        if m:
            chain.append(f"{namer(m.group('file'))}:{m.group('line')}")
            continue
        m = DIAG.match(line)
        if m:
            kind = m.group("kind")
            loc_file = namer(m.group("file"))
            msg = m.group("msg")
            if kind == "note":
                if collecting_notes:
                    first_notes.append(msg)
                chain = []
                continue
            collecting_notes = False
            if kind == "warning":
                warnings += 1
                wf = WARNING_FLAG.search(msg)
                warning_sites[wf.group(1) if wf else "(no flag)"].add(f"{loc_file}:{m.group('line')}")
            else:
                errors += 1
                short = DID_YOU_MEAN.sub("", msg)
                sites.add(f"{loc_file}:{m.group('line')}: {short}")
                im = IDENT.search(msg)
                if im:
                    # The x86 intrinsic headers alone declare thousands of these.
                    name = im.group(1)
                    idents.add("__builtin_ia32_*" if name.startswith("__builtin_ia32_") else name)
                if first is None:
                    first = {"file": loc_file, "line": int(m.group("line")), "col": int(m.group("col")),
                             "kind": kind, "message": msg, "included_from": list(chain)}
                    collecting_notes = True
            chain = []
            continue
        m = DRIVER_ERROR.match(line)
        if m:
            errors += 1
            sites.add(f"<driver>: {m.group('msg')}")
            if first is None:
                first = {"file": "<driver>", "line": 0, "col": 0, "kind": m.group("kind"),
                         "message": m.group("msg"), "included_from": []}
            chain = []
    if first is not None:
        first["notes"] = first_notes[:6]
    return errors, warnings, first, sites, idents, warning_sites


# --------------------------------------------------------------------------
# first-error classes
# --------------------------------------------------------------------------

X86_HEADERS = re.compile(r"^(intrin|immintrin|xmmintrin|emmintrin|mmintrin|pmmintrin|tmmintrin|smmintrin|"
                         r"nmmintrin|wmmintrin|ammintrin|x86intrin|ia32intrin|x86gprintrin)\.h$")
DX_HEADERS = re.compile(r"^(d3d\w*|d3dx\w*|dxgi\w*|dxerr\w*|dxfile|dxdiag|ddraw|dsound|dinput\w*|xinput\w*|"
                        r"x3daudio|xact\w*|xaudio2\w*|dsetup|dxsdkver)\.h$")
WIN_HEADERS = re.compile(r"^(windows|windowsx|winsock2?|ws2\w*|mswsock|wsipx|mmsystem|mmreg|msacm|dbghelp|"
                         r"imagehlp|shlobj|shellapi|shlwapi|commctrl|commdlg|objbase|ole\w*|oaidl|ocidl|"
                         r"unknwn|objidl|propidl|wtypes\w*|comdef|comutil|comip|guiddef|basetsd|"
                         r"win\w+|rpc\w*|process|io|direct|crtdbg|tchar|strsafe|eh|new|conio|share|"
                         r"mbstring|mbctype|tlhelp32|psapi|iphlpapi|wininet|errorrep|faultrep|vfw|"
                         r"richedit|atl\w*|sys/utime|sys/timeb|minidumpapiset|intsafe|specstrings\w*|"
                         r"sal|mmdeviceapi|audioclient|dbt|setupapi|devguid|hidsdi|hidusage|lmcons)\.h$", re.I)
MISSING_HEADER = re.compile(r"'([^']+)' file not found")
X86_HEADER_CLASS = "x86-only header (intrin.h, immintrin.h, ...)"
MSVC_INTRINSIC = re.compile(r"^(_mm\w*|__m\d+\w*|_Interlocked\w+|_BitScan\w+|_bittest\w*|_bittestand\w+|"
                            r"_ReturnAddress|_AddressOfReturnAddress|__debugbreak|__noop|__assume|__cpuid\w*|"
                            r"__rdtsc|_rot[lr]\d*|_lrot[lr]|_byteswap_\w+|__popcnt\w*|__lzcnt\w*|__emul\w*|"
                            r"_umul128|_mul128|__read[fg]s\w+|_ReadWriteBarrier|_ReadBarrier|_WriteBarrier|"
                            r"__faststorefence|_mm_pause|__ll_\w+|__ull_\w+)$")
X86_USE = re.compile(r"__builtin_ia32|invalid (?:input|output) constraint|only meant to be used on x86|"
                     r"'__m(?:64|128\w*|256\w*)'|requires target feature|needs target feature|"
                     r"always_inline function .* requires target")
INLINE_ASM = re.compile(r"MS-style inline assembly|__asm\b|\basm\b.*not supported|unknown register name|"
                        r"invalid operand for instruction|unrecognized instruction mnemonic|"
                        r"inline assembly", re.I)
SEH = re.compile(r"__try|__except|__finally|__leave|structured exception|\bSEH\b")
STATIC_ASSERT = re.compile(r"static assertion failed|static_assert")
REMOVED_STD = re.compile(r"\b(auto_ptr|unary_function|binary_function|bind1st|bind2nd|ptr_fun|"
                         r"pointer_to_unary_function|pointer_to_binary_function|mem_fun1?(?:_ref)?(?:_t)?|"
                         r"random_shuffle|set_unexpected|unexpected_handler|binder1st|binder2nd)\b")
PTR_TRUNC = re.compile(r"cast from pointer to smaller type|loses information|"
                       r"cast from '[^']*\*[^']*' to '(?:int|unsigned int|long|unsigned long|DWORD|LONG|"
                       r"std::uint32_t|uint32_t|std::int32_t|int32_t)'")
DEFAULT_ARG = re.compile(r"redefinition of default argument|default arguments cannot be added")
TWO_PHASE = re.compile(r"dependent base class|missing 'typename'|use 'template' keyword|"
                       r"neither visible in the template definition nor found by argument-dependent lookup|"
                       r"explicit qualification required|must qualify identifier|"
                       r"declarations in dependent base|template argument for template type parameter must be a type|"
                       r"is not usable in a constant expression.*template|cannot be used prior to '::'")
LP64 = re.compile(r"'(?:unsigned )?long'.*'(?:unsigned )?int'|'(?:unsigned )?int'.*'(?:unsigned )?long'|"
                  r"wchar_t|char16_t|non-constant-expression cannot be narrowed|narrowing")
WIN32_NAMES = set("""
    DWORD WORD BYTE BOOL BOOLEAN HANDLE HMODULE HINSTANCE HWND HDC HICON HCURSOR HBITMAP HBRUSH HFONT HMENU
    HKEY HRESULT LONG ULONG UINT INT USHORT UCHAR CHAR SHORT FLOAT DOUBLE LPVOID LPCVOID LPSTR LPCSTR LPWSTR
    LPCWSTR LPTSTR LPCTSTR LPDWORD LPBYTE LPLONG LPWORD LPBOOL PVOID PBYTE PDWORD PLONG PCHAR PSTR PCSTR
    LARGE_INTEGER ULARGE_INTEGER PLARGE_INTEGER FILETIME LPFILETIME SYSTEMTIME LPSYSTEMTIME RECT LPRECT POINT
    LPPOINT SIZE MSG WPARAM LPARAM LRESULT ATOM COLORREF CRITICAL_SECTION LPCRITICAL_SECTION SOCKET SOCKADDR
    SOCKADDR_IN WSADATA INT_PTR UINT_PTR LONG_PTR ULONG_PTR DWORD_PTR SIZE_T SSIZE_T DWORD64 DWORDLONG
    ULONGLONG LONGLONG TCHAR WCHAR LPOVERLAPPED OVERLAPPED SECURITY_ATTRIBUTES WIN32_FIND_DATA
    WIN32_FIND_DATAA WIN32_FIND_DATAW OSVERSIONINFO OSVERSIONINFOEX MEMORYSTATUS MEMORYSTATUSEX SYSTEM_INFO
    GUID IID CLSID REFIID REFGUID LPUNKNOWN IUnknown VARIANT BSTR HGLOBAL HLOCAL HRGN HPEN HPALETTE HGDIOBJ
    HMONITOR HHOOK HACCEL HRSRC HFILE FARPROC PROC WNDPROC TRUE FALSE MAX_PATH INFINITE INVALID_HANDLE_VALUE
    CALLBACK WINAPI APIENTRY WINBASEAPI EXCEPTION_POINTERS EXCEPTION_RECORD CONTEXT PEXCEPTION_POINTERS
    LPTOP_LEVEL_EXCEPTION_FILTER MINIDUMP_TYPE HKL TIMECAPS MMRESULT WAVEFORMATEX BITMAPINFO BITMAPINFOHEADER
    RGBQUAD PAINTSTRUCT WNDCLASS WNDCLASSEX CREATESTRUCT MINMAXINFO TRACKMOUSEEVENT HOSTENT FD_SET TIMEVAL
    in_addr IN_ADDR SOCKET_ERROR INVALID_SOCKET
    InterlockedIncrement InterlockedDecrement InterlockedExchange InterlockedExchangeAdd
    InterlockedCompareExchange InterlockedCompareExchangePointer InterlockedExchangePointer
    EnterCriticalSection LeaveCriticalSection InitializeCriticalSection DeleteCriticalSection
    InitializeCriticalSectionAndSpinCount TryEnterCriticalSection GetCurrentThreadId GetCurrentThread
    GetCurrentProcess GetCurrentProcessId Sleep SleepEx QueryPerformanceCounter QueryPerformanceFrequency
    timeGetTime timeBeginPeriod timeEndPeriod timeGetDevCaps GetTickCount GetTickCount64 CloseHandle
    CreateEvent CreateEventA CreateEventW CreateThread CreateFile CreateFileA CreateFileW CreateMutex
    CreateSemaphore CreateDirectory CreateDirectoryA CreateDirectoryW CreateProcess CreateWindow
    CreateWindowEx CreateFileMapping SetEvent ResetEvent WaitForSingleObject WaitForMultipleObjects
    GetLastError SetLastError VirtualAlloc VirtualFree VirtualProtect VirtualQuery HeapAlloc HeapFree
    HeapReAlloc HeapCreate HeapDestroy HeapSize GetProcessHeap OutputDebugString OutputDebugStringA
    OutputDebugStringW IsDebuggerPresent DebugBreak TlsAlloc TlsFree TlsGetValue TlsSetValue MessageBox
    MessageBoxA MessageBoxW GetCursorPos SetCursorPos ClipCursor ShowCursor GetKeyState GetAsyncKeyState
    GetModuleFileName GetModuleFileNameA GetModuleFileNameW GetModuleHandle GetModuleHandleA
    GetModuleHandleW GetSystemInfo GetSystemTime GetLocalTime GetSystemTimeAsFileTime FileTimeToSystemTime
    SystemTimeToFileTime FileTimeToLocalFileTime GetFileAttributes GetFileAttributesA GetFileAttributesW
    GetFileAttributesEx GetFileSize GetFileSizeEx SetFilePointer GetEnvironmentVariable
    SetEnvironmentVariable GetCurrentDirectory SetCurrentDirectory GetTempPath GetComputerName GetUserName
    GetVersionEx SetThreadPriority GetThreadPriority SetPriorityClass SetThreadAffinityMask
    GetDiskFreeSpaceEx GetWindowLong SetWindowLong GetForegroundWindow SetForegroundWindow GetDesktopWindow
    GetClientRect GetWindowRect FindFirstFile FindFirstFileA FindFirstFileW FindNextFile FindNextFileA
    FindNextFileW FindClose ReadFile WriteFile DeleteFile DeleteFileA DeleteFileW MoveFile MoveFileEx
    CopyFile RemoveDirectory RegOpenKeyEx RegQueryValueEx RegCloseKey RegCreateKeyEx RegSetValueEx
    SHGetFolderPath SHGetFolderPathA SHGetFolderPathW SHCreateDirectoryEx ShellExecute ShellExecuteA
    ShellExecuteW MultiByteToWideChar WideCharToMultiByte lstrlen lstrcpy lstrcmpi wsprintf wsprintfA
    wsprintfW ZeroMemory CopyMemory FillMemory MoveMemory SecureZeroMemory RaiseException
    SetUnhandledExceptionFilter UnhandledExceptionFilter LoadLibrary LoadLibraryA LoadLibraryW
    FreeLibrary GetProcAddress LOWORD HIWORD LOBYTE HIBYTE MAKEWORD MAKELONG MAKEINTRESOURCE SUCCEEDED
    FAILED closesocket ioctlsocket GetVersion GetSystemDirectory GetWindowsDirectory GlobalMemoryStatus
    GlobalMemoryStatusEx GetExitCodeThread TerminateThread ResumeThread SuspendThread ExitProcess
    TerminateProcess GetStdHandle SetConsoleTitle AllocConsole PostMessage SendMessage PeekMessage
    GetMessage DispatchMessage TranslateMessage DefWindowProc PostQuitMessage MiniDumpWriteDump
    StackWalk64 SymInitialize SymFromAddr SymGetLineFromAddr64 SymCleanup
""".split())
WIN32_PREFIX = re.compile(r"^(WSA\w+|ERROR_\w+|WAIT_\w+|FILE_\w+|GENERIC_\w+|MB_\w+|VK_\w+|WM_\w+|SW_\w+|"
                          r"CSIDL_\w+|THREAD_PRIORITY_\w+|EXCEPTION_\w+|PAGE_\w+|MEM_\w+|HEAP_\w+|"
                          r"S_[A-Z_]+|E_[A-Z_]+|SM_\w+|IDC_\w+|IDI_\w+|WS_\w+|GWL_\w+|HWND_\w+|"
                          r"SEM_\w+|CREATE_\w+|OPEN_\w+|INVALID_\w+|STD_\w+_HANDLE)$")
CRT_NAME = re.compile(r"^(_[a-z]\w*|_[A-Z]\w*_s|\w+_s|_?stricmp|_?strnicmp|_?strlwr|_?strupr|_?strrev|"
                      r"_?itoa|_?ltoa|_?ultoa|_?i64toa|_?ui64toa|_?wcsicmp|_?wcsnicmp|_?strdate|_?strtime|"
                      r"_?alloca|errno_t|_countof|_TCHAR|_T|_tcs\w+|_tprintf|_tmain|__iob_func|_HEAP_\w+|"
                      r"_MAX_\w+|_O_\w+|_S_I\w+|_SH_\w+|_CRT_\w+|_finite|_isnan|_fpclass|_controlfp\w*|"
                      r"_control87|_clearfp|_statusfp|_fpreset|_matherr|_set_\w+|_get_\w+)$")


def classify(first, rc, timed_out):
    """The kind of fix the first error asks for. Ordered: the first rule that matches wins."""
    if timed_out:
        return "timeout"
    if first is None:
        return "no error line (exit code %s)" % rc
    msg, file = first["message"], first["file"]
    text = msg + "\n" + "\n".join(first.get("notes", []))
    m = MISSING_HEADER.search(msg)
    if m:
        header = fwd(m.group(1)).lower()
        base = header.rsplit("/", 1)[-1]
        if X86_HEADERS.match(base):
            return X86_HEADER_CLASS
        if header.startswith("wx/"):
            return "missing header: wxWidgets"
        if DX_HEADERS.match(base):
            return "missing header: Direct3D / DirectX / XACT"
        if WIN_HEADERS.match(header) or WIN_HEADERS.match(base):
            return "missing header: Windows SDK / MSVC CRT"
        return "missing header: other"
    if "only meant to be used on x86" in msg:
        return X86_HEADER_CLASS
    if file.endswith("boost/thread/xtime.hpp") and "expected identifier" in msg:
        # C11's TIME_UTC macro (<time.h>) collides with boost 1.34's enum constant.
        return "boost 1.34 TIME_UTC vs C11 <time.h>"
    if PTR_TRUNC.search(msg):
        return "pointer truncation (M3)"
    ident = IDENT.search(msg)
    name = ident.group(1) if ident else ""
    if X86_USE.search(msg) or (name and MSVC_INTRINSIC.match(name)):
        return "x86 / MSVC intrinsic"
    if INLINE_ASM.search(msg):
        return "MS inline assembly"
    if SEH.search(msg):
        return "SEH (__try/__except)"
    if STATIC_ASSERT.search(msg):
        return "static_assert"
    if REMOVED_STD.search(msg):
        return "std feature removed in C++17/20 (auto_ptr, unary_function, ...)"
    if name:
        if name in WIN32_NAMES or WIN32_PREFIX.match(name):
            return "Win32 type / macro / API undeclared"
        if CRT_NAME.match(name):
            return "MSVC CRT name undeclared"
        if msg.startswith("unknown type name") and re.fullmatch(r"[A-Z][A-Z0-9_]{2,}", name):
            return "Win32 type / macro / API undeclared"
    if DEFAULT_ARG.search(msg):
        return "default argument redefined"
    if TWO_PHASE.search(text):
        return "two-phase / dependent-name lookup"
    if LP64.search(msg):
        return "long / wchar_t size or narrowing (LP64)"
    if file.startswith("dependencies/"):
        return "error inside " + "/".join(file.split("/")[:2])
    if file.startswith("<ndk>") or file.startswith("<clang>"):
        return "error inside an NDK header"
    if file.startswith("port/engine/shim"):
        return "error inside port/engine/shim"
    if file == "<driver>":
        return "clang driver error"
    return "C++ error in engine code (unclassified)"


# --------------------------------------------------------------------------
# the sweep
# --------------------------------------------------------------------------

def repo_rel(tu):
    """A TU's path in the repository: src/sdk/... or, for the boost.thread
    sources main.vcxproj lists as ../../dependencies/..., dependencies/...."""
    return posixpath.normpath("src/sdk/" + tu)


def artifact_rel(tu):
    """Where a TU's log and object go below <out>/logs and <out>/obj: its path
    in src/sdk, or dependencies/... for the TUs outside it (never above)."""
    rel = repo_rel(tu)
    rel = rel[len("src/sdk/"):] if rel.startswith("src/sdk/") else rel
    return "/".join("__" if part == ".." else part for part in rel.split("/"))


def subsystem(tu):
    parts = artifact_rel(tu).split("/")
    return "/".join(parts[:2]) if len(parts) > 2 else parts[0]


def select_tus(all_tus, patterns):
    keys = {t: {t.lower(), repo_rel(t).lower(), artifact_rel(t).lower()} for t in all_tus}
    chosen, missing = set(), []
    for pat in patterns:
        p = re.sub(r"^\./", "", fwd(pat).strip())
        forms = {p.lower(), re.sub(r"^(?:.*/)?src/sdk/", "", p, flags=re.I).lower()}
        hits = [t for t in all_tus if any(fnmatch.fnmatchcase(k, f) for k in keys[t] for f in forms)]
        if not hits and not any(c in p for c in "*?["):
            # A directory stands for everything below it.
            prefixes = tuple(f.rstrip("/") + "/" for f in forms)
            hits = [t for t in all_tus if any(k.startswith(prefixes) for k in keys[t])]
        if not hits:
            missing.append(pat)
        chosen.update(hits)
    return [t for t in all_tus if t in chosen], missing


def load_results(path):
    p = path if path.endswith(".json") else os.path.join(path, "results.json")
    with open(p, encoding="utf-8") as f:
        data = json.load(f)
    return data, {r["tu"]: r for r in data.get("tus", [])}


def build_commands(project, tus, clang, flag_args, drop_defines, drop_includes, shim, out_dir, error_limit, extra):
    drop_def = set(drop_defines)
    drop_inc = [fwd(d).lower() for d in drop_includes]
    meta_by_tu = dict(project.items)
    commands = {}
    for tu in tus:
        meta = meta_by_tu[tu]
        src = norm(os.path.join(project.dir, tu))
        obj = norm(os.path.join(out_dir, "obj", artifact_rel(tu) + ".o"))
        cmd = [fwd(clang), "-c", src, "-o", obj, f"-ferror-limit={error_limit}",
               "-fno-color-diagnostics", "-fdiagnostics-absolute-paths"]
        for d in project.split_list(meta["PreprocessorDefinitions"]):
            if d.split("=", 1)[0] not in drop_def:
                cmd.append("-D" + d)
        for u in project.split_list(meta["UndefinePreprocessorDefinitions"]):
            cmd.append("-U" + u)
        if shim:
            cmd.append("-I" + norm(shim))
        dirs = [d for d in project.include_dirs(meta) if not any(x in d.lower() + "/" for x in drop_inc)]
        cmd += ["-I" + d for d in dirs]
        for fi in map(fwd, project.split_list(meta["ForcedIncludeFiles"])):
            resolved = None
            for base in [project.dir] + dirs:
                cand = os.path.join(base, fi)
                if os.path.isfile(cand):
                    resolved = norm(cand)
                    break
            cmd += ["-include", resolved or fi]
        cmd += flag_args + list(extra)
        commands[tu] = cmd
    return commands


def compile_tu(tu, cmd, out_dir, timeout, keep_objects, namer, cwd, log_limit):
    log_path = os.path.join(out_dir, "logs", artifact_rel(tu) + ".log")
    obj_path = cmd[cmd.index("-o") + 1]
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    os.makedirs(os.path.dirname(obj_path), exist_ok=True)
    t0 = time.monotonic()
    timed_out = False
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, cwd=cwd)
        rc, raw = p.returncode, p.stdout
    except subprocess.TimeoutExpired as e:
        rc, raw, timed_out = None, (e.output or b""), True
    except OSError as e:
        rc, raw = None, f"engine_sweep: could not run clang: {e}\n".encode()
    seconds = time.monotonic() - t0
    text = raw.decode("utf-8", "replace").replace("\r\n", "\n")
    logged = text
    if log_limit and len(logged) > log_limit:
        # The counts and the first error come from the whole output; the file
        # keeps the start, which is what a reader looks at.
        cut = logged.rfind("\n", 0, log_limit) + 1 or log_limit
        logged = (logged[:cut] + f"\n[engine_sweep] log truncated: {len(text) - cut} more characters "
                  f"(--log-limit-mb)\n")
    header = (f"# engine_sweep: {tu}\n# cwd: {fwd(cwd)}\n# command: {shlex.join(cmd)}\n"
              f"# exit: {'timeout after %ds' % timeout if timed_out else rc}   seconds: {seconds:.1f}\n\n")
    with open(log_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header + logged)
    if not keep_objects:
        try:
            os.remove(obj_path)
        except OSError:
            pass
    errors, warnings, first, sites, idents, wsites = parse_output(text, namer)
    ok = (rc == 0 and errors == 0 and not timed_out)
    record = {
        "tu": tu,
        "subsystem": subsystem(tu),
        "ok": ok,
        "exit_code": rc,
        "timed_out": timed_out,
        "errors": errors,
        "warnings": warnings,
        "first_error": None if ok else first,
        "class": None if ok else classify(first, rc, timed_out),
        "seconds": round(seconds, 2),
        "log": "logs/" + artifact_rel(tu) + ".log",
    }
    return record, (sites, idents, wsites)


def parse_log_file(out_dir, rec, namer):
    """Error sites of a TU compiled by an earlier run (for --merge reports)."""
    path = os.path.join(out_dir, rec.get("log") or "logs/" + artifact_rel(rec["tu"]) + ".log")
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return set(), set(), {}
    _, _, _, sites, idents, wsites = parse_output(text, namer)
    return sites, idents, wsites


# --------------------------------------------------------------------------
# report
# --------------------------------------------------------------------------

def md_escape(s):
    """Plain table text: no cell breaks, no HTML."""
    return str(s).replace("|", "\\|").replace("<", "&lt;").replace(">", "&gt;")


def md_code(s):
    """A code span in a table cell (entities are not decoded inside code spans)."""
    return "`" + str(s).replace("`", "'").replace("|", "\\|") + "`"


def pct(a, b):
    return f"{100.0 * a / b:.1f}%" if b else "-"


def write_report(out_dir, meta, records, extras, compare):
    total = len(records)
    ok = sum(1 for r in records if r["ok"])
    failed = [r for r in records if not r["ok"]]
    L = []
    L.append("# Engine arm64 compile sweep\n")
    L.append("Generated by `scripts/port/engine_sweep.py`; see `port/engine/README.md`.\n")
    L.append(f"**{ok} / {total} TUs compile** ({pct(ok, total)}), {len(failed)} fail"
             + (f", {sum(1 for r in records if r['timed_out'])} timed out" if any(r["timed_out"] for r in records) else "")
             + ".\n")
    L.append("| | |\n|---|---|")
    for k in ("date", "git", "project", "configuration", "selection", "clang", "flags_file", "shim",
              "jobs", "wall_seconds", "command_line"):
        if k in meta and meta[k] not in (None, ""):
            v = meta[k]
            L.append(f"| {k} | {md_code(v) if k in ('command_line', 'project', 'flags_file', 'shim') else md_escape(v)} |")
    L.append("")
    L.append("Arguments every TU gets (after `-c <tu> -o <obj>`; per-item project overrides come on top):\n")
    L.append("```\n" + meta.get("common_args", "") + "\n```\n")

    if compare is not None:
        cmp_meta, cmp_by_tu = compare
        common = [r for r in records if r["tu"] in cmp_by_tu]
        before = sum(1 for r in common if cmp_by_tu[r["tu"]]["ok"])
        now = sum(1 for r in common if r["ok"])
        newly_ok = [r["tu"] for r in common if r["ok"] and not cmp_by_tu[r["tu"]]["ok"]]
        newly_bad = [r for r in common if not r["ok"] and cmp_by_tu[r["tu"]]["ok"]]
        absent = [r["tu"] for r in records if r["tu"] not in cmp_by_tu]
        L.append(f"## Compared with `{md_escape(meta.get('compare', ''))}`\n")
        L.append(f"Over the {len(common)} TUs in both sweeps: **{before} -> {now} OK** "
                 f"(+{len(newly_ok)} newly compile, -{len(newly_bad)} no longer compile)"
                 + (f"; {len(absent)} TUs are not in the other sweep" if absent else "") + ".\n")
        if newly_bad:
            L.append("No longer compile:\n")
            for r in newly_bad:
                fe = r.get("first_error") or {}
                L.append(f"- `{r['tu']}`: `{fe.get('file', '?')}:{fe.get('line', 0)}`: "
                         f"{md_escape(fe.get('message', '?')[:200])}")
            L.append("")
        if newly_ok:
            L.append(f"<details><summary>Newly compile ({len(newly_ok)})</summary>\n")
            L += [f"- `{t}`" for t in newly_ok]
            L.append("\n</details>\n")
        changed = collections.Counter()
        for r in common:
            if not r["ok"] and not cmp_by_tu[r["tu"]]["ok"] and r.get("class") != cmp_by_tu[r["tu"]].get("class"):
                changed[(cmp_by_tu[r["tu"]].get("class"), r.get("class"))] += 1
        if changed:
            L.append("Still failing, but now stopped by something else (old class -> new class):\n")
            L.append("| TUs | before | now |\n|---:|---|---|")
            for (a, b), n in changed.most_common(15):
                L.append(f"| {n} | {md_escape(a)} | {md_escape(b)} |")
            L.append("")

    L.append("## First-error classes\n")
    L.append("The class of the first error of each failing TU (heuristic, from the message; "
             "see `classify()` in the script). Later errors often only follow from the first.\n")
    L.append("| TUs | class |\n|---:|---|")
    for c, n in collections.Counter(r["class"] for r in failed).most_common():
        L.append(f"| {n} | {md_escape(c)} |")
    L.append("")

    L.append("## Top first-error sites\n")
    L.append("Fixing one of these unblocks every TU it stops (the TU may then stop somewhere else).\n")
    L.append("| TUs | where | first error |\n|---:|---|---|")
    site_count = collections.Counter()
    for r in failed:
        fe = r.get("first_error")
        if fe:
            site_count[(f"{fe['file']}:{fe['line']}", DID_YOU_MEAN.sub("", fe["message"]))] += 1
        else:
            site_count[("?", r["class"])] += 1
    for (loc, msg), n in site_count.most_common(40):
        L.append(f"| {n} | {md_code(loc)} | {md_escape(msg[:160])} |")
    L.append("")

    L.append("## Files holding the first error\n")
    L.append("| TUs | file |\n|---:|---|")
    for f, n in collections.Counter((r.get("first_error") or {}).get("file", "?") for r in failed).most_common(25):
        L.append(f"| {n} | {md_code(f)} |")
    L.append("")

    L.append("## Engine lines that lead to the first error\n")
    L.append("The innermost `src/sdk` location on the way to each first error: the first error itself "
             "when it is in engine code, otherwise the engine `#include` that pulled in the header "
             "holding it. This is where an engine-side fix goes.\n")
    L.append("| TUs | engine line |\n|---:|---|")
    entry = collections.Counter()
    for r in failed:
        fe = r.get("first_error") or {}
        if fe.get("file", "").startswith("src/sdk/"):
            entry[f"{fe['file']}:{fe['line']}"] += 1
        else:
            inner = [x for x in fe.get("included_from", []) if x.startswith("src/sdk/")]
            entry[inner[-1] if inner else "(none: the TU is outside src/sdk or has no error line)"] += 1
    for loc, n in entry.most_common(25):
        L.append(f"| {n} | {md_code(loc)} |")
    L.append("")

    all_sites = collections.Counter()
    ident_tus = collections.Counter()
    warn_sites = collections.defaultdict(set)
    for r in records:
        sites, idents, wsites = extras.get(r["tu"], (set(), set(), {}))
        all_sites.update(sites)
        ident_tus.update(idents)
        for flag, s in wsites.items():
            warn_sites[flag].update(s)
    L.append("## All errors\n")
    L.append(f"{sum(r['errors'] for r in records)} error diagnostics over all TUs; "
             f"**{len(all_sites)} unique error sites** (file:line:message, counted once however many TUs "
             f"hit them). Distribution of errors per failing TU: "
             + ", ".join(f"{label}: {n}" for label, n in (
                 ("1-5", sum(1 for r in failed if 1 <= r["errors"] <= 5)),
                 ("6-20", sum(1 for r in failed if 6 <= r["errors"] <= 20)),
                 ("21-100", sum(1 for r in failed if 21 <= r["errors"] <= 100)),
                 (">100", sum(1 for r in failed if r["errors"] > 100)))) + ".\n")
    if ident_tus:
        L.append("Undeclared identifiers and unknown type names, by the number of TUs that hit them "
                 "(any error, not only the first):\n")
        L.append(", ".join(f"{md_code(k)} {v}" for k, v in ident_tus.most_common(120)) + "\n")
    if warn_sites:
        L.append("Warnings, unique sites per flag (top 15):\n")
        L.append("| sites | flag |\n|---:|---|")
        for flag, s in sorted(warn_sites.items(), key=lambda kv: -len(kv[1]))[:15]:
            L.append(f"| {len(s)} | {md_code(flag)} |")
        L.append("")

    L.append("## Per subsystem\n")
    by_sub = collections.OrderedDict()
    for r in records:
        by_sub.setdefault(r["subsystem"], []).append(r)
    head = "| subsystem | TUs | OK | failed | OK % |"
    sep = "|---|---:|---:|---:|---:|"
    if compare is not None:
        head += " OK in compared sweep |"
        sep += "---:|"
    head += " most common first-error class |"
    sep += "---|"
    L.append(head + "\n" + sep)
    for sub in sorted(by_sub):
        rs = by_sub[sub]
        n_ok = sum(1 for r in rs if r["ok"])
        top = collections.Counter(r["class"] for r in rs if not r["ok"]).most_common(1)
        row = f"| {sub} | {len(rs)} | {n_ok} | {len(rs) - n_ok} | {pct(n_ok, len(rs))} |"
        if compare is not None:
            row += f" {sum(1 for r in rs if compare[1].get(r['tu'], {}).get('ok'))} |"
        row += f" {md_escape(top[0][0]) + ' (' + str(top[0][1]) + ')' if top else ''} |"
        L.append(row)
    L.append(f"| **total** | {total} | {ok} | {total - ok} | {pct(ok, total)} |"
             + (f" {sum(1 for r in records if compare[1].get(r['tu'], {}).get('ok'))} |" if compare is not None else "")
             + " |")
    L.append("")

    L.append(f"<details><summary>Failing TUs ({len(failed)}), in project order</summary>\n")
    L.append("| TU | errors | first error |\n|---|---:|---|")
    for r in failed:
        fe = r.get("first_error") or {}
        L.append(f"| {md_code(r['tu'])} | {r['errors']} | "
                 f"{md_code(str(fe.get('file', '?')) + ':' + str(fe.get('line', 0)))}: "
                 f"{md_escape(fe.get('message', r['class'])[:140])} |")
    L.append("\n</details>\n")
    slow = sorted(records, key=lambda r: -r["seconds"])[:10]
    L.append("Slowest TUs: " + ", ".join(f"`{r['tu']}` {r['seconds']:.0f}s" for r in slow) + "\n")
    with open(os.path.join(out_dir, "report.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(L))


def report_only(out_dir, compare_arg):
    """Rewrites a finished sweep's report, and its first-error classes, without compiling."""
    try:
        data, _ = load_results(out_dir)
        compare = load_results(compare_arg) if compare_arg else None
    except (OSError, ValueError) as e:
        print(f"engine_sweep: --report-only: {e}", file=sys.stderr)
        return 2
    meta, records = data["meta"], data["tus"]
    paths = meta.get("paths", {})
    namer = PathNamer(paths.get("sdk", DEFAULT_SDK), paths.get("repo_root", REPO_ROOT), paths.get("ndk"),
                      meta.get("clang_path", ""))
    extras = {}
    for r in records:
        if not r["ok"]:
            r["class"] = classify(r.get("first_error"), r.get("exit_code"), r.get("timed_out"))
        extras[r["tu"]] = parse_log_file(out_dir, r, namer)
    meta["compare"] = norm(os.path.abspath(compare_arg)) if compare_arg else None
    with open(os.path.join(out_dir, "results.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"meta": meta, "tus": records}, f, indent=1)
    write_report(out_dir, meta, records, extras, compare)
    print(f"engine_sweep: report rewritten: {norm(os.path.join(out_dir, 'report.md'))}", file=sys.stderr)
    return 0


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="See the module docstring and port/engine/README.md.")
    ap.add_argument("--jobs", "-j", type=int, default=os.cpu_count() or 4, help="parallel compilers (default: CPU count)")
    ap.add_argument("--out", default=DEFAULT_OUT, help="output directory (default buildstage/engine-sweep/latest)")
    ap.add_argument("--files", nargs="+", metavar="GLOB", help="only these TUs (paths or globs relative to src/sdk)")
    ap.add_argument("--rerun-failed", metavar="SWEEP", help="only the TUs that failed in that sweep (dir or results.json)")
    ap.add_argument("--compare", metavar="SWEEP", help="compare with an earlier sweep (dir or results.json)")
    ap.add_argument("--fail-on-regression", action="store_true",
                    help="exit 1 if a TU that compiled in --compare does not compile now")
    ap.add_argument("--merge", action="store_true",
                    help="update the TUs compiled now in --out's existing results instead of replacing them")
    ap.add_argument("--config", default="Release|x64", help="main.vcxproj configuration to read (default Release|x64)")
    ap.add_argument("--sdk", default=DEFAULT_SDK, help="the src/sdk directory to sweep (default: this checkout's)")
    ap.add_argument("--flags", default=DEFAULT_FLAGS, help="compile flags file (default port/engine/compile_flags.txt)")
    ap.add_argument("--shim", default=DEFAULT_SHIM, help="Android shim include directory (default port/engine/shim)")
    ap.add_argument("--no-shim", action="store_true", help="compile without the shim directory")
    ap.add_argument("--extra", action="append", default=[], metavar="ARG",
                    help="extra clang argument, appended last (repeatable; use --extra=-Wfoo)")
    ap.add_argument("--ndk", help="Android NDK directory (default: ANDROID_NDK_ROOT, then the SDK's ndk/)")
    ap.add_argument("--clang", help="clang++ to use instead of the NDK's")
    ap.add_argument("--timeout", type=int, default=600, help="seconds per TU (default 600)")
    ap.add_argument("--error-limit", type=int, default=0, help="clang -ferror-limit (default 0 = all errors)")
    ap.add_argument("--log-limit-mb", type=float, default=8,
                    help="keep at most this much of each TU's output in its log (default 8; 0 = all)")
    ap.add_argument("--keep-objects", action="store_true", help="keep the .o files under <out>/obj")
    ap.add_argument("--list", action="store_true", help="print the selected TUs and exit")
    ap.add_argument("--show-command", action="store_true", help="print the clang command of each selected TU and exit")
    ap.add_argument("--report-only", action="store_true",
                    help="rewrite --out's report.md (and the classes in its results.json) from its results "
                         "and logs, compiling nothing; takes --compare")
    args = ap.parse_args()

    if args.jobs < 1:
        ap.error("--jobs must be at least 1")
    if args.report_only:
        return report_only(os.path.abspath(args.out), args.compare)
    # The on-disk spelling: clang warns (-Wnonportable-include-path) about every
    # forced include whose path differs in case from the file system.
    sdk = os.path.realpath(args.sdk)
    vcxproj = os.path.join(sdk, "main.vcxproj")
    try:
        project = Project(vcxproj, args.config)
    except ProjectError as e:
        print(f"engine_sweep: {e}", file=sys.stderr)
        return 2
    all_tus = [t for t, _ in project.items]
    selection = "all"
    tus = list(all_tus)
    if args.files:
        tus, missing = select_tus(all_tus, args.files)
        if missing:
            print("engine_sweep: no TU of main.vcxproj (" + args.config + ") matches: " + ", ".join(missing),
                  file=sys.stderr)
            excluded_hits = [p for p in missing if any(fnmatch.fnmatchcase(e.lower(), fwd(p).lower())
                                                        for e in project.excluded)]
            if excluded_hits:
                print("  (excluded from that build: " + ", ".join(excluded_hits) + ")", file=sys.stderr)
            return 2
        selection = "--files " + " ".join(args.files)
    if args.rerun_failed:
        try:
            _, prev = load_results(args.rerun_failed)
        except (OSError, ValueError) as e:
            print(f"engine_sweep: --rerun-failed: {e}", file=sys.stderr)
            return 2
        failed_before = {t for t, r in prev.items() if not r["ok"]}
        tus = [t for t in tus if t in failed_before]
        selection = (selection + ", " if selection != "all" else "") + f"failed in {args.rerun_failed}"
    if args.list:
        print("\n".join(tus))
        print(f"{len(tus)} of {len(all_tus)} TUs ({len(project.excluded)} excluded from the build)", file=sys.stderr)
        return 0
    if not tus:
        print("engine_sweep: nothing selected", file=sys.stderr)
        return 2

    clang, ndk_dir = find_clang(args.ndk, args.clang)
    flag_args, drop_defines, drop_includes = read_flags_file(args.flags)
    shim = None if args.no_shim else os.path.realpath(args.shim)
    out_dir = os.path.abspath(args.out)
    repo_root_prop = project.props.get("FafRepoRoot")
    repo_root = os.path.normpath(repo_root_prop) if repo_root_prop else os.path.normpath(os.path.join(sdk, "..", ".."))
    commands = build_commands(project, tus, clang, flag_args, drop_defines, drop_includes, shim, out_dir,
                              args.error_limit, args.extra)
    if args.show_command:
        for tu in tus:
            print(shlex.join(commands[tu]))
        return 0

    compare = None
    if args.compare:
        try:
            compare = load_results(args.compare)
        except (OSError, ValueError) as e:
            print(f"engine_sweep: --compare: {e}", file=sys.stderr)
            return 2

    # Never delete anything but what this script writes.
    previous = {}
    if os.path.isfile(os.path.join(out_dir, "results.json")):
        try:
            _, previous = load_results(out_dir)
        except (OSError, ValueError):
            previous = {}
    if not args.merge:
        for sub in ("logs", "obj"):
            shutil.rmtree(os.path.join(out_dir, sub), ignore_errors=True)
        for name in ("results.json", "report.md"):
            try:
                os.remove(os.path.join(out_dir, name))
            except OSError:
                pass
    else:
        for tu in tus:
            try:
                os.remove(os.path.join(out_dir, "logs", artifact_rel(tu) + ".log"))
            except OSError:
                pass
    os.makedirs(out_dir, exist_ok=True)

    namer = PathNamer(sdk, repo_root, ndk_dir, clang)
    # Longest first, so one slow TU does not finish alone at the end: by the
    # last measured time where there is one, else by source size.
    timing = {}
    for src in (previous, compare[1] if compare else {}):
        for t, r in src.items():
            timing.setdefault(t, r.get("seconds"))

    def cost(tu):
        if timing.get(tu) is not None:
            return (1, timing[tu])
        try:
            return (0, os.path.getsize(os.path.join(sdk, tu)))
        except OSError:
            return (0, 0)
    order = sorted(tus, key=cost, reverse=True)

    print(f"engine_sweep: {len(tus)} TUs, {args.jobs} jobs, {fwd(clang)}", file=sys.stderr)
    t0 = time.monotonic()
    records, extras = {}, {}
    lock = threading.Lock()
    done = [0, 0]
    step = max(10, len(tus) // 20)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(compile_tu, tu, commands[tu], out_dir, args.timeout, args.keep_objects,
                               namer, sdk, int(args.log_limit_mb * 1024 * 1024)): tu for tu in order}
        for fut in concurrent.futures.as_completed(futures):
            rec, extra = fut.result()
            with lock:
                records[rec["tu"]] = rec
                extras[rec["tu"]] = extra
                done[0] += 1
                done[1] += rec["ok"]
                if done[0] % step == 0 or done[0] == len(tus):
                    print(f"  [{done[0]:4d}/{len(tus)}] ok {done[1]:4d}   {time.monotonic() - t0:6.0f}s",
                          file=sys.stderr)
    wall = time.monotonic() - t0

    # Results in project order; with --merge, the earlier run's other TUs stay.
    if args.merge and previous:
        merged = dict(previous)
        merged.update(records)
        for tu in merged:
            if tu not in extras:
                extras[tu] = parse_log_file(out_dir, merged[tu], namer)
        final = [merged[t] for t in all_tus if t in merged]
    else:
        final = [records[t] for t in all_tus if t in records]

    common_args = []
    sample = commands[tus[0]]
    skip_next = False
    for i, a in enumerate(sample):
        if skip_next:
            skip_next = False
            continue
        if i == 0:
            continue
        if a in ("-c", "-o"):
            skip_next = True
            continue
        common_args.append(a)
    with open(args.flags, "rb") as f:
        flags_sha = hashlib.sha256(f.read()).hexdigest()[:12]
    # Local changes to the sweep's own inputs, which the result depends on.
    dirty = git("status", "--porcelain", "--", "src/sdk", "port/engine", "scripts/port/engine_sweep.py")
    meta = {
        "results_version": RESULTS_VERSION,
        "date": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
        "git": (git("rev-parse", "--short", "HEAD") or "?") + (f" + {len(dirty.splitlines())} changed files" if dirty else ""),
        "project": norm(vcxproj) + ("" if norm(sdk).lower() == norm(DEFAULT_SDK).lower()
                                    else " (not this checkout's src/sdk)"),
        "configuration": args.config,
        "selection": selection + (" (merged into the earlier results)" if args.merge and previous else ""),
        "tus_in_project": len(all_tus),
        "excluded_from_build": project.excluded,
        "not_compiled_items": project.skipped,
        "clang": clang_version(clang),
        "clang_path": fwd(clang),
        "flags_file": f"{norm(args.flags)} (sha256 {flags_sha})",
        "shim": norm(shim) + ("" if os.path.isdir(shim) else " (does not exist)") if shim else "none (--no-shim)",
        "jobs": args.jobs,
        "wall_seconds": round(wall, 1),
        "command_line": shlex.join(["python", "scripts/port/engine_sweep.py"] + sys.argv[1:]),
        "compare": norm(os.path.abspath(args.compare)) if args.compare else None,
        "common_args": " ".join(shlex.quote(a) for a in common_args),
        # For --report-only: how this sweep's paths were shortened.
        "paths": {"sdk": norm(sdk), "repo_root": norm(repo_root), "ndk": norm(ndk_dir) if ndk_dir else None},
        "unknown_msbuild_properties": sorted(project.unknown_props),
    }
    with open(os.path.join(out_dir, "results.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"meta": meta, "tus": final}, f, indent=1)
    write_report(out_dir, meta, final, extras, compare)

    ok = sum(1 for r in final if r["ok"])
    print(f"engine_sweep: {ok}/{len(final)} TUs compile ({wall:.0f}s). Report: {norm(os.path.join(out_dir, 'report.md'))}",
          file=sys.stderr)
    for c, n in collections.Counter(r["class"] for r in final if not r["ok"]).most_common(8):
        print(f"  {n:5d}  {c}", file=sys.stderr)
    if compare is not None:
        regressions = [r["tu"] for r in final if r["tu"] in compare[1] and compare[1][r["tu"]]["ok"] and not r["ok"]]
        newly_ok = [r["tu"] for r in final if r["tu"] in compare[1] and not compare[1][r["tu"]]["ok"] and r["ok"]]
        print(f"  vs {args.compare}: +{len(newly_ok)} newly compile, -{len(regressions)} no longer compile",
              file=sys.stderr)
        for t in regressions[:20]:
            print(f"    REGRESSION {t}", file=sys.stderr)
        if regressions and args.fail_on_regression:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
