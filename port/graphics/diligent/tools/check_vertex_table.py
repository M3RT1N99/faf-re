#!/usr/bin/env python3
"""Checks that port/graphics/diligent/VertexFormatTableD3D9.inl is still a verbatim copy of the D3D9
backend's vertex declaration table (src/sdk/gpg/gal/backends/d3d9/D3D9Interfaces.cpp, from
`kVertexFormatCount = 24U` to the end of `kVertexFormatsByCode`).

The Diligent backend computes VertexFormat::streamStrides_ from its copy the way
VertexFormatD3D9::SetFormatDeclaration does; engine code reads those strides
(moho/render/d3d/CD3DVertexFormat.cpp:55-77), so the two tables must not drift.

    python port/graphics/diligent/tools/check_vertex_table.py      # exit 0 when identical
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", ".."))
D3D9 = os.path.join(ROOT, "src", "sdk", "gpg", "gal", "backends", "d3d9", "D3D9Interfaces.cpp")
COPY = os.path.join(ROOT, "port", "graphics", "diligent", "VertexFormatTableD3D9.inl")


def table(text):
    start = text.index("static constexpr std::uint32_t kVertexFormatCount = 24U;")
    end = text.index("};", text.index("kVertexFormatsByCode[kVertexFormatCount]")) + 2
    # compare code, not indentation or line endings
    return [re.sub(r"\s+", " ", line).strip() for line in text[start:end].splitlines() if line.strip()]


def main():
    reference = table(open(D3D9, encoding="utf-8-sig").read())
    copy = table(open(COPY, encoding="utf-8").read())
    if reference == copy:
        print(f"identical: {len(reference)} lines")
        return 0
    for index, (a, b) in enumerate(zip(reference, copy)):
        if a != b:
            print(f"first difference at table line {index + 1}:\n  D3D9: {a}\n  copy: {b}")
            break
    else:
        print(f"length differs: D3D9 {len(reference)} lines, copy {len(copy)}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
