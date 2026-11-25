#!/usr/bin/env python3
"""The sim thread's profile from the game app (profile.txt), symbolized, as a flat and a cumulative profile.

The app samples the sim thread 200 times a second of its CPU time and writes profile.txt beside the game's report
(android/native/README.md, The sim's profile), which Share results and the FAF client's Diagnostics take into their zips.
This checks the profile's build id against the release's unstripped libfafgame.so, symbolizes the library's frames with
the NDK's llvm-symbolizer and prints

- the flat profile: the samples by the function the thread was in, the leaf (its own time);
- the cumulative profile: the samples by each function of a sample's short stack (the leaf and up to four callers, as
  their frame pointers chain them), once a sample, out of the samples the listed stacks hold;
- the samples by module.

A system library's frames (libm.so, libc.so) are named by their dynamic symbols, as the phone found them; the others stay
module+offset. It needs nothing but Python and the NDK's llvm-symbolizer.

    python -I android/tools/sim_profile.py <profile.txt> --libs <folder> [--top 40] [--any-build]

--libs: the build's unstripped libraries, as libfafgame.so or libfafgame-arm64-v8a.so (buildstage/releases/android-v<ver>/
for a release, build/android-arm64/bin for your own build). Without it, libfafgame.so's frames stay module+offset.
"""

import argparse
import glob
import json
import os
import re
import struct
import subprocess
import sys
from collections import Counter

MAGIC = "faf-re sim profile 1"
NT_GNU_BUILD_ID = 3


class Profile:
    """profile.txt as its writer (include/SimProfile.h, Profile::Text) has it."""

    def __init__(self, header, leaves, stacks, names):
        self.header = header  # {key: value} of its head
        self.leaves = leaves  # [(samples, (module, offset))]
        self.stacks = stacks  # [(samples, [(module, offset), ...])], the leaf first
        self.names = names  # {(module, offset): "sinl+0x40"}, of the modules without their files

    def samples(self):
        match = re.match(r"(\d+)", self.header.get("samples", ""))
        return int(match.group(1)) if match else sum(count for count, _ in self.leaves)

    def build_id(self):
        return self.header.get("build id", "").split(" ")[0]

    def library(self):
        match = re.search(r"\(([^,)]+)", self.header.get("build id", ""))
        return match.group(1) if match else "libfafgame.so"


def frame(text):
    """("libm.so", 0x40) of "libm.so+0x40"."""
    module, plus, offset = text.rpartition("+0x")
    if not plus:
        raise ValueError(f"not a frame: {text!r}")
    return module, int(offset, 16)


def parse(text):
    lines = text.splitlines()
    if not lines or not lines[0].startswith(MAGIC):
        raise ValueError(f"not a profile: its first line is not {MAGIC!r}")
    header = {}
    at = 1
    while at < len(lines) and lines[at]:
        key, _, value = lines[at].partition(": ")
        header[key] = value
        at += 1
    sections = {}
    current = None
    for line in lines[at:]:
        if not line:
            current = None
        elif current is None:
            current = sections.setdefault(line.split(":", 1)[0], [])
        else:
            current.append(line)
    leaves = []
    for line in sections.get("leaves", []):
        count, _share, module, offset = line.split()
        leaves.append((int(count), (module, int(offset, 16))))
    stacks = []
    for line in sections.get("stacks", []):
        count, *frames = line.split()
        stacks.append((int(count), [frame(part) for part in frames]))
    names = {}
    for line in sections.get("symbols", []):
        where, _, name = line.partition(" ")
        names[frame(where)] = name
    return Profile(header, leaves, stacks, names)


def elf_build_id(path):
    """The GNU build id of an ELF file, in hex, or None: its PT_NOTE segments' NT_GNU_BUILD_ID."""
    with open(path, "rb") as file:
        data = file.read(1 << 20)  # the notes are near the start, in the first segment
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        return None  # 64-bit little-endian, as both ABIs are
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    for index in range(phnum):
        kind, _flags, offset, _vaddr, _paddr, size = struct.unpack_from("<IIQQQQ", data, phoff + index * phentsize)
        if kind != 4:  # PT_NOTE
            continue
        at, end = offset, offset + size
        while at + 12 <= end:
            namesz, descsz, note = struct.unpack_from("<III", data, at)
            name = at + 12
            desc = name + ((namesz + 3) & ~3)
            if note == NT_GNU_BUILD_ID and data[name : name + namesz] == b"GNU\x00":
                return data[desc : desc + descsz].hex()
            at = desc + ((descsz + 3) & ~3)
    return None


def find_symbolizer():
    home = os.environ.get("ANDROID_HOME", r"C:\Android\sdk")
    found = sorted(glob.glob(os.path.join(home, "ndk", "*", "toolchains", "llvm", "prebuilt", "*", "bin", "llvm-symbolizer*")))
    if not found:
        sys.exit(f"no llvm-symbolizer under {home}/ndk: set ANDROID_HOME")
    return found[-1]


