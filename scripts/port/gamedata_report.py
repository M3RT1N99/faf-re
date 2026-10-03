#!/usr/bin/env python3
"""Prints the game data tables of docs/port/gamedata.md from the manifest.

Reads port/data/gamedata.json and, for the "this install" columns, a local
Supreme Commander: Forged Alliance install and FAF client directory. It
applies the manifest's selection rule the same way the deploy script, the
launcher's importer and the native runtime do, and never writes anything.

usage: python scripts/port/gamedata_report.py [--scfa DIR] [--faf DIR] [--manifest FILE]

Without --scfa, the install is taken from the FAF client's fa_path.lua (as
scripts/port/deploy_android.ps1 does). Paste the output into the doc; the doc
is the committed text, this script only keeps its numbers honest.
"""

import argparse
import hashlib
import json
import os
import re
import stat
import sys

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
TIERS = ("required", "recommended", "optional")


def format_size(size):
    """Binary units, as the deploy script prints them."""
    for unit, factor in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if size >= factor:
            return f"{size / factor:.1f} {unit}" if unit != "GB" else f"{size / factor:.2f} {unit}"
    return f"{size} B"


def wildcard(pattern):
    """'*' and '?', case-insensitive, one name: the manifest's pattern rule."""
    return re.compile("^" + re.escape(pattern).replace(r"\*", ".*").replace(r"\?", ".") + "$", re.IGNORECASE)


def is_hidden(path):
    # Windows Explorer clutter (desktop.ini, Thumbs.db) is hidden/system and
    # skipped by the deploy script; mirror that so the numbers agree.
    attributes = getattr(os.stat(path), "st_file_attributes", 0)
    return bool(attributes & (stat.FILE_ATTRIBUTE_HIDDEN | stat.FILE_ATTRIBUTE_SYSTEM)) if attributes else False


def resolve_case_insensitive(base, relative):
    """Walks `relative` below `base` one component at a time, ignoring case."""
    current = base
    for component in [c for c in relative.split("/") if c]:
        if not os.path.isdir(current):
            return None
        names = [n for n in os.listdir(current) if os.path.isdir(os.path.join(current, n))]
        exact = [n for n in names if n == component]
        folded = [n for n in names if n.lower() == component.lower()]
        if not exact and not folded:
            return None
        current = os.path.join(current, (exact or folded)[0])
    return current if os.path.isdir(current) else None


def tree_size(path):
    total, count = 0, 0
    for root, directories, files in os.walk(path):
        directories[:] = [d for d in directories if not is_hidden(os.path.join(root, d))]
        for name in files:
            full = os.path.join(root, name)
            if not is_hidden(full):
                total += os.path.getsize(full)
                count += 1
    return total, count


def expand(entry, base):
    """(names, bytes, files, missing literal names) of one scfa/user entry below `base`."""
    directory = resolve_case_insensitive(base, entry["src"]) if base else None
    if directory is None:
        return None
    include = [wildcard(p) for p in entry["include"]]
    exclude = [wildcard(p) for p in entry.get("exclude", [])]
    want_dirs = entry["kind"] == "dir"
    names, total, count = [], 0, 0
    for name in sorted(os.listdir(directory)):
        full = os.path.join(directory, name)
        if os.path.isdir(full) != want_dirs or is_hidden(full):
            continue
        if not any(p.match(name) for p in include) or any(p.match(name) for p in exclude):
            continue
        names.append(name)
        if want_dirs:
            size, files = tree_size(full)
        else:
            size, files = os.path.getsize(full), 1
        total += size
        count += files
    literals = [p for p in entry["include"] if not re.search(r"[*?]", p) and not any(x.match(p) for x in exclude)]
    found = {n.lower() for n in names}
    missing = [p for p in literals if p.lower() not in found]
    return names, total, count, missing


def read_lua_string(path, name):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError:
        return None
    match = re.search(r'(?m)^\s*' + re.escape(name) + r'\s*=\s*"((?:[^"\\]|\\.)*)"', text)
    return re.sub(r"\\(.)", r"\1", match.group(1)) if match else None


