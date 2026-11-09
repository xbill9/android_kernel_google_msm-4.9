#!/bin/bash
# Build clone3test for arm64 and 32-bit arm with the LineageOS tree's clang and NDK sysroot
# (after a full build). Usage: build-clone3test.sh [tree]
# Output: clone3test and clone3test32 next to this script.
set -eu
T=${1:-$HOME/android/lineage-24.0}
B=$(dirname "$(readlink -f "$0")")
CC=$(ls -d $T/prebuilts/clang/host/linux-x86/clang-r*/bin/clang | sort -V | tail -1)
S=$T/out/soong/ndk/sysroot
I=$T/out/soong/.intermediates/bionic/libc
build() {  # target libdir crtvariant output
  $CC --target=$1 --sysroot=$S -O2 -s -fPIE -pie -nostartfiles -fuse-ld=lld -Wall \
    $I/crtbegin_dynamic/$3/crtbegin_dynamic.o "$B/clone3test.c" \
    -L$S/usr/lib/$2/30 -lc -ldl $I/crtend_android/$3/crtend_android.o -o "$4"
  echo "built $4"
}
build aarch64-linux-android30 aarch64-linux-android android_arm64_armv8-a "$B/clone3test"
build armv7a-linux-androideabi30 arm-linux-androideabi android_arm_armv8-a "$B/clone3test32"
