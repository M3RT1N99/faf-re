# port/native — portable core of the Android port

Static libraries that take the game's data from "files on a phone" to "bytes
the engine can use", with the same rules the desktop engine applies. Nothing
here is recovered from the binary; the engine sources in `src/sdk` are the
behaviour reference and are cited where a rule comes from.

| Target | What it is |
|---|---|
| `faf_port_core` (`faf::port_core`) | `FileSystem`, `Vfs`, `DataPath`, `Image`, `CommandLine` — the API in `include/faf/port/` |
| `faf_port_lua` (`faf::port_lua`) | LuaPlus Build 1081 C core (Lua 5.0.1, `float` numbers), built from a patched copy |
| `faf_datacheck` | Host tool: runs a data-path script and mounts the data exactly as the device does |
| `tests/` | CTest unit tests plus an integration test against a local FAF install |

The Android runtime (`port/android`) links `faf::port_core` through
`add_subdirectory`; tools and tests are off there.

## What each part does

- **FileSystem** (`fs::`) — Windows path semantics on POSIX storage:
  `NormalizePath` (`\` and `/`, `.`/`..` folding, drive prefixes),
  `WildcardMatch` and `FindFiles` (`_findfirst`: case-insensitive, `*.*`,
  synthetic `.`/`..`, NTFS upper-case order — that order decides the mount
  order of the `.nx2`/`.scd` archives), `ResolveCaseInsensitive` (component by
  component; one `stat` per component when names exist as written, a
  directory listing only for the components that don't), `CreateDirectories`
  (reuses existing directories whose case differs), `ReadWholeFile`.
- **Vfs** (`VirtualFileSystem`) — `CVFSImpl` re-expressed: mounts in order,
  first mount wins, lower-cased mount points, zip archives (stored and
  deflate, zip64 too) or directories. The zip reader bounds-checks every
  field against the file and the central directory, reads with positional
  64-bit reads (`pread` / `ReadFile`+`OVERLAPPED`), so `Read` is thread-safe
  without locks, and rejects entries whose declared size deflate cannot
  produce before allocating. Engine behaviour kept on purpose: data-descriptor
  entries are dropped, duplicate names resolve to the first entry, CRCs are not
  checked. Directory mounts never resolve a VFS path containing `..`.
- **DataPath** (`RunDataPathScript`) — `DISK_SetupDataAndSearchPaths`: a fresh
  `lua_State`, the "unsafe" bindings (`LOG`, `SHGetFolderPath`, `io.dir`) plus
  case-insensitive `dofile`/`loadfile`/`io.open`/`io.lines`/`os.remove`/`os.rename`,
  `LaunchDir`/`InitFileDir`, then `path`/`hook`/`protocols` read back in table
  order. `allowWrites = false` turns `os.remove`, `os.rename` and write-mode
  `io.open` into logged no-ops for dry runs against a real install.
  `os.execute`/`os.exit` raise errors, `os.setlocale` can only query, and
  LuaPlus's `towstring` is removed (see the patch notes). Bindings reach their
  per-run context through upvalues, so runs share no state — two runs in one
  process, or on two threads, do not interfere. `CheckLuaSyntax` parses a chunk
  without running it.
- **Image** — DDS top-mip decode to RGBA8 (DXT1/2/3/4/5, DX10 BC1–3 and RGBA8,
  uncompressed masks incl. 565/1555/4444/L8/A8), rejecting truncated data;
  `WritePng` for host tooling.
- **CommandLine** — `CFG_GetArgOption` on the launcher's `argv` tokens.

## Why stock LuaPlus and not `src/sdk/lua`

The engine's own Lua VM in `src/sdk/lua` is the recovered 32-bit fork. It is
bound to MSVC/x86 layouts — `static_assert`s on 32-bit sizes (`TObject` is 8
bytes, `msvc8::string` 28), Win32 types and CRT internals — and does not build
for arm64 (compiling `lua/LuaObject.cpp` with the NDK stopped at 294 errors
after stubbing `Windows.h` and the x86 intrinsics; `CVFSImpl.cpp` and
`CZipFile.cpp` likewise). Making the engine VM LP64-clean is part of the engine
port, not of this bring-up.

The stock LuaPlus 1081 C core is the same language family (LuaPlus is what the
game was built on), is LP64-clean, and runs FAF's scripts once
[`dependencies/patches/luaplus_build1081_faf_android.patch`](../../dependencies/patches/luaplus_build1081_faf_android.md)
adds what the engine's lexer accepts (`!=`, UTF-8 BOMs) and fixes the
char-signedness and 4-byte `wchar_t` problems. `lua_Number` is `float` as in
the game (`faf_lua_config.h`), and `^` is bitwise XOR in both (the engine
compiles `^` to its `BXOR` opcode; FAF scripts use `math.pow`). Remaining
differences to the engine VM are not reconciled here; the data-path scripts and
a parse of every file in `lua.nx2` are what this core is verified against.

The patch is applied to a **copy** at configure time
(`<build>/luaplus`, `cmake/FafPortLuaPlus.cmake`); `dependencies/LuaPlus_Build1081`
is shared with the engine build and never edited. Configure fails with an
explanation if that tree carries the engine's 32-bit object layout
(`luaplus_build1081_faf_required.patch` fully applied).

## Build and test on the host (Windows)

```powershell
cmake -S port/native -B buildstage/native-host            # Visual Studio generator, or -G Ninja with clang
cmake --build buildstage/native-host --config Release
ctest --test-dir buildstage/native-host -C Release --output-on-failure
```

zlib comes from `dependencies/zlib-1.2.3` on Windows and from the system
(`find_package(ZLIB)`, the NDK sysroot on Android) elsewhere
(`FAF_PORT_USE_SYSTEM_ZLIB`). Options: `FAF_PORT_BUILD_TOOLS`,
`FAF_PORT_BUILD_TESTS` (both default ON only for a top-level non-Android
build), `FAF_PORT_WARNINGS_AS_ERRORS`, `FAF_PORT_LUAPLUS_DIR`,
`FAF_PORT_TEST_FAF_INSTALL` (default `C:/ProgramData/FAForever`),
`FAF_PORT_TEST_TMP`.

The integration test runs FAF's real `init_faf.lua` from the install with
writes disabled and a sandboxed `LOCAL_APPDATA`, then checks ≥ 700 mounts,
≥ 15 archives, the standard lookups (`tools/DataCheck.cpp`), zero syntax
errors across `lua.nx2`, and the 1024x768 DXT5 splash. It reports "skipped"
when no install is present. The zip tests include a truncation/corruption
sweep that is worth running under AddressSanitizer (clang:
`-DCMAKE_C_FLAGS=-fsanitize=address -DCMAKE_CXX_FLAGS=-fsanitize=address
-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`).

Compile check for Android (the libraries only):

```powershell
cmake -S port/native -B buildstage/native-android -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=C:/Android/sdk/ndk/29.0.14206865/build/cmake/android.toolchain.cmake `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release
cmake --build buildstage/native-android --target faf_port_core faf_port_lua
```

### faf_datacheck

```text
faf_datacheck --init <script> [--root <dir>] [--localappdata <dir>] [--documents <dir>]
              [--no-writes] [--extract <vfspath> <out.png>]... [--check-lua]
              [--verify-archives] [--list-mounts] [--json] [--verbose]
```

`--root` points at an Android-style data root (as `deploy_android.ps1 -StageDir`
builds it) and defaults the script to `<root>/faf/bin/init_faf.lua` and the
folders to `<root>/localappdata` and `<root>/documents`. Without `--root` or
`--localappdata` the folders default to a sandbox under the temp directory —
never the real profile. Always pass `--no-writes` against a real install.
Exit code 0 = all checks passed, 1 = a check failed, 2 = usage error. From Git
Bash, set `MSYS_NO_PATHCONV=1` so VFS paths like `/textures/...` are not
rewritten into Windows paths.

## What becomes engine code later, and what is scaffolding

- **FileSystem** is meant to grow into the engine's POSIX platform layer:
  everything the engine does with `_findfirst`, `GetFileAttributes` and
  NTFS-style lookups on Windows needs exactly these semantics on Android, and
  `NativeFile` is the seed of its file I/O.
- **Vfs**, **DataPath** and **Image** are bring-up scaffolding. They prove the
  data pipeline natively and feed the M1 splash; the engine port will mount
  through its own `CVFSImpl`/`CZipFile`, run the data-path script on its own Lua
  VM, and transcode textures (ETC2/ASTC) instead of decoding to RGBA8 — see
  `docs/port/android-roadmap.md`. Their tests document the behaviour that port
  has to keep.
- **CommandLine** is the shared contract with the launchers and stays.
