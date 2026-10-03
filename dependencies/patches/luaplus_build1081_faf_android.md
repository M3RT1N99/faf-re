# FAF LuaPlus Build 1081 patch — Android port core

Baseline: the LuaPlus Build 1081 tree in `dependencies/LuaPlus_Build1081`, the
same one the other `luaplus_build1081_*` patches describe. The two files this
patch touches had these SHA256s when it was cut:

| File | SHA256 |
|---|---|
| `Src/LuaPlus/src/llex.c` | `44a7c98d8fed2f03c6895eca4ae6a4b589b42f2ab6a9663d38231cc7a8599c64` |
| `Src/LuaPlus/src/lib/lauxlib.c` | `14f9a57e66cfa9c08255c8d85e4086110780713ab028785e7ad99ee173c2adc1` |

Role: **applied to a copy, never in place.** `port/native/cmake/FafPortLuaPlus.cmake`
copies `Src/LuaPlus` into the build tree at configure time and applies this
patch there (`git apply`, `patch -p1` as a fallback). The tree in
`dependencies/` is shared with the engine build and stays untouched — anything
that bulk-applies `luaplus_build1081_*.patch` to it must skip this file.

`luaplus_build1081_faf_android.patch` carries two files, seven hunks. It is
byte-exact (`*.patch -text` in `.gitattributes`): both targets are CRLF and the
body lines carry the CRs.

## Why it is needed

`port/native` builds the stock LuaPlus 1081 C core for arm64 to run FAF's
`init_faf.lua` and to parse FAF's Lua on the device (see
[`port/native/README.md`](../../port/native/README.md) for why it is not the
engine's recovered VM). Unpatched, three things go wrong:

1. FAF's scripts use syntax the game's lexer accepts and stock LuaPlus does
   not: `!=` (four times in `init_faf.lua` alone) and a UTF-8 byte-order mark
   (`lua/ui/help/unitdescription.lua` starts with one).
2. Bytes are widened to `lua_WChar` from a plain `char`. On x86 that sign
   extends 0xEF to 0xFFFFFFEF, and `isspace()` on that value reads outside the
   CRT's table — a BOM, or any byte >= 0x80 outside a string, crashed the
   parser on Windows x86 and x64. ARM's `char` is unsigned, so the same file
   behaved differently per platform.
3. `LUAPLUS_HAS_WCHAR_T` makes `lua_WChar` a `wchar_t`, which is 4 bytes on
   Android and Linux. Code written for Windows' 2-byte `wchar_t` reads
   uninitialised memory (`luaL_loadfile`'s BOM test) or mis-sizes buffers
   (wide-string literals).

## Scope — two files, seven hunks

| # | File | Hunk | What and why |
|---|---|---|---|
| 1 | `src/lib/lauxlib.c` | `getF` (`@@ -479`) | Widen file bytes through `unsigned char`. Problem 2, for `luaL_loadfile`. |
| 2 | `src/lib/lauxlib.c` | `getWF` (`@@ -488`) | Read UTF-16LE files in 2-byte units and widen them in place, back to front, so the loader works whatever `sizeof(lua_WChar)` is. Before, it read `sizeof(lua_WChar)`-byte units, which on Android split every UTF-16 file into garbage. |
| 3 | `src/lib/lauxlib.c` | `luaL_loadfile` (`@@ -520`) | Test the UTF-16LE BOM byte-wise (`FF FE`). The old code read 2 bytes into a `lua_WChar`, leaving the upper half of a 4-byte `wchar_t` uninitialised, so the test was random on Android. |
| 4 | `src/lib/lauxlib.c` | `getS` (`@@ -557`) | Widen buffer bytes through `unsigned char`. Problem 2, for `luaL_loadbuffer` — the path the port uses for every script. This is the hunk that fixed the BOM crash. |
| 5 | `src/llex.c` | `case '!'` (`@@ -525`) | `!=` is `TK_NE`; a lone `!` is an "invalid character" error. Copied from the engine's lexer (`src/sdk/lua/LuaParser.cpp`, `case '!'`). |
| 6 | `src/llex.c` | `default:` (`@@ -554`) | A byte >= 0x80 where a token may start is accepted only as part of a UTF-8 BOM (`EF BB BF`, skipped); anything else is "invalid character". Same rule and message as the engine's lexer (`LuaParser.cpp`, the `(ls->current & 0x80)` branch), and it keeps such values away from `isspace`/`isalpha`. Bytes inside strings and comments are unaffected. |
| 7 | `src/llex.c` | `L"..."` (`@@ -565`) | Remove LuaPlus's wide-string literals. The engine's lexer has none — `L"x"` is the name `L` followed by a string, a call — and LuaPlus's `wsave()`/`luaS_newlwstr` assume a 2-byte `lua_WChar` (they write 4-byte values at 2-byte steps and hash/compare `2 * len` bytes). `read_wstring` stays in the file, unused. |

None of the hunks change the VM, the object layouts or the bytecode; a chunk
that parsed before parses to the same code (except `L"..."`, which now means
what it means in the game).

Not patched, handled by the port instead: `towstring()` (the remaining way to
create a LuaPlus wide string, and `towstring(table)` passes an `int` to `%s`)
is removed from the data-path state in `port/native/src/DataPath.cpp`; the
Windows-only wide-char declarations come from the port's prefix header
(`port/native/src/lua/faf_port_lua_config.h`: `<wchar.h>`, `<wctype.h>`,
`_snwprintf` -> `swprintf`).

## Relation to the other LuaPlus patches

Independent of both. `luaplus_build1081_faf_crt.patch` touches neither file.
`luaplus_build1081_faf_required.patch` touches `llex.c` only at lines 35-46
(the `luaM_setname` block); this patch applies with a 3-line offset on top of
it and without it, checked both ways. The port does **not** build the full
`faf_required` layout (pack(4) `TObject`, `__int64` fields): it targets the
32-bit engine, does not compile with clang and is wrong on LP64, so the port's
configure step refuses a tree that carries it.

## Verified

- Applies with `git apply --check` to a fresh copy of the local tree and to a
  copy with the `faf_required` `llex.c` hunk applied; reverse-checks cleanly.
- `port/native` host tests (MSVC and clang + AddressSanitizer): `!=`, BOM-only
  chunks, high bytes in and outside strings, `L"x"`, UTF-16LE and UTF-8-BOM
  files through `luaL_loadfile`.
- All 1477 `.lua` files in FAF 3839's `lua.nx2` parse (0 failures) and FAF's
  `init_faf.lua` runs (786 path entries).
- Compiles for `aarch64-linux-android26` with NDK 29 (no warnings on the
  patched lines; the vendored code's own warnings are suppressed with `-w`).

## Checking it

```powershell
# from a scratch copy of dependencies/LuaPlus_Build1081 (never the original):
git apply --check -v ..\patches\luaplus_build1081_faf_android.patch
```

From inside this repository's work tree, set `GIT_CEILING_DIRECTORIES` to the
copy's parent first; otherwise `git apply` takes the paths relative to the
repository root (the CMake module does this).
