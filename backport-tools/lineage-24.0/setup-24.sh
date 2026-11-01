#!/bin/bash
# First-time setup of the LineageOS 24.0 tree on a new build machine. Run once; after that, sargo-build.
set -eu
HERE=$(dirname "$(readlink -f "$0")")
T=~/android/lineage-24.0
mkdir -p "$T/.repo/local_manifests" ~/sargo-bpf-test/logs
cd "$T"
[ -d .repo/manifests ] || repo init -u https://github.com/LineageOS/android.git -b lineage-24.0 \
  --git-lfs --depth=1 --no-clone-bundle
cp "$HERE/local_manifest-sargo.xml" .repo/local_manifests/sargo.xml
for attempt in 1 2 3 4 5; do
  repo sync -c -j8 --no-tags --no-clone-bundle --optimized-fetch && break
  [ "$attempt" = 5 ] && exit 1
  sleep 60
done
bash "$HERE/fetch-clang-24.sh"
