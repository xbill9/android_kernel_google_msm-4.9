#!/bin/bash
# Build atomictest (arm64) with the LineageOS tree's clang and NDK sysroot (after a full build).
# Usage: build-atomictest.sh [tree]   Output: atomictest next to this script.
set -eu
T=${1:-$HOME/android/lineage-24.0}
B=$(dirname "$(readlink -f "$0")")
CC=$(ls -d $T/prebuilts/clang/host/linux-x86/clang-r*/bin/clang | sort -V | tail -1)
S=$T/out/soong/ndk/sysroot
I=$T/out/soong/.intermediates/bionic/libc
$CC --target=aarch64-linux-android30 --sysroot=$S -O2 -s -fPIE -pie -nostartfiles -fuse-ld=lld -Wall \
  $I/crtbegin_dynamic/android_arm64_armv8-a/crtbegin_dynamic.o "$B/atomictest.c" \
  -L$S/usr/lib/aarch64-linux-android/30 -lc -ldl $I/crtend_android/android_arm64_armv8-a/crtend_android.o \
  -o "$B/atomictest"
echo "built $B/atomictest"
