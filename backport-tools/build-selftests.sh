#!/bin/bash
# Build the linux-5.4.y test_verifier as a static arm64 binary and run it on the phone over adb.
# Usage: build-selftests.sh [DIR]   (needs adb root; see README "Selftests")
#
# One-time host setup (Debian):
#   sudo dpkg --add-architecture arm64 && sudo apt-get update
#   sudo apt-get install gcc-aarch64-linux-gnu libc6-dev-arm64-cross \
#     libelf-dev:arm64 libcap-dev:arm64 zlib1g-dev:arm64 libzstd-dev:arm64
set -e
U=${1:-$PWD/up54y}
KOUT=~/sargo-bpf-test/kout
if [ ! -d "$U" ]; then
	git clone -q --depth 1 --branch linux-5.4.y --filter=blob:none --sparse \
		https://github.com/gregkh/linux.git "$U"
	git -C "$U" sparse-checkout set tools/testing/selftests/bpf tools/lib/bpf \
		tools/include tools/arch/arm64 tools/build tools/scripts scripts \
		include/uapi/linux include/uapi/asm-generic arch/arm64/include/uapi
fi
mkdir -p "$U/include/generated" "$U/out"
cp "$KOUT/include/generated/autoconf.h" "$U/include/generated/"
make -C "$U/tools/testing/selftests/bpf" ARCH=arm64 \
	CROSS_COMPILE=aarch64-linux-gnu- CC=aarch64-linux-gnu-gcc CXX=true \
	LDFLAGS=-static LDLIBS='-lcap -lelf -lz -lzstd -lrt -lpthread' \
	OUTPUT="$U/out" "$U/out/test_verifier"
adb root >/dev/null && adb wait-for-device
adb push "$U/out/test_verifier" /data/local/tmp/test_verifier54y >/dev/null
adb shell /data/local/tmp/test_verifier54y 2>&1 | tail -1
