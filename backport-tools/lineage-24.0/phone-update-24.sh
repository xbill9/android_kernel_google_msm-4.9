#!/bin/bash
# Sync, build, then phone-install-24.sh: install on the attached phone, put GApps back and
# compile apps, in one go. The only manual step is on the phone, in recovery: "Install anyway",
# then "Reboot system now".
# Env: SKIP_SYNC=1, SKIP_BUILD=1 (install the last build again).
# Usage: phone-update-24.sh
set -u
B=$(dirname "$(readlink -f "$0")")
T=~/android/lineage-24.0
L=~/sargo-bpf-test/logs
export PATH=$T/out/host/linux-x86/bin:$PATH
log() { echo; echo "=== $(date +%T) $*"; }
die() { echo "!!! STOPPED: $*"; exit 1; }
mkdir -p "$L"

# Fail on a missing phone now rather than after a long build.
command -v adb >/dev/null || die "no adb in $T/out/host/linux-x86/bin; build once first"
[ "$(adb devices | grep -c 'device$')" = 1 ] || die "need exactly one phone connected and booted (adb devices)"

if [ "${SKIP_SYNC:-0}" != 1 ]; then
  log "sync (log: $L/sync-24-*.log)"
  S=$L/sync-24-$(date +%m%d-%H%M).log
  bash "$B/sync-24.sh" > "$S" 2>&1
  grep -q 'DONE exit=0' "$S" || { tail -20 "$S"; die "sync failed, see $S"; }
fi

if [ "${SKIP_BUILD:-0}" != 1 ]; then
  log "build (about 15-90 minutes; log: $L/build-24.log)"
  [ -f $L/build-24.log ] && mv $L/build-24.log $L/build-24-$(date +%m%d-%H%M)-old.log
  bash "$B/build-24-guarded.sh"
  grep -q '^exit=0' $L/build-24.log || { grep -E 'FAILED:|error:|DISK GUARD' $L/build-24.log | head -20; die "build failed, see $L/build-24.log"; }
  grep '^real' $L/build-24.log
fi
grep -q '^exit=0' $L/build-24.log 2>/dev/null || die "no finished build in $L/build-24.log"

exec bash "$B/phone-install-24.sh" $T/out/target/product/sargo/lineage_sargo-ota.zip
