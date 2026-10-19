#!/bin/bash
# Set up a build machine: kernel source, pinned toolchains, lz4 and mkbootimg.
# Usage: setup.sh   (lays out $SARGO_ROOT, default ~/sargo-bpf-test)
#
# Host packages (Debian/Ubuntu): git make gcc bc bison flex libssl-dev
#   libelf-dev python3 cpio
# For build-selftests.sh, see the package list at the top of that script.
set -e
ROOT=${SARGO_ROOT:-$HOME/sargo-bpf-test}
mkdir -p "$ROOT/tc" && cd "$ROOT"

# repo url commit
pin() {
	local dir=$1 url=$2 sha=$3
	if [ ! -d "$dir/.git" ]; then
		git init -q "$dir"
		git -C "$dir" remote add origin "$url"
	fi
	git -C "$dir" fetch -q --depth 1 origin "$sha"
	git -C "$dir" checkout -q --detach "$sha"
	echo "$dir @ $(git -C "$dir" rev-parse --short=12 HEAD)"
}

[ -d ksargo/.git ] || git clone -q https://github.com/xbill9/android_kernel_google_msm-4.9.git ksargo
git -C ksargo remote get-url lineage >/dev/null 2>&1 ||
	git -C ksargo remote add lineage https://github.com/LineageOS/android_kernel_google_msm-4.9.git

L=https://github.com/LineageOS
pin tc/android_prebuilts_clang_kernel_linux-x86_clang-r416183b \
	$L/android_prebuilts_clang_kernel_linux-x86_clang-r416183b \
	54220fd601050b350b2af7adc913089ebf0e7aed
pin tc/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9 \
	$L/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9 \
	5e030eafe024784a73cdf47e6936ac0dbfc763dc
pin tc/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9 \
	$L/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9 \
	111258a10e017f067b27e6cfcea7619d753f3309
pin tc/lz4 https://github.com/lz4/lz4 5ff839680134437dbf4678f3d0c7b371d84f4964
make -s -C tc/lz4/programs lz4

if [ ! -f tools/mkbootimg.py ]; then
	git clone -q --depth 1 https://android.googlesource.com/platform/system/tools/mkbootimg tools
fi
echo "done; build with: $ROOT/ksargo/backport-tools/build.sh [Image.lz4 modules]"
