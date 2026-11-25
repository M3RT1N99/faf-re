#!/usr/bin/env python3
"""Tests of sim_profile.py: profile.txt as the app writes it (include/SimProfile.h, whose test pins the same text), the
flat and the cumulative profile, and an ELF file's build id. Run: python -I android/tools/test_sim_profile.py"""

import os
import struct
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # -I leaves the script's folder out
import sim_profile  # noqa: E402

# tests/griff/SimProfileTests.cpp's TheProfileCountsLeavesAndStacksAndWritesProfileTxt.
PROFILE = (
    "faf-re sim profile 1\n"
    "build id: 4433a795d16bab81 (libfafgame.so, arm64-v8a)\n"
    "rate: 2 Hz of the sim thread's CPU time, its frames by frame pointer\n"
    "samples: 6 in 2 sims over 1342 ticks, 1 dropped\n"
    "cpu: 3.5 s of the sim thread's time sampled\n"
    "cost: 100.0 us a sample in the handler, 0.020% of the sim thread's time; 0.21 s on the counting thread\n"
    "written: 125 s after the profiler began\n"
    "leaves: 2, the top 2 with 100.00% of the samples\n"
    "stacks: 3, the top 2 with 83.33% of the samples\n"
    "\n"
    "leaves: samples, share, module, offset\n"
    "4 66.67% libfafgame.so 0x1000\n"
    "2 33.33% libm.so 0x40\n"
    "\n"
    "stacks: samples, then the leaf and its callers' calls as module+offset\n"
    "3 libfafgame.so+0x1000 libfafgame.so+0x2000 libfafgame.so+0x3000\n"
    "2 libm.so+0x40 libfafgame.so+0x2000\n"
    "\n"
    "symbols: the frames of modules without their files, by their dynamic symbols\n"
    "libm.so+0x40 sinl+0x40\n"
)


class ProfileTest(unittest.TestCase):
    def test_the_apps_text_reads_back(self):
        profile = sim_profile.parse(PROFILE)
        self.assertEqual(profile.build_id(), "4433a795d16bab81")
        self.assertEqual(profile.library(), "libfafgame.so")
        self.assertEqual(profile.samples(), 6)
        self.assertEqual(profile.leaves, [(4, ("libfafgame.so", 0x1000)), (2, ("libm.so", 0x40))])
        self.assertEqual(profile.stacks, [
            (3, [("libfafgame.so", 0x1000), ("libfafgame.so", 0x2000), ("libfafgame.so", 0x3000)]),
            (2, [("libm.so", 0x40), ("libfafgame.so", 0x2000)]),
        ])
        self.assertEqual(profile.names, {("libm.so", 0x40): "sinl+0x40"})

    def test_another_text_is_no_profile(self):
        with self.assertRaises(ValueError):
            sim_profile.parse("faf-re sim profile 2\n")
        with self.assertRaises(ValueError):
            sim_profile.parse("")

    def test_the_flat_profile_is_by_the_leafs_function_and_the_cumulative_by_each_of_a_stacks_once(self):
        profile = sim_profile.parse(PROFILE)
        # 0x1000 and 0x3000 are in one function: the cumulative counts it once for the stack that has both.
        symbols = {0x1000: ("Spin", "src/a.cpp"), 0x2000: ("Tick", "src/b.cpp"), 0x3000: ("Spin", "src/a.cpp")}
        name_of = sim_profile.Names(profile, "libfafgame.so", symbols)
        self.assertEqual(sim_profile.flat(profile, name_of), [(4, "Spin", "src/a.cpp"), (2, "sinl (libm.so)", "")])
        self.assertEqual(sim_profile.cumulative(profile, name_of),
                         [(5, "Tick", "src/b.cpp"), (3, "Spin", "src/a.cpp"), (2, "sinl (libm.so)", "")])
        self.assertEqual(sim_profile.modules(profile), [("libfafgame.so", 4), ("libm.so", 2)])
        # Without the library's symbols a frame is its module and offset.
        bare = sim_profile.Names(profile, "libfafgame.so", {})
        self.assertEqual(sim_profile.flat(profile, bare)[0], (4, "libfafgame.so+0x1000", ""))

    def test_a_libraries_build_id_is_its_gnu_note(self):
        note = struct.pack("<III", 4, 4, 3) + b"GNU\x00" + bytes.fromhex("4433a795")
        phoff = 64
        notes_at = phoff + 56
        header = bytearray(64)
        header[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<Q", header, 0x20, phoff)
        struct.pack_into("<HH", header, 0x36, 56, 1)
        program = struct.pack("<IIQQQQQQ", 4, 4, notes_at, notes_at, notes_at, len(note), len(note), 4)
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "libfafgame.so")
            with open(path, "wb") as file:
                file.write(bytes(header) + program + note)
            self.assertEqual(sim_profile.elf_build_id(path), "4433a795")
            self.assertEqual(sim_profile.find_library(folder, "libfafgame.so"), path)
            with open(path, "wb") as file:
                file.write(b"not an ELF file")
            self.assertIsNone(sim_profile.elf_build_id(path))

    def test_without_a_library_the_script_prints_the_frames_as_they_are(self):
        with tempfile.TemporaryDirectory() as folder:
            path = os.path.join(folder, "profile.txt")
            with open(path, "w", encoding="utf-8") as file:
                file.write(PROFILE)
            out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "sim_profile.py"), path], capture_output=True,
                                 text=True, check=True).stdout
        self.assertIn("   66.67%         4  libfafgame.so+0x1000\n", out)
        self.assertIn("   33.33%         2  sinl (libm.so)\n", out)
        self.assertIn("the listed stacks hold 83.33% of them", out)


if __name__ == "__main__":
    unittest.main()
