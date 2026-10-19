#!/bin/bash
# Build sargo kernel the way LineageOS does (clang r416183b + GNU binutils). Usage: build.sh [make targets...]
TC=~/sargo-bpf-test/tc
export PATH=$TC/lz4/programs:$TC/android_prebuilts_clang_kernel_linux-x86_clang-r416183b/bin:$TC/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9/bin:$TC/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9/bin:$PATH
export LD_LIBRARY_PATH=$TC/android_prebuilts_clang_kernel_linux-x86_clang-r416183b/lib64
cd ~/sargo-bpf-test/kbase
M="make O=../kbase-out ARCH=arm64 CC=clang CLANG_TRIPLE=aarch64-linux-gnu- CROSS_COMPILE=aarch64-linux-android- CROSS_COMPILE_ARM32=arm-linux-androideabi- -j10"
[ -f ../kbase-out/.config ] || $M bonito_defconfig
$M "${@:-Image.lz4}"
