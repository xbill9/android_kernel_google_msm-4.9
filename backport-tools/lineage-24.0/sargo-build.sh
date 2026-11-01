#!/bin/bash
# Pull upstream, build the sargo ROM, publish it to GitHub as a prerelease.
# Stops at the first failure. Installed as ~/bin/sargo-build.
set -euo pipefail
HERE=$(dirname "$(readlink -f "$0")")
T=~/android/lineage-24.0
OUT=$T/out/target/product/sargo
LOG=~/sargo-bpf-test/logs/build-24.log
REPO=xbill9/android_kernel_google_msm-4.9
DATE=$(date +%Y%m%d)
TAG=lineage-24.0-sargo-$DATE
DIR=~/sargo-bpf-test/build-24.0-$DATE

echo "== 1/3 pull upstream"
git -C "$HERE" pull --ff-only
cp "$HERE/local_manifest-sargo.xml" "$T/.repo/local_manifests/sargo.xml"
cd "$T"
repo sync -c -j8 --no-tags --no-clone-bundle --optimized-fetch
behind=$(bash "$HERE/update-forks.sh" | grep -v ' 0 behind' || true)
if [ -n "$behind" ]; then
  echo "$behind"
  echo "== STOPPED: forks are behind upstream. Run update-forks.sh --apply, then sargo-build again."
  exit 1
fi

echo "== 2/3 build (log: $LOG)"
( set +eu +o pipefail
  source build/envsetup.sh
  lunch lineage_sargo cp2a userdebug && m bacon -j"$(nproc)" ) 2>&1 | tee "$LOG"

echo "== 3/3 publish $TAG"
mkdir -p "$DIR"
cp "$OUT/lineage_sargo-ota.zip" "$DIR/lineage-24.0-$DATE-UNOFFICIAL-sargo.zip"
cp "$OUT"/{boot,dtbo,vbmeta}.img "$DIR/"
cp .repo/local_manifests/sargo.xml "$DIR/local_manifest-sargo.xml"
rm -f "$DIR/SHA256SUMS"
(cd "$DIR" && sha256sum * > SHA256SUMS.tmp && mv SHA256SUMS.tmp SHA256SUMS)

if gh release view "$TAG" -R "$REPO" >/dev/null 2>&1; then
  gh release upload "$TAG" "$DIR"/* -R "$REPO" --clobber
else
  gh release create "$TAG" "$DIR"/* -R "$REPO" --prerelease \
    --title "LineageOS 24.0 for Pixel 3a (sargo), $DATE" \
    --notes "Automated build. Not tested on a phone."
fi
echo "== done: https://github.com/$REPO/releases/tag/$TAG"
