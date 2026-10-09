#!/bin/bash
# Builds the zakuro APK for the Redmi 9A (armeabi-v7a, Android 10).
# Needs: Rust with the thumbv7neon-linux-androideabi target, the Android NDK
# (r27c or so) and the SDK's build-tools 34 + platform 34, and a JDK for
# keytool/apksigner.
#
#   ANDROID_NDK=/path/to/ndk ANDROID_SDK=/path/to/sdk ./build_apk.sh
set -e
set -o pipefail

cd "$(dirname "$0")/.."   # the zakuro checkout
ROOT=$(pwd)

NDK=${ANDROID_NDK:-/home/z/my-project/toolchain/android-ndk-r27c}
SDK=${ANDROID_SDK:-/home/z/my-project/toolchain/android-sdk}
TARGET=thumbv7neon-linux-androideabi
BT=$SDK/build-tools/34.0.0
JAR=$SDK/platforms/android-34/android.jar

OUT=$ROOT/android/app/build
rm -rf "$OUT"
mkdir -p "$OUT/apk/lib/armeabi-v7a"

# 1. the emulator as a shared library, compiled with the NDK's clang
export PATH="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
cargo build --release --lib --target $TARGET
cp "$ROOT/target/$TARGET/release/libzakuro.so" "$OUT/apk/lib/armeabi-v7a/libzakuro.so"

# 2. the manifest, the icon, and the library inside the APK
RES=$ROOT/android/app/src/main/res
if [ -d "$RES" ]; then
    $BT/aapt2 compile --dir "$RES" -o "$OUT/res.zip"
    LINK_RES="$OUT/res.zip"
else
    LINK_RES=""
fi
$BT/aapt2 link \
    --manifest "$ROOT/android/app/src/main/AndroidManifest.xml" \
    -I "$JAR" \
    --min-sdk-version 29 --target-sdk-version 29 \
    --version-code 1 --version-name 0.2.23 \
    -o "$OUT/unsigned.apk" $LINK_RES
cd "$OUT/apk"
zip -q "$OUT/unsigned.apk" lib/armeabi-v7a/libzakuro.so

# 3. align and sign. a debug key is made once and kept, good enough for
# sideloading
KEYSTORE=$ROOT/android/keystore.jks
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -storepass zakuro -keypass zakuro \
        -alias zakuro -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=zakuro" 2>/dev/null
fi
$BT/zipalign -f 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
$BT/apksigner sign --ks "$KEYSTORE" --ks-pass pass:zakuro --key-pass pass:zakuro \
    --out "$ROOT/android/zakuro-redmi9a.apk" "$OUT/aligned.apk"
$BT/apksigner verify --print-certs "$ROOT/android/zakuro-redmi9a.apk" | head -3

ls -la "$ROOT/android/zakuro-redmi9a.apk"
echo "APK built: $ROOT/android/zakuro-redmi9a.apk"
