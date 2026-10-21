#!/bin/bash
# Partial clone + sparse checkout of the two huge AOSP prebuilt repos, keeping only what the build uses.
set -e
cd ~/android/lineage-23.2
get() {  # get PATH REPO DIR...
	local path=$1 repo=$2; shift 2
	rm -rf "$path"
	git clone -q --filter=blob:none --no-checkout --depth=1 --branch android-16.0.0_r4 \
		"https://android.googlesource.com/$repo" "$path" 2>&1 | grep -v "is not a commit" || true
	git -C "$path" sparse-checkout init --no-cone
	printf '/*\n!/*/\n' > "$path/.git/info/sparse-checkout"   # root files only ...
	for d in "$@"; do echo "/$d/" >> "$path/.git/info/sparse-checkout"; done  # ... plus these dirs
	git -C "$path" checkout -q FETCH_HEAD 2>/dev/null || git -C "$path" checkout -q "$(git -C "$path" rev-parse HEAD)"
	echo "$path: $(git -C "$path" ls-files | wc -l) files, $(du -sh "$path" | cut -f1)"
}
time get prebuilts/clang/host/linux-x86 platform/prebuilts/clang/host/linux-x86 \
	clang-r563880c clang-r563880 clang-3289846 llvm-binutils-stable clang-stable soong kleaf mlgo-models profiles docs
time get prebuilts/rust platform/prebuilts/rust linux-x86 soong tests
echo done
