#!/usr/bin/env python3
"""Engine addition (dedicated-server plan WPL.13): the faf_sim objects call no host libm
transcendental outside the reviewed list.

A transcendental from the C library (sin, cos, tan, atan2, exp, log, pow, ...) can round
differently on Windows (UCRT) and Linux (glibc), so the simulation's arithmetic goes through
the engine's own implementations (legacy/math/X87Math). This gate lists every function in the
faf_sim objects that calls one (nm -u, then objdump relocations) and fails on any pair
(symbol, function) that tests/linux/libm_allowlist.txt does not name. Static initialisers
calling sin/cos are allowed as a class: they are the Wm3 axis-angle constants of
QuaternionMath.h, whose float results are millions of ulps from any rounding boundary (WPR.57).

Usage: LibmGate.py <allowlist> <object>|@<file with one object per line>...

FAF_NM and FAF_OBJDUMP name the tools (default nm and objdump); cmake/FafLinux.cmake passes
the toolchain's, which read x86-64 and AArch64 objects alike.
"""
import os
import re
import subprocess
import sys

NM = os.environ.get("FAF_NM") or "nm"
OBJDUMP = os.environ.get("FAF_OBJDUMP") or "objdump"

TRANSCENDENTALS = {
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "exp", "exp2", "expm1", "log", "log2",
    "log10", "log1p", "logb", "pow", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh", "cbrt",
    "erf", "erfc", "hypot", "lgamma", "tgamma",
}
TRANSCENDENTALS |= {name + suffix for name in list(TRANSCENDENTALS) for suffix in ("f", "l")}


def load_allowlist(path):
    allowed = set()
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if line:
            symbol, function = line.split(" ", 1)
            allowed.add((symbol, function.strip()))
    return allowed


def calls(obj):
    """(symbol, demangled caller) for every transcendental this object calls."""
    undefined = subprocess.run([NM, "-u", obj], capture_output=True, text=True).stdout.split()
    if not TRANSCENDENTALS.intersection(undefined):
        return set()
    listing = subprocess.run([OBJDUMP, "-dr", "--no-show-raw-insn", "-C", obj], capture_output=True, text=True).stdout
    found, function = set(), "?"
    for line in listing.splitlines():
        head = re.match(r"^[0-9a-f]+ <(.*)>:$", line)
        if head:
            # The allowlist spells libc++'s inline namespace as Linux does (std::__1);
            # the NDK's libc++ names it std::__ndk1.
            function = head.group(1).replace("std::__ndk1::", "std::__1::")
            continue
        reloc = re.search(r"R_(?:X86_64_(?:PLT32|PC32|GOTPCREL\w*)|AARCH64_(?:CALL26|JUMP26))\s+(\w+)(?:[-+]0x[0-9a-f]+)?$", line)
        if reloc and reloc.group(1) in TRANSCENDENTALS:
            found.add((reloc.group(1), function))
    return found


def objects(arguments):
    """The object paths, with each @<file> argument replaced by the lines of that file."""
    for argument in arguments:
        if argument.startswith("@"):
            yield from (line.strip() for line in open(argument[1:], encoding="utf-8") if line.strip())
        else:
            yield argument


def main():
    allowed = load_allowlist(sys.argv[1])
    unexpected = []
    paths = list(objects(sys.argv[2:]))
    for obj in paths:
        for symbol, function in sorted(calls(obj)):
            if function.startswith("_GLOBAL__sub_I_") and symbol in ("sin", "cos"):
                continue
            if (symbol, function) not in allowed:
                unexpected.append(f"{symbol} {function}    ({obj})")
    if unexpected:
        print("libm gate: transcendental calls not in tests/linux/libm_allowlist.txt:")
        for line in unexpected:
            print("  " + line)
        return 1
    print(f"libm gate: {len(paths)} objects, no unreviewed transcendental call")
    return 0


if __name__ == "__main__":
    sys.exit(main())
