#!/bin/sh
# Regenerates android/native/ffmpeg/<abi>/: what FFmpeg's configure writes for the Android build
# (config.h, config_components.h, libavutil/avconfig.h, the component lists) plus
# libavutil/ffversion.h, sources.cmake (the object list, from FFmpeg's Makefiles) and a stamp of
# what they were made from, which CMakeLists.txt checks.
#
#   sh android/native/ffmpeg/generate.sh                     both ABIs
#   sh android/native/ffmpeg/generate.sh arm64-v8a           the one named
#
# Runs in Git Bash on Windows, on the model of dependencies/ffmpeg/faf/generate.sh: no make, since
# configure only writes files and faf/gen_sources.py reads the Makefiles. Needs the pinned NDK
# (cmake/toolchains/android-pins.cmake; FAF_ANDROID_NDK, or under ANDROID_HOME, default
# C:/Android/sdk) and Python 3 (PYTHON, default python3 or python). Takes minutes per ABI: configure
# spawns clang hundreds of times.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
SRC="$REPO/dependencies/ffmpeg"
FAF="$SRC/faf"

[ -f "$SRC/configure" ] || { echo "generate.sh: $SRC is empty: git submodule update --init dependencies/ffmpeg" >&2; exit 1; }

# The build's pins and the engine's FFmpeg configuration, read from where they are kept, so that
# neither can drift from what is generated here.
pins="$REPO/cmake/toolchains/android-pins.cmake"
API=$(sed -n 's/^set(FAF_ANDROID_API_LEVEL \([0-9][0-9]*\)).*/\1/p' "$pins")
NDK_REVISION=$(sed -n 's/^set(FAF_ANDROID_PINNED_NDK_REVISION "\([^"]*\)").*/\1/p' "$pins")
[ -n "$API" ] && [ -n "$NDK_REVISION" ] || { echo "generate.sh: no API level or NDK revision in $pins" >&2; exit 1; }

FAF_CONFIGURE_OPTIONS=$(tr -d '\r' < "$FAF/generate.sh" | sed -n '/^FAF_CONFIGURE_OPTIONS="/,/"$/p' \
  | sed -e 's/^FAF_CONFIGURE_OPTIONS="//' -e 's/"$//' | tr '\n' ' ')
FAF_LIBRARIES=$(tr -d '\r' < "$FAF/generate.sh" | sed -n 's/^FAF_LIBRARIES="\(.*\)"$/\1/p')
case "$FAF_CONFIGURE_OPTIONS" in
  *--enable-decoder=*) ;;
  *) echo "generate.sh: no FAF_CONFIGURE_OPTIONS found in $FAF/generate.sh" >&2; exit 1 ;;
esac
[ -n "$FAF_LIBRARIES" ] || { echo "generate.sh: no FAF_LIBRARIES found in $FAF/generate.sh" >&2; exit 1; }

NDK=${FAF_ANDROID_NDK:-${ANDROID_HOME:-C:/Android/sdk}/ndk/$NDK_REVISION}
PATH="$(cygpath -u "$NDK")/toolchains/llvm/prebuilt/windows-x86_64/bin:$PATH"
export PATH
for tool in clang llvm-ar llvm-nm llvm-ranlib; do
  command -v $tool >/dev/null || { echo "generate.sh: $tool not found under NDK $NDK" >&2; exit 1; }
done
if [ -z "${PYTHON:-}" ]; then
  PYTHON=$(command -v python3 || command -v python || true)
fi
[ -n "$PYTHON" ] || { echo "generate.sh: no Python 3 (set PYTHON)" >&2; exit 1; }

# configure evaluates its temporary paths unquoted, and a Windows TEMP loses its backslashes there.
TMPDIR=$(cygpath -u "${TMPDIR:-${TEMP:-/tmp}}")
export TMPDIR

