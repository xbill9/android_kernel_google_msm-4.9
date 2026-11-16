#!/bin/bash
# LineageOS 24.0 for the Pixel 3a (sargo): one command. Installed as ~/bin/sargo.
#
#   sargo update     pull LineageOS (and rebase the forks), commit the upstream kernel fixes that
#                    apply as is, build the ROM with this kernel tree, then test that ROM's kernel
#                    on the plugged-in phone (RAM boot: nothing is flashed). On PASS: push the
#                    kernel and publish the ROM as a GitHub release. On FAIL nothing is published.
#   sargo install    install the newest release on the phone (args: tested | rom.zip [gapps.zip])
#   sargo test       test this kernel tree on the phone without building the ROM (a kernel change
#                    in progress; builds the kernel alone, about 35 minutes)
#
# One sargo command runs at a time (they share the build tree and the phone).
# Logs: ~/sargo-bpf-test/logs/; test reports: ~/sargo-logs/; results: boot-test/results.tsv.
set -euo pipefail
HERE=$(dirname "$(readlink -f "$0")")
SRC=$(cd "$HERE/../.." && pwd)

case ${1:-} in
  install) shift; exec bash "$HERE/phone-install-24.sh" "$@" ;;
  test) shift; exec bash "$HERE/sargo-test.sh" "$@" ;;
  update) ;;
  *) sed -n '2,15p' "$0"; exit 1 ;;
esac

T=~/android/lineage-24.0
OUT=$T/out/target/product/sargo
LOG=~/sargo-bpf-test/logs/build-24.log
REPO=xbill9/android_kernel_google_msm-4.9
DATE=$(date +%Y%m%d-%H%M)   # unique, so a rebuild never replaces a published release
TAG=lineage-24.0-sargo-$DATE
DIR=~/sargo-bpf-test/build-24.0-$DATE
step() { echo; echo "== $(date +%T) $*"; }
stop() { echo; echo "== STOPPED: $*"; exit 1; }

. "$HERE/sargo-lock.sh"; sargo_lock "sargo update"
export SARGO_LOCK_OWNER=$$   # the test below runs under this lock

step "1/4 pull: kernel tree, upstream kernel fixes, LineageOS, forks"
git -C "$SRC" pull -q --ff-only || stop "this kernel tree and GitHub main have diverged; merge them first"
bash "$SRC/backport-tools/check-upstream.sh" --apply
cp "$HERE/local_manifest-sargo.xml" "$T/.repo/local_manifests/sargo.xml"
cd "$T"
repo sync -c -j8 --no-tags --no-clone-bundle --optimized-fetch
behind=$(bash "$HERE/update-forks.sh" | grep -v ' 0 behind' | cut -d: -f1 || true)
if [ -n "$behind" ]; then
  echo "rebasing forks: $behind"
  bash "$HERE/update-forks.sh" --apply $behind || stop "a fork rebase conflicts (above); resolve it in that fork, push, run sargo update again"
  repo sync -c --force-sync $behind
fi
# The ROM builds this tree's HEAD, including fixes not pushed yet; they are pushed only on PASS.
KC=$(git -C "$SRC" rev-parse HEAD)
git -C "$T/kernel/google/msm-4.9" fetch -q --depth=1 "file://$SRC" "$KC"
git -C "$T/kernel/google/msm-4.9" checkout -q "$KC"
echo "kernel: $(git -C "$SRC" log -1 --format='%h %s' "$KC" | cut -c1-80)"

step "2/4 build the ROM (log: $LOG)"
( set +eu +o pipefail
  source build/envsetup.sh
  lunch lineage_sargo cp2a userdebug && m bacon -j"$(nproc)" ) > "$LOG" 2>&1
grep -q '#### build completed successfully' "$LOG" || { tail -30 "$LOG"; stop "ROM build failed (log: $LOG)"; }
[ "$(git -C "$T/kernel/google/msm-4.9" rev-parse HEAD)" = "$KC" ] || stop "the ROM's kernel checkout moved during the build"

step "3/4 test the ROM's kernel on the phone"
[ "$(adb devices | grep -c 'device$')" = 1 ] || stop "no phone on adb: the ROM is built ($OUT) but not tested, so not published"
KOUT=$OUT/obj/KERNEL_OBJ bash "$HERE/sargo-test.sh" --no-build --push \
  || stop "the test failed (report above); nothing pushed or published"
R=$(tail -1 "$HERE/boot-test/results.tsv")

step "4/4 publish $TAG"
mkdir -p "$DIR"
cp "$OUT/lineage_sargo-ota.zip" "$DIR/lineage-24.0-$DATE-UNOFFICIAL-sargo.zip"
cp "$OUT"/{boot,dtbo,vbmeta}.img "$DIR/"
cp .repo/local_manifests/sargo.xml "$DIR/local_manifest-sargo.xml"
(cd "$DIR" && sha256sum * > SHA256SUMS.tmp && mv SHA256SUMS.tmp SHA256SUMS)
NOTES="Kernel $(cut -f2 <<< "$R") ($KC on GitHub main): sargo-test $(cut -f3 <<< "$R") on $(cut -f1 <<< "$R"), RAM-booted on a Pixel 3a.
Not yet installed on a phone: \`sargo install\` marks it Latest when its hardware check passes."
gh release create "$TAG" "$DIR"/* -R "$REPO" --prerelease \
  --title "LineageOS 24.0 for Pixel 3a (sargo), $DATE" --notes "$NOTES"
step "done: https://github.com/$REPO/releases/tag/$TAG  (install: sargo install)"