def find_library(folder, name):
    """libfafgame.so in `folder`, as it is or with its ABI (libfafgame-arm64-v8a.so)."""
    stem = name[:-3]
    for candidate in [name, *sorted(os.path.basename(p) for p in glob.glob(os.path.join(folder, stem + "-*.so")))]:
        path = os.path.join(folder, candidate)
        if os.path.isfile(path):
            return path
    return None


def source(path):
    """A source's path from the repository's root, where it has one."""
    match = re.search(r"[/\\]((?:src|dependencies|android)[/\\].*)$", path)
    return (match.group(1) if match else os.path.basename(path)).replace("\\", "/")


def symbolize(library, offsets):
    """{offset: (function, its source)} of the library's offsets: what llvm-symbolizer names, without inlined frames."""
    ordered = sorted(offsets)
    if not ordered:
        return {}
    output = subprocess.run(
        [find_symbolizer(), f"--obj={library}", "--relative-address", "--functions=linkage", "--no-inlines",
         "--output-style=JSON"],
        input="".join(f"{offset:#x}\n" for offset in ordered),
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    names = {}
    for offset, line in zip(ordered, output.splitlines()):
        symbols = json.loads(line).get("Symbol") or [{}]
        function = symbols[0].get("FunctionName", "")
        if function and function != "??":
            names[offset] = (function, source(symbols[0].get("StartFileName") or symbols[0].get("FileName", "")))
    return names


class Names:
    """A frame's function: the library's by llvm-symbolizer, another module's by its dynamic symbol, else module+offset."""

    def __init__(self, profile, library, symbols):
        self.profile = profile
        self.library = library
        self.symbols = symbols  # {offset: (function, source)} of the library

    def __call__(self, where):
        module, offset = where
        if module == self.library and offset in self.symbols:
            return self.symbols[offset]
        name = self.profile.names.get(where)
        if name:
            return (f"{name.rpartition('+0x')[0] or name} ({module})", "")
        return (f"{module}+{offset:#x}", "")


def flat(profile, name_of):
    """[(samples, function, source)] by the function the thread was in, the most first."""
    counts = Counter()
    sources = {}
    for count, where in profile.leaves:
        function, where_from = name_of(where)
        counts[function] += count
        sources.setdefault(function, where_from)
    return [(count, function, sources[function]) for function, count in counts.most_common()]


def cumulative(profile, name_of):
    """[(samples, function, source)] by each function of a sample's short stack, once a sample, the most first."""
    counts = Counter()
    sources = {}
    for count, frames in profile.stacks:
        seen = set()
        for where in frames:
            function, where_from = name_of(where)
            if function not in seen:
                seen.add(function)
                counts[function] += count
                sources.setdefault(function, where_from)
    return [(count, function, sources[function]) for function, count in counts.most_common()]


def modules(profile):
    counts = Counter()
    for count, (module, _offset) in profile.leaves:
        counts[module] += count
    return counts.most_common()


def table(title, rows, total, top):
    print(f"\n{title}")
    print(f"  {'share':>7}  {'samples':>8}  function")
    for count, function, where_from in rows[:top]:
        share = 100.0 * count / total if total else 0.0
        print(f"  {share:6.2f}%  {count:8d}  {function}" + (f"  ({where_from})" if where_from else ""))


def held(rows, total):
    return 100.0 * sum(count for count, *_ in rows) / total if total else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("profile")
    parser.add_argument("--libs", help="the folder of the build's unstripped libraries")
    parser.add_argument("--top", type=int, default=40, help="the rows of each profile (40)")
    parser.add_argument("--any-build", action="store_true", help="symbolize against a library of another build too")
    arguments = parser.parse_args()
    with open(arguments.profile, encoding="utf-8") as file:
        profile = parse(file.read())

    library_name = profile.library()
    symbols = {}
    if arguments.libs:
        library = find_library(arguments.libs, library_name)
        if library is None:
            sys.exit(f"no {library_name} in {arguments.libs}")
        found = elf_build_id(library)
        if found != profile.build_id() and not arguments.any_build:
            sys.exit(f"{library} has build id {found}, the profile's is {profile.build_id()}: another build's library "
                     "(--any-build symbolizes against it anyway)")
        offsets = {offset for _, (module, offset) in profile.leaves if module == library_name}
        offsets |= {offset for _, frames in profile.stacks for module, offset in frames if module == library_name}
        symbols = symbolize(library, offsets)
    name_of = Names(profile, library_name, symbols)

    total = profile.samples()
    for key in ("build id", "rate", "samples", "cpu", "cost", "written"):
        if key in profile.header:
            print(f"{key}: {profile.header[key]}")
    leaves = flat(profile, name_of)
    table(f"flat: the samples by the function the thread was in (the listed leaves hold {held(leaves, total):.2f}% of them)",
          leaves, total, arguments.top)
    stacks = [(count,) for count, _ in profile.stacks]
    table("cumulative: the samples by each function of the leaf and its callers, up to five, once a sample (the listed "
          f"stacks hold {held(stacks, total):.2f}% of them)", cumulative(profile, name_of), total, arguments.top)
    print("\nmodules: the samples by the module the thread was in")
    for module, count in modules(profile):
        print(f"  {100.0 * count / total if total else 0.0:6.2f}%  {count:8d}  {module}")


if __name__ == "__main__":
    main()
