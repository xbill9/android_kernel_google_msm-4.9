#!/bin/bash
# Sparse checkout of the clang prebuilts LineageOS 24.0 uses, from the partial clone at
# android-17.0.0_r1, one directory at a time with retries (googlesource drops long transfers).
P=~/android/lineage-24.0/prebuilts/clang/host/linux-x86
retry() { for i in 1 2 3 4 5 6 7 8; do "$@" && return 0; echo "  retry $i: $*"; sleep 20; done; return 1; }
H=$(git -C $P rev-parse HEAD)
git -C $P sparse-checkout init --no-cone
pat='/*\n!/*/\n'
for d in soong docs kleaf mlgo-models profiles embedded-sysroots llvm-binutils-stable clang-stable clang-3289846 clang-r584948 clang-r596125; do
  pat="$pat/$d/\n"
  printf "$pat" > $P/.git/info/sparse-checkout
  retry git -C $P checkout -q $H || { echo "FAILED at $d"; exit 1; }
  echo "$(date +%T) ok $d: $(du -sh $P | cut -f1)"
done
echo "DONE clang: $(git -C $P ls-files | wc -l) files, $(du -sh $P | cut -f1)"