generate() {
  abi=$1
  case $abi in
    arm64-v8a) ffarch=aarch64; triple=aarch64-linux-android; abi_options= ;;
    # Only the emulator's. Its inline assembly addresses FFmpeg's global tables directly, which a
    # shared library cannot link without -Bsymbolic (the game is one, and that would be for all of it).
    x86_64)    ffarch=x86_64;  triple=x86_64-linux-android;  abi_options=--disable-inline-asm ;;
    *) echo "generate.sh: unknown ABI $abi (arm64-v8a or x86_64)" >&2; exit 1 ;;
  esac
  out="$HERE/$abi"
  build=$(mktemp -d "$TMPDIR/ffmpeg-android-$abi.XXXXXX")

  echo "generate.sh: configuring $abi for API $API in $build"
  # --disable-autodetect: the NDK has zlib and the like, and nothing may be picked up. No debug
  # information, as in faf/: that is the CMake build's choice. configure hands --extra-cflags to the
  # assembler checks as well, so the target reaches them. --host-cc: the host tools are not built
  # with this configuration, but configure tests a host compiler anyway, and the NDK's clang finds C
  # headers only with a target.
  # shellcheck disable=SC2086
  (cd "$build" && "$SRC/configure" \
      --enable-cross-compile --target-os=android --arch=$ffarch \
      --cc=clang --cxx=clang++ --ld=clang --ar=llvm-ar --nm=llvm-nm --ranlib=llvm-ranlib \
      --host-cc=clang --host-cflags=--target=$triple$API \
      --extra-cflags=--target=$triple$API --extra-ldflags=--target=$triple$API \
      --enable-pic --disable-debug --disable-autodetect \
      $abi_options $FAF_CONFIGURE_OPTIONS) > "$build/configure.out" 2>&1 || {
    tail -40 "$build/ffbuild/config.log" >&2; echo "generate.sh: configure failed, see $build" >&2; exit 1; }

  rm -rf "$out"
  mkdir -p "$out/libavutil"
  cp "$build/config.h" "$build/config_components.h" "$out/"
  cp "$build/libavutil/avconfig.h" "$out/libavutil/"
  # What FFmpeg's Makefile generates before compiling (libavutil/ffversion.h).
  (cd "$build" && sh "$SRC/ffbuild/version.sh" "$SRC" libavutil/ffversion.h)
  cp "$build/libavutil/ffversion.h" "$out/libavutil/"
  for lib in $FAF_LIBRARIES; do
    for list in "$build/lib$lib"/*_list.c; do
      [ -f "$list" ] || continue
      mkdir -p "$out/lib$lib"
      cp "$list" "$out/lib$lib/"
    done
  done
  # shellcheck disable=SC2086
  "$PYTHON" "$FAF/gen_sources.py" "$SRC" "$build/ffbuild/config.mak" "$out/sources.cmake" $FAF_LIBRARIES

  # Its header sends the reader to faf/generate.sh; here that is this script.
  sed -i 's#regenerate with faf/generate.sh#regenerate with android/native/ffmpeg/generate.sh#' "$out/sources.cmake"

  # gen_sources.py has only ever served .c lists, and FFmpeg assembles with flags of their own
  # (ffbuild/common.mak: $(ASFLAGS), not $(CFLAGS)). The target is CMake's, as for C.
  asflags=$(tr -d '\r' < "$build/ffbuild/config.mak" | sed -n 's/^ASFLAGS=//p')
  {
    echo "set(FFMPEG_FAF_ASM_OPTIONS"
    for flag in $asflags; do
      case $flag in --target=*) ;; *) echo "  $flag" ;; esac
    done
    echo ")"
  } >> "$out/sources.cmake"

  {
    echo "commit $(git -C "$SRC" rev-parse HEAD)"
    echo "api $API"
    echo "ndk $NDK_REVISION"
  } > "$out/stamp.txt"

  # The checks that must not rest on anyone reading the output.
  grep -q 'FFMPEG_LICENSE "LGPL' "$out/config.h" || { echo "generate.sh: $abi is not LGPL" >&2; exit 1; }
  if grep -Eq '^#define CONFIG_(GPL|NONFREE|VERSION3) 1' "$out/config.h"; then
    echo "generate.sh: $abi enables GPL, version 3 or nonfree code" >&2; exit 1
  fi
  if grep -rIlE "$REPO|$(cygpath -m "$REPO")|$(cygpath -m "$NDK")|$TMPDIR" "$out" >&2; then
    echo "generate.sh: the files above name a path of this machine" >&2; exit 1
  fi

  rm -rf "$build"
  echo "generate.sh: wrote $out"
}

if [ $# -eq 0 ]; then
  set -- arm64-v8a x86_64
fi
for abi in "$@"; do
  generate "$abi"
done
