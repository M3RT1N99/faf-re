# port/third_party/zstd — the zstd decoder for the headless runner

**Zstandard 1.5.7** (Meta Platforms, Inc.), the decompression subset only, unchanged. The Android
headless runner executable (`faf_headless_runner`, in the APK `libfafrunner.so`) uses it to decode
the zstd body of a `.fafreplay` (FAF's vault, the browser download `https://replay.faforever.com/<id>`
and the Python and Rust clients write zstd); see `port/engine/runner/ReplayFile.cpp`. Nothing else
links it: not `libfafengine.so`, not the app's `libfaf_android.so`, not main.exe.

## Licence

zstd is dual-licensed, BSD (`LICENSE`) or GPLv2 (`COPYING` upstream). This project uses it under the
**BSD licence**; `LICENSE` is the upstream file unchanged. Its notice must ship with every binary
that contains the decoder: the APK carries it as `assets/licenses/zstd.txt`
(`scripts/port/build_android.ps1`), and the release notes name it.

## Source

Copied without changes from the zstd source tree vendored in the Rust crate
`zstd-sys 2.0.16+zstd.1.5.7`, from the local cargo registry of this PC
(`%USERPROFILE%\.cargo\registry\src\index.crates.io-1949cf8c6b5b557f\zstd-sys-2.0.16+zstd.1.5.7\zstd\`);
nothing was downloaded. `lib/zstd.h` says `ZSTD_VERSION_MAJOR 1`, `MINOR 5`, `RELEASE 7`.

| Taken | Why |
|---|---|
| `lib/zstd.h`, `lib/zstd_errors.h` | the public decoder API |
| `lib/common/*` | shared code (entropy tables, xxhash, error codes); `pool.c` and `threading.c` are only for the multithreaded compressor and are not compiled |
| `lib/decompress/*` | the decoder; `huf_decompress_amd64.S` is not compiled (`ZSTD_DISABLE_ASM`) |
| `LICENSE` | the BSD licence |

Left out: `lib/compress`, `lib/dictBuilder`, `lib/legacy` (formats before v0.8), `lib/deprecated`,
the programs, tests and contrib.

## Build

`scripts/port/build_runner.py` compiles `common/{debug,entropy_common,error_private,fse_decompress,
xxhash,zstd_common}.c` and `decompress/{huf_decompress,zstd_ddict,zstd_decompress,
zstd_decompress_block}.c` as C11 with `-O2 -fPIC -DZSTD_DISABLE_ASM=1 -DZSTD_LEGACY_SUPPORT=0
-DZSTD_TRACE=0` and links them into the runner executable only.

## sha256 of the files

As copied and as committed; `.gitattributes` here turns off end-of-line conversion so a checkout
keeps them byte for byte.

```
7055266497633c9025b777c78eb7235af13922117480ed5c674677adc381c9d8  LICENSE
6a718e8edaca112abdc0bdfa7de67edefaa44c8d9e514b79672ab58ab3a9118a  lib/common/allocations.h
5bb7693a35d7ce03f8715c0b1e237062285e052250fdd34f72f38e7e92365090  lib/common/bits.h
a9461685c9add6f609078acc950a08db4c02e6e6a849d550f45182ba2ae38556  lib/common/bitstream.h
d19829705d8039437ffba873df2b58d20d7b876d58da479121d2e515df55cbf7  lib/common/compiler.h
23d520b536a6f23114e66549d03a967b1f24b15560c5b1ebd47ee392ccee4a1c  lib/common/cpu.h
7ab1ea104acf3243fc1cd4e6d2514046f1dfc1101c7c676145c8da080e8c24ca  lib/common/debug.c
8260cd5087b8309679c61b772b535688993c61a675a30481b8ae55b45740ba15  lib/common/debug.h
7dfce29c6bc807645b1f0c373dad94bbb49603b175a84e72d1739bf9df65feb8  lib/common/entropy_common.c
3f5873b2626ca1cdf554b7ec7b04ea0ef296fd0f6882d7fad42448b7e2dbb41c  lib/common/error_private.c
ae077bb433eb150ee247410a0357f910b9c07be839c6a9abe0b2e6b18ceb716d  lib/common/error_private.h
5235ce1e512bf80204d013b1f4cecd776fdaa9effddb48308bce08f6d0b84499  lib/common/fse.h
84eba7030a036b36363a5108e760428ce549f1a833bec6464dc755f6ed51a6fb  lib/common/fse_decompress.c
9a83d899c8c9bf03389d482562090ec2443bc0f87f27864a2b891b180333f2d4  lib/common/huf.h
4dd5fe76fa30a020cd4b3af3134ae143920300a1fe54baac1a5da73297e66783  lib/common/mem.h
9431e26cf7ca46ffb878f17df31198046664d592ae208dfbe3715e82b87af79d  lib/common/pool.c
bca19a2408e85f31bad3a13f205de92ac26ecda0c5af96fdd7f213ce03d9fc78  lib/common/pool.h
75e2b43968b70decc6bd17876833be04894035594ddc1e745cd6b70be8265637  lib/common/portability_macros.h
50448323b46a8e1bde042ee88ec44fcfc646f3d4aeba8a06be54d26a710ed78d  lib/common/threading.c
9ca63027ea64046acacccbe056a414d37ea7ea428ce299fe08abe53c204c5c65  lib/common/threading.h
8444d064922f434b67b708a987981aa21dfeece735b48c82d31f7027d0ccef03  lib/common/xxhash.c
8cb837b21a8fe9a6b9dbcd0961ab16e733bfcbfa9e003f3a496ce07ae80aa8ee  lib/common/xxhash.h
f49eb8023a90d3de925373bd9ee0d36ed46b26c529840b64220397446cd73795  lib/common/zstd_common.c
9c77ea7d0afa8d5c868a7839e178a446a82d88eb75fd56f2fd7d9d96c5ab7c11  lib/common/zstd_deps.h
ad3d95ce2b81a8c5c6b00cfe6323d8e80cacb690d3b5883725cd0d939f4f576f  lib/common/zstd_internal.h
17f6daa4c7e97055cd1ed7bcf4d9241a4e79b465f083c5d732fda1df2f0be11e  lib/common/zstd_trace.h
710a88d877d5dc5c83a4f839392996f337ec81cf38006c4a5be6042de5b54828  lib/decompress/huf_decompress.c
7fe53316261517a8f877b793f3bfba8305a3f9ff05c5947f1d709aaff346f1dc  lib/decompress/huf_decompress_amd64.S
38bf812283d61f4cd1f6aea30f84b43317418257c4e6fc198b09b436bd484397  lib/decompress/zstd_ddict.c
a97a250fd2f956e3ae419ddde68ae1f9fe4025bc0373254058d31bbd352fa71c  lib/decompress/zstd_ddict.h
029580818b7e9cd38d9d07c63516b00ceaa943ffcfbd099fc5ccbe3628fa362f  lib/decompress/zstd_decompress.c
9cb8bcb07aeb87e4717a9c8a86d20832dc525dde2c1b72efdaaab7c937f43b87  lib/decompress/zstd_decompress_block.c
62ee0c6ae3f7353020538397f436b59743da8acb922068841b346f480f7078ac  lib/decompress/zstd_decompress_block.h
73217b49a644c0fcf567b70677cae31449ba9f50a235ef3b681001c3f36dc56d  lib/decompress/zstd_decompress_internal.h
9b4bc8245565c98ccfc61c07749928b57e7c0f6fddb0530c4f6aa1971893d88b  lib/zstd.h
66a8c3f71d12ea6e797e4f622f31f3f8f81c41b36f48cad4f5de7d8bfb6aac0a  lib/zstd_errors.h
```
