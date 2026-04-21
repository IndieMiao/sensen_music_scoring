#!/usr/bin/env bash
# Build SingScoring.xcframework for iOS device + simulator.
#
# Requirements: macOS, Xcode (15+), CMake (3.22+). Run from the repo root.
# Output: build-ios/SingScoring.xcframework
#
# Why three builds? Apple Silicon Macs run arm64 in both device and simulator
# contexts, so we need a separate simulator slice (arm64 + x86_64 via lipo)
# distinct from the device slice (arm64 only).

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$HERE/build-ios"
OUT="$BUILD_DIR/SingScoring.xcframework"
CONFIG="${CONFIG:-Release}"
MIN_IOS="${MIN_IOS:-13.0}"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

configure_and_build() {
    local label="$1"
    local sysroot="$2"
    local archs="$3"
    local dir="$BUILD_DIR/$label"

    cmake -S "$HERE" -B "$dir" -G Xcode \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_SYSROOT="$sysroot" \
        -DCMAKE_OSX_ARCHITECTURES="$archs" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_IOS" \
        -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
        -DSINGSCORING_BUILD_TESTS=OFF

    cmake --build "$dir" --config "$CONFIG" --target SingScoring
}

configure_and_build device    iphoneos         "arm64"
configure_and_build simulator iphonesimulator  "arm64;x86_64"

DEVICE_FW="$BUILD_DIR/device/bindings/ios/$CONFIG-iphoneos/SingScoring.framework"
SIM_FW="$BUILD_DIR/simulator/bindings/ios/$CONFIG-iphonesimulator/SingScoring.framework"

for fw in "$DEVICE_FW" "$SIM_FW"; do
    [ -d "$fw" ] || { echo "Missing framework: $fw" >&2; exit 1; }
done

xcodebuild -create-xcframework \
    -framework "$DEVICE_FW" \
    -framework "$SIM_FW" \
    -output "$OUT"

echo
echo "Built $OUT"