def patterns(entry):
    text = ", ".join(f"`{p}`" for p in entry["include"])
    if entry.get("exclude"):
        text += " except " + ", ".join(f"`{p}`" for p in entry["exclude"])
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--manifest", default=os.path.join(REPO_ROOT, "port", "data", "gamedata.json"))
    parser.add_argument("--faf", default=r"C:\ProgramData\FAForever", help="FAF client data directory")
    parser.add_argument("--scfa", help="SCFA install (default: fa_path from the FAF client's fa_path.lua)")
    parser.add_argument("--hash", action="store_true", help="also check the FAF files' sha256")
    args = parser.parse_args()

    with open(args.manifest, encoding="utf-8") as handle:
        manifest = json.load(handle)
    faf = manifest["faf"]
    scfa_path = args.scfa or read_lua_string(os.path.join(args.faf, "fa_path.lua"), "fa_path")
    if scfa_path and not os.path.isdir(scfa_path):
        print(f"warning: SCFA install {scfa_path} not found; sizes from the manifest", file=sys.stderr)
        scfa_path = None
    totals = {tier: {"faf": 0, "scfa": 0} for tier in TIERS}

    print(f"### FAF files (game version {faf['version']})\n")
    print("| File | Destination | Tier | Size | Download name | Purpose |")
    print("| --- | --- | --- | ---: | --- | --- |")
    for item in faf["files"]:
        local = os.path.join(args.faf, item["local"])
        note = ""
        if os.path.isfile(local):
            if os.path.getsize(local) != item["size"]:
                note = " (local copy differs)"
            elif args.hash:
                with open(local, "rb") as handle:
                    if hashlib.sha256(handle.read()).hexdigest() != item["sha256"]:
                        note = " (local sha256 differs)"
        totals[item["tier"]]["faf"] += item["size"]
        remote = item["remote"].replace("{v}", str(faf["version"]))
        print(f"| `{item['name']}` | `{item['dest']}` | {item['tier']} | {format_size(item['size'])}{note} "
              f"| `{remote}` | {item['purpose']} |")

    print(f"\n### SCFA selection\n")
    print("| Entry | Source folder | Names | Kind | Tier | Mount | Files | Size | Purpose |")
    print("| --- | --- | --- | --- | --- | --- | ---: | ---: | --- |")
    for entry in manifest["scfa"]["entries"]:
        result = expand(entry, scfa_path)
        if result is None:
            files, size = "-", entry.get("approxBytes", 0)
            size_text = "~" + format_size(size)
        else:
            names, size, count, missing = result
            plural = "" if len(names) == 1 else "s"
            files = f"{len(names)} dir{plural}, {count}" if entry["kind"] == "dir" else str(count)
            size_text = format_size(size) + (f" (missing {', '.join(missing)})" if missing else "")
        totals[entry["tier"]]["scfa"] += size
        print(f"| `{entry['id']}` | `{entry['src']}/` | {patterns(entry)} | {entry['kind']} | {entry['tier']} "
              f"| `{entry['mount']}` | {files} | {size_text} | {entry['purpose']} |")

    print("\n### Totals per -Tier\n")
    print("| Tier | FAF | SCFA | This tier | Cumulative (what -Tier copies) |")
    print("| --- | ---: | ---: | ---: | ---: |")
    cumulative = 0
    for tier in TIERS:
        tier_total = totals[tier]["faf"] + totals[tier]["scfa"]
        cumulative += tier_total
        label = {"required": "required", "recommended": "recommended", "optional": "optional (`-Tier all`)"}[tier]
        print(f"| {label} | {format_size(totals[tier]['faf'])} | {format_size(totals[tier]['scfa'])} "
              f"| {format_size(tier_total)} | {format_size(cumulative)} |")
    if scfa_path:
        print(f"\nSCFA sizes measured on {scfa_path}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
