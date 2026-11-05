"""The census per Lua file: how large its largest control is, and how small what a finger acts on gets.
Usage: census_files.py <rows file>"""
import re
import sys

sys.stdout.reconfigure(errors="replace")
PX_PER_MM = 10.5
files = {}
for line in open(sys.argv[1], encoding="latin-1"):
    m = re.search(r"CENSUS (\d+\.\d+) (\S+) (\w+) (-?\d+) (-?\d+) (\d+) (\d+) (\S+) ?(.*)$", line)
    if not m:
        continue
    scale, place, kind, x, y, w, h, flags, name = m.groups()
    x, y, w, h = int(x), int(y), int(w), int(h)
    # the file that made it: the first in the chain outside lua/ui/controls
    chain = [p.split(":")[0] for p in place.split("<")]
    owner = next((p for p in chain if "controls/" not in p), chain[0])
    entry = files.setdefault(owner, {"big": (0, 0, 0, 0), "live": [], "count": 0})
    entry["count"] += 1
    if w * h > entry["big"][0] * entry["big"][1] and w < 1640:
        entry["big"] = (w, h, x, y)
    # Text controls all carry a handler: count only those that are not plain labels by size
    if ("e" in flags or kind in ("ItemList", "Edit", "Scrollbar")) and "h" in flags and kind != "Text" and min(w, h) >= 4:
        entry["live"].append((min(w, h), w, h, kind, place.split("<")[0].split("/")[-1]))
print("file | controls | largest (w x h at x,y) | live | under 4 mm | under 7 mm | the smallest")
for owner, e in sorted(files.items()):
    live = sorted(e["live"])
    if not live:
        continue
    under4 = sum(1 for r in live if r[0] / PX_PER_MM < 4)
    under7 = sum(1 for r in live if r[0] / PX_PER_MM < 7)
    small = ", ".join(f"{r[1]}x{r[2]} {r[3][:4]}" for r in live[:3])
    b = e["big"]
    print(f"{owner[:34]:34} {e['count']:4} {b[0]:5}x{b[1]:<4}@{b[2]},{b[3]:<4} {len(live):4} {under4:4} {under7:4}  {small}")
