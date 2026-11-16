#!/bin/bash
# Build this kernel tree's Image.lz4 outside the platform build, with the LineageOS 24.0 tree's
# prebuilt toolchain and the ROM's own kernel config (LTO, CFI). Used by `sargo test`.
# Env: KOUT (output dir, default /android/sargo-bpf-test/kout), LINEAGE_TREE, JOBS.
# The first build copies the config from the ROM build's KERNEL_OBJ; later builds are incremental.
# Exit status is make's; the image is $KOUT/arch/arm64/boot/Image.lz4.
set -u
SRC=$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)
B=${LINEAGE_TREE:-$HOME/android/lineage-24.0}
KOUT=${KOUT:-/android/sargo-bpf-test/kout}
mkdir -p "$KOUT"
if [ ! -f "$KOUT/.config" ]; then
  cp "$B/out/target/product/sargo/obj/KERNEL_OBJ/.config" "$KOUT/.config" \
    || { echo "no ROM kernel config; run sargo update once first"; exit 1; }
fi
CL=$B/prebuilts/clang/kernel/linux-x86/clang-r416183b
KT=$B/prebuilts/kernel-build-tools/linux-x86
BT=$B/prebuilts/build-tools
SYS=--sysroot=$B/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/sysroot
# Failing gcc/cc stubs first on PATH, so nothing falls back to the host GCC unnoticed.
SHIM=$KOUT/.shim; mkdir -p "$SHIM"
for c in gcc g++ cc c++; do printf '#!/bin/sh\necho "host $0 called: $*" >&2\nexit 1\n' > "$SHIM/$c"; chmod +x "$SHIM/$c"; done
ln -sf "$KT/bin/dtc" "$SHIM/dtc"
PATH=$SHIM:$B/prebuilts/tools-lineage/linux-x86/bin:$BT/linux-x86/bin:$CL/bin:$PATH \
BISON_PKGDATADIR=$BT/common/bison \
"$BT/linux-x86/bin/make" -j"${JOBS:-10}" -C "$SRC" O="$KOUT" ARCH=arm64 \
  HOSTCFLAGS="$SYS -I$KT/include" \
  HOSTLDFLAGS="$SYS -Wl,-rpath,$KT/lib64 -L $KT/lib64 -fuse-ld=lld --rtlib=compiler-rt" \
  LLVM=1 LLVM_IAS=1 LZ4=$KT/bin/lz4 \
  LEX=$BT/linux-x86/bin/flex YACC=$BT/linux-x86/bin/bison M4=$BT/linux-x86/bin/m4 \
  CROSS_COMPILE=$B/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android- \
  CROSS_COMPILE_ARM32=$B/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin/arm-linux-androideabi- \
  HOSTCC=$CL/bin/clang HOSTCXX=$CL/bin/clang++ HOSTLD=$CL/bin/ld.lld HOSTAR=$CL/bin/llvm-ar \
  DTC_EXT=$KT/bin/dtc CLANG_TRIPLE=aarch64-linux-gnu- CC=clang \
  PAHOLE=$KT/bin/pahole Image.lz4
