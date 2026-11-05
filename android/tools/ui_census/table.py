"""The census's rows as a table: what a finger can act on, the smallest first.
Usage: census_table.py <rows file> [substring of the place] [all]
Sizes in the UI's pixels and in mm on the user's phone at 1647x768 (10.5 pixels to the mm)."""
import re
import sys

PX_PER_MM = 10.5
sys.stdout.reconfigure(errors="replace")
rows = {}
for line in open(sys.argv[1], encoding="latin-1"):
    m = re.search(r"CENSUS (\d+\.\d+) (\S+) (\w+) (-?\d+) (-?\d+) (\d+) (\d+) (\S+) ?(.*)$", line)
    if not m:
        if "CENSUS error" in line or "CENSUS started" in line and False:
            print(line.strip()[:140])
        continue
    scale, place, kind, x, y, w, h, flags, name = m.groups()
    rows[(place, kind, w, h, flags)] = (float(scale), place, kind, int(x), int(y), int(w), int(h), flags, name.strip())

want = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] != "-" else ""
everything = len(sys.argv) > 3
LIVE_KINDS = {"ItemList", "Edit", "Scrollbar"}
out = []
for row in rows.values():
    scale, place, kind, x, y, w, h, flags, name = row
    if want and want not in place:
        continue
    live = "e" in flags or kind in LIVE_KINDS
    if not everything and (not live or "h" not in flags):
        continue
    if w < 2 or h < 2:
        continue
    out.append(row)
out.sort(key=lambda r: (min(r[5], r[6]), r[1]))
print(f"{len(out)} of {len(rows)} rows")
for scale, place, kind, x, y, w, h, flags, name in out:
    short = place.split("<")
    where = short[0] + (" <" + short[1] if len(short) > 1 else "")
    print(f"{min(w, h) / PX_PER_MM:4.1f}mm {w:4}x{h:<4} @{x:4},{y:<4} {kind[:6]:6} {flags:3} {where[:64]} {name[:18]}")
