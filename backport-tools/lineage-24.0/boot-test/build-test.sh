#!/bin/bash
# Build the adb-shell test programs with the LineageOS tree's clang and NDK sysroot (after a full build).
# Usage: build-test.sh [name ...]   (default: all; names are the .c files here, e.g. ringbuftest)
#   LINEAGE_TREE=<tree> overrides ~/android/lineage-24.0.
# Output: <name> next to this script, arm64; clone3test also as 32-bit arm (clone3test32).
# kmsglog.c is a freestanding /init and is built by add-logger.sh instead.
set -eu
T=${LINEAGE_TREE:-$HOME/android/lineage-24.0}
B=$(dirname "$(readlink -f "$0")")
CC=$(ls -d $T/prebuilts/clang/host/linux-x86/clang-r*/bin/clang | sort -V | tail -1)
S=$T/out/soong/ndk/sysroot
I=$T/out/soong/.intermediates/bionic/libc
ALL="atomictest clone3test rawtptest ringbuftest sensorread udp0test"

build() {  # name output target libdir crtvariant [extra libs]
  local n=$1 o=$2 t=$3 l=$4 v=$5; shift 5
  $CC --target=$t --sysroot=$S -O2 -s -fPIE -pie -nostartfiles -fuse-ld=lld -Wall \
    $I/crtbegin_dynamic/$v/crtbegin_dynamic.o "$B/$n.c" \
    -L$S/usr/lib/$l/30 "$@" -lc -ldl $I/crtend_android/$v/crtend_android.o -o "$o"
  echo "built $o"
}

for n in ${*:-$ALL}; do
  [ -f "$B/$n.c" ] || { echo "no $B/$n.c"; exit 1; }
  extra=(); [ $n = sensorread ] && extra=(-landroid)
  build $n "$B/$n" aarch64-linux-android30 aarch64-linux-android android_arm64_armv8-a "${extra[@]}"
  [ $n = clone3test ] && build $n "$B/${n}32" armv7a-linux-androideabi30 arm-linux-androideabi android_arm_armv8-a
done
