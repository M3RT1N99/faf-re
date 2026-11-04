#!/usr/bin/env python3
"""A tombstone from the app's results zip, as text with its stacks symbolized.

The app keeps a native crash's tombstone (game-tombstone.pb, game-previous-tombstone.pb,
renderer-tombstone.pb), which is Android's protobuf (system/core/debuggerd/proto/tombstone.proto).
A phone has no adb, so this is the stack a tester can send. It needs nothing but Python and the
NDK's llvm-symbolizer.

    python -I android/tools/tombstone.py <file.pb> [--libs <folder>] [--all-threads]

--libs: the build's unstripped libraries, as libfafgame.so or libfafgame-arm64-v8a.so
(buildstage/releases/android-v<ver>/ for a release, build/android-arm64/bin for your own build).
Without it, frames show what the tombstone itself names.
"""

import argparse
import glob
import os
import re
import subprocess
import sys

# Field numbers of tombstone.proto.
TOMBSTONE_PID, TOMBSTONE_TID, TOMBSTONE_SIGNAL, TOMBSTONE_ABORT, TOMBSTONE_THREADS = 5, 6, 10, 14, 16
SIGNAL_NUMBER, SIGNAL_NAME, SIGNAL_CODE, SIGNAL_CODE_NAME, SIGNAL_HAS_ADDRESS, SIGNAL_ADDRESS = 1, 2, 3, 4, 7, 8
THREAD_ID, THREAD_NAME, THREAD_BACKTRACE = 1, 2, 4
FRAME_REL_PC, FRAME_FUNCTION, FRAME_FUNCTION_OFFSET, FRAME_FILE = 1, 4, 5, 6


def varint(data, at):
    shift = value = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return value, at


def fields(data):
    """A message's fields in order, as (number, value): an integer, or bytes for the rest."""
    at = 0
    out = []
    while at < len(data):
        key, at = varint(data, at)
        number, wire = key >> 3, key & 7
        if wire == 0:
            value, at = varint(data, at)
        elif wire == 2:
            length, at = varint(data, at)
            value = data[at : at + length]
            at += length
        elif wire == 1:
            value = data[at : at + 8]
            at += 8
        elif wire == 5:
            value = data[at : at + 4]
            at += 4
        else:
            raise ValueError(f"wire type {wire}")
        out.append((number, value))
    return out


def text(value):
    return value.decode("utf-8", "replace") if isinstance(value, bytes) else ""


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


def symbolize(frames, folder):
    """{(library, rel_pc): name} for the frames in libraries that `folder` has."""
    wanted = {}
    for library, rel_pc in frames:
        wanted.setdefault(library, set()).add(rel_pc)
    names = {}
    symbolizer = find_symbolizer()
    for library, addresses in wanted.items():
        path = find_library(folder, library)
        if path is None:
            continue
        ordered = sorted(addresses)
        output = subprocess.run(
            [symbolizer, f"--obj={path}", "--relative-address", "--functions=short", "--no-inlines"] + [hex(a) for a in ordered],
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        for address, block in zip(ordered, output.split("\n\n")):
            lines = block.strip().split("\n")
            if lines[0] != "??":
                where = lines[1].rsplit("faf-re", 1)[-1].lstrip("/\\") if len(lines) > 1 else ""
                names[(library, address)] = f"{lines[0]}  {where}"
    return names


def library_of(frame):
    # /data/app/.../base.apk!libfafgame.so and /data/app/.../lib/arm64/libfafgame.so alike.
    return re.split(r"[/!]", text(frame.get(FRAME_FILE, b"")))[-1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tombstone")
    parser.add_argument("--libs", help="the folder of the build's unstripped libraries")
    parser.add_argument("--all-threads", action="store_true")
    arguments = parser.parse_args()
    with open(arguments.tombstone, "rb") as file:
        top = fields(file.read())
    first = {}
    for number, value in top:
        first.setdefault(number, value)
    crashing = first.get(TOMBSTONE_TID)
    print(f"pid {first.get(TOMBSTONE_PID)}, crashing thread {crashing}")
    signal = dict(fields(first.get(TOMBSTONE_SIGNAL, b"")))
    address = f", address {signal.get(SIGNAL_ADDRESS, 0):#x}" if signal.get(SIGNAL_HAS_ADDRESS) else ""
    code = signal.get(SIGNAL_CODE, 0)
    code = code - (1 << 64) if code >= (1 << 63) else code  # an int32: a negative one comes sign-extended
    print(f"signal {signal.get(SIGNAL_NUMBER)} ({text(signal.get(SIGNAL_NAME))}), code {code} "
          f"({text(signal.get(SIGNAL_CODE_NAME))}){address}")
    if TOMBSTONE_ABORT in first:
        print("abort message:", text(first[TOMBSTONE_ABORT]))

    threads = []
    for number, value in top:
        if number == TOMBSTONE_THREADS:  # a map entry: 1 the key, 2 the thread
            thread = fields(dict(fields(value))[2])
            single = dict(thread)
            frames = [dict(fields(v)) for n, v in thread if n == THREAD_BACKTRACE]
            threads.append((single.get(THREAD_ID, 0), text(single.get(THREAD_NAME)), frames))
    threads.sort(key=lambda entry: (entry[0] != crashing, entry[0]))
    shown = threads if arguments.all_threads else [t for t in threads if t[0] == crashing]

    names = {}
    if arguments.libs:
        wanted = {(library_of(f), f.get(FRAME_REL_PC, 0)) for _, _, frames in shown for f in frames}
        names = symbolize({key for key in wanted if key[0].endswith(".so")}, arguments.libs)
    for tid, name, frames in shown:
        print(f"\nthread {tid} '{name}'{' (crashed)' if tid == crashing else ''}")
        for index, frame in enumerate(frames):
            library = library_of(frame)
            rel_pc = frame.get(FRAME_REL_PC, 0)
            function = text(frame.get(FRAME_FUNCTION))
            told = f"{function}+{frame.get(FRAME_FUNCTION_OFFSET, 0)}" if function else ""
            print(f"  #{index:02d} {library}+{rel_pc:#x}  {names.get((library, rel_pc), told)}")


if __name__ == "__main__":
    main()
