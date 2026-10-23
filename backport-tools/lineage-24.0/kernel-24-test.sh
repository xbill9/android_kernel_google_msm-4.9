#!/bin/bash
# Build the 4.9 kernel with LineageOS 24's kernel make flags plus bonito's KERNEL_CROSS_COMPILE,
# outside the platform build. A gcc shim first on PATH fails if host GCC is still used.
# Usage: kernel-24-test.sh <shim dir> [targets...]; the shim dir holds failing gcc/cc/g++/c++ stubs
# and a dtc symlink (prebuilts/kernel-build-tools/linux-x86/bin/dtc). O= sets the output dir.
set -o pipefail
B=~/android/lineage-24.0; cd $B || exit 1
SHIM=$1; shift
CL=$B/prebuilts/clang/kernel/linux-x86/clang-r416183b
G64=$B/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
G32=$B/prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9/bin/arm-linux-androideabi-
SYS=--sysroot=$B/prebuilts/gcc/linux-x86/host/x86_64-linux-glibc2.17-4.8/sysroot
export PATH=$SHIM:$B/prebuilts/tools-lineage/linux-x86/bin:$B/prebuilts/build-tools/linux-x86/bin:$CL/bin:$PATH
export BISON_PKGDATADIR=$B/prebuilts/build-tools/common/bison
O=${O:-$B/out/kernel-24-test}
mkdir -p $O
$B/prebuilts/build-tools/linux-x86/bin/make -j8 \
  HOSTCFLAGS="$SYS -I$B/prebuilts/kernel-build-tools/linux-x86/include" \
  HOSTLDFLAGS="$SYS -Wl,-rpath,$B/prebuilts/kernel-build-tools/linux-x86/lib64 -L $B/prebuilts/kernel-build-tools/linux-x86/lib64 -fuse-ld=lld --rtlib=compiler-rt" \
  LLVM=1 LLVM_IAS=1 LZ4=$B/prebuilts/kernel-build-tools/linux-x86/bin/lz4 \
  LEX=$B/prebuilts/build-tools/linux-x86/bin/flex YACC=$B/prebuilts/build-tools/linux-x86/bin/bison M4=$B/prebuilts/build-tools/linux-x86/bin/m4 \
  -C kernel/google/msm-4.9 O=$O ARCH=arm64 \
  CROSS_COMPILE=$G64 CROSS_COMPILE_ARM32=$G32 HOSTCC=$CL/bin/clang HOSTCXX=$CL/bin/clang++ HOSTLD=$CL/bin/ld.lld HOSTAR=$CL/bin/llvm-ar \
  DTC_EXT=$B/prebuilts/kernel-build-tools/linux-x86/bin/dtc CLANG_TRIPLE=aarch64-linux-gnu- CC=clang "$@"
