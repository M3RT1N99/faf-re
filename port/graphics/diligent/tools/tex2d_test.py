"""GetTexture2D unit test (M6b gate, component R): DXT5 blocks byte-identical to D3DX.

Builds tools/tex2d_test.cpp (x86, against dependencies/DXSDK_D3DX and the backend's
Texture2DPortable.cpp) and runs it over the files GetTexture2D decodes:

  --dump DIR   the sources main.exe wrote with /galdumptex (tex2d_<n>_<fnv>.bin, with the engine's
               output in .out next to each): exactly the files the run fed GetTexture2D. With --ui-index
               every source is named by its VFS path (matched by content hash against the archives).
  --files DIR  every image under DIR (for example all of /textures/ui extracted into scratch).

Each file is decoded three ways - the D3D9 backend's code on a D3D9 HAL device (the reference), the
same calls on a NULLREF device (the Diligent backend's oracle mode), and the portable decoder - and the
DXT5 blocks and sizes are compared byte for byte; a dump's engine output is compared too.

Game data stays out of the repository: point --dump/--files/--out at scratch or buildstage.

  python port/graphics/diligent/tools/tex2d_test.py --dump <scratch>/tex --ui-index <scratch>/ui/index.json --out <scratch>/tex2d
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
VCVARS32 = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
DXSDK = os.path.join(REPO, "dependencies", "DXSDK_D3DX")
BELOW_NORMAL = 0x00004000


def fnv1a(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def build(out_dir):
    exe = os.path.join(out_dir, "tex2d_test.exe")
    sources = [os.path.join(HERE, "tex2d_test.cpp"), os.path.join(REPO, "port", "graphics", "diligent", "Texture2DPortable.cpp")]
    if os.path.isfile(exe) and all(os.path.getmtime(exe) >= os.path.getmtime(s) for s in sources):
        return exe
    os.makedirs(out_dir, exist_ok=True)
    # The x86 developer environment from vcvars32.bat (a fixed path), as tools/gate1.py reads it.
    dump = subprocess.run(f'cmd /s /c ""{VCVARS32}" >nul && set"', capture_output=True, text=True)
    env = dict(line.split("=", 1) for line in dump.stdout.splitlines() if "=" in line)
    env["INCLUDE"] = env.get("INCLUDE", "") + ";" + os.path.join(DXSDK, "include")
    cl = shutil.which("cl", path=env.get("PATH") or env.get("Path"))
    if cl is None:
        raise SystemExit("cl.exe not found through vcvars32.bat")
    command = [cl, "/nologo", "/EHsc", "/std:c++17", "/W3", "/O2", "/MD", f"/I{REPO}", *sources, f"/Fe{exe}", f"/Fo{out_dir}\\",
               "/link", f"/LIBPATH:{os.path.join(DXSDK, 'lib', 'x86')}", "d3d9.lib", "d3dx9.lib", "user32.lib"]
    result = subprocess.run(command, capture_output=True, text=True, errors="replace", env=env, cwd=out_dir, creationflags=BELOW_NORMAL)
    if result.returncode != 0 or not os.path.isfile(exe):
        print(result.stdout, result.stderr)
        raise SystemExit("cannot build tex2d_test")
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dump", help="directory of main.exe /galdumptex output")
    parser.add_argument("--files", help="directory of image files to test")
    parser.add_argument("--ui-index", help="index.json naming archive files by content hash (scratch extract)")
    parser.add_argument("--out", required=True, help="work directory (tool build, list, result)")
    parser.add_argument("--no-hal", action="store_true", help="reference on NULLREF (no GPU)")
    args = parser.parse_args()
    if not args.dump and not args.files:
        parser.error("--dump or --files")
    out = os.path.abspath(args.out)
    if os.path.commonpath([out, REPO]) == REPO and not out.startswith(os.path.join(REPO, "buildstage")):
        raise SystemExit("--out must be outside the repository or under buildstage (game data)")
    exe = build(os.path.join(out, "build"))

    names = {}
    if args.ui_index:
        for path, entry in json.load(open(args.ui_index)).items():
            names.setdefault(entry["fnv"], path)
    lines = []
    if args.dump:
        seen = set()
        for name in sorted(os.listdir(args.dump)):
            if not (name.startswith("tex2d_") and name.endswith(".bin")):
                continue
            path = os.path.join(args.dump, name)
            content = fnv1a(open(path, "rb").read())
            key = "%016x" % content
            label = names.get(key, name)
            # Each distinct file once (the dump is in call order and may repeat a file).
            if key in seen:
                continue
            seen.add(key)
            lines.append(f"{path}|{label}")
    if args.files:
        for root, _, files in os.walk(args.files):
            for name in sorted(files):
                if name.lower().rsplit(".", 1)[-1] in ("dds", "png", "tga", "bmp", "jpg"):
                    path = os.path.join(root, name)
                    lines.append(f"{path}|{os.path.relpath(path, args.files).replace(os.sep, '/')}")
    list_path = os.path.join(out, "inputs.txt")
    with open(list_path, "w") as handle:
        handle.write("\n".join(lines) + "\n")
    result_path = os.path.join(out, "result.json")
    run = [exe, list_path, result_path] + (["--no-hal"] if args.no_hal else [])
    process = subprocess.run(run, capture_output=True, text=True, errors="replace", creationflags=BELOW_NORMAL)
    print(process.stdout.strip())
    if process.stderr.strip():
        print(process.stderr.strip())
    result = json.load(open(result_path))
    bad = [entry for entry in result["files"] if entry["oracle"] != "equal" or entry["portable"] != "equal" or entry.get("engine", "equal") != "equal"]
    formats = {}
    for entry in result["files"]:
        formats[entry["portableSource"]] = formats.get(entry["portableSource"], 0) + 1
    print("source formats:", ", ".join(f"{name} {count}" for name, count in sorted(formats.items())))
    for entry in bad[:40]:
        print(f"  {entry['file']}: oracle {entry['oracle']}; portable {entry['portable']}; engine {entry.get('engine', '-')}")
    if len(bad) > 40:
        print(f"  ... {len(bad) - 40} more")
    print("GATE:", "PASS" if process.returncode == 0 else "FAIL", f"({result['total']} files, result {result_path})")
    return process.returncode


if __name__ == "__main__":
    sys.exit(main())
