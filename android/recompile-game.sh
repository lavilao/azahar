#!/bin/bash
# Recompiles one game's ARM code into native armv7 code the Redmi 9A's
# zakuro app runs at full speed. Run this on a PC, not the phone.
#
#   ./recompile-game.sh <rom.3ds|cxi|cci> <ndk-dir> [output-dir]
#
# Needs: Rust with recomp3ds installed
#   cargo install --git https://github.com/fearkov/3dsrecomp --locked recomp3ds
# and an Android NDK (r27 or similar) unpacked somewhere.
set -e
set -o pipefail

ROM=$1
NDK=$2
OUT=${3:-$(basename "${ROM%.*}").recomp}

if [ -z "$ROM" ] || [ -z "$NDK" ]; then
    echo "usage: $0 <rom.3ds|cxi|cci> <ndk-dir> [output-dir]" >&2
    exit 2
fi
CLANG=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi29-clang
if [ ! -x "$CLANG" ]; then
    echo "no armv7a clang in $NDK (need the NDK, not the SDK)" >&2
    exit 2
fi
command -v 3dsrecomp >/dev/null || {
    echo "3dsrecomp is not installed:" >&2
    echo "  cargo install --git https://github.com/fearkov/3dsrecomp --locked recomp3ds" >&2
    exit 2
}

# 3dsrecomp takes the compiler from CC. the NDK's clang wrapper targets
# armeabi-v7a / Android 10; NEON is on, -ffp-contract=off the tool adds
# itself so float rounds exactly like the interpreter.
export CC="$CLANG -mfpu=neon -mtune=cortex-a53"

echo "recompiling $ROM into $OUT (takes about ten minutes)..."
3dsrecomp build "$ROM" "$OUT"

SO=$(ls "$OUT"/*.so 2>/dev/null | head -1)
if [ -z "$SO" ]; then
    echo "no library came out; look at the messages above" >&2
    exit 1
fi
file "$SO" | head -1

cat <<EOF

done: $SO

copy it to the phone, in this folder (make it if it is not there):

  Android/data/io.github.lavilao.zakuro/files/3dsrecomp/

for example with adb:

  adb shell mkdir -p /storage/emulated/0/Android/data/io.github.lavilao.zakuro/files/3dsrecomp
  adb push "$SO" /storage/emulated/0/Android/data/io.github.lavilao.zakuro/files/3dsrecomp/
EOF
