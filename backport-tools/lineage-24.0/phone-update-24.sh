#!/bin/bash
# Sync, build, install on the attached phone, put GApps back and compile apps, in one go.
# The only manual step is on the phone, in recovery: "Install anyway", then "Reboot system now".
# Installs into the phone's inactive slot from its running system, so each run flips the slot.
# Env: SKIP_SYNC=1, SKIP_BUILD=1 (install the last build again).
# Usage: phone-update-24.sh
set -u
B=$(dirname "$(readlink -f "$0")")
T=~/android/lineage-24.0
L=~/sargo-bpf-test/logs
G=~/sargo-bpf-test/gapps
export PATH=$T/out/host/linux-x86/bin:$PATH
log() { echo; echo "=== $(date +%T) $*"; }
die() { echo "!!! STOPPED: $*"; exit 1; }
prop() { timeout 5 adb shell getprop "$1" 2>/dev/null | tr -d '\r'; }
wait_boot() {
  for i in $(seq 1 200); do [ "$(prop sys.boot_completed)" = 1 ] && return 0; sleep 3; done
  return 1
}
mkdir -p "$L"

log "checking the phone"
command -v adb >/dev/null || die "no adb in $T/out/host/linux-x86/bin; build once first"
[ "$(adb devices | grep -c 'device$')" = 1 ] || die "need exactly one phone connected and booted (adb devices)"
[ "$(prop ro.product.device)" = sargo ] || die "the connected phone is not a sargo"
echo "phone: slot $(prop ro.boot.slot_suffix), $(prop ro.lineage.version)"

ZIP=$(ls $G/MindTheGapps-17*-sargo.zip 2>/dev/null | tail -1)
if [ -z "$ZIP" ]; then
  SRC=$(ls $G/MindTheGapps-17*-arm64-*.zip 2>/dev/null | grep -v -- -sargo | tail -1)
  [ -n "$SRC" ] || die "no GApps zip in $G (copy MindTheGapps-17...-sargo.zip there)"
  log "repacking $SRC"; bash "$B/repack-gapps.sh" "$SRC" || die "repack-gapps.sh failed"
  ZIP=$(ls $G/MindTheGapps-17*-sargo.zip | tail -1)
fi
echo "gapps: $ZIP"

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

log "installing the update into the other slot (about 7 minutes; do not use the phone)"
OLD=$(prop ro.boot.slot_suffix)
bash "$B/boot-test/ota-install-24.sh" $L/build-24.log | tee $L/ota-install-last.log
grep -q 'kSuccess' $L/ota-install-last.log || die "the install did not report kSuccess"

log "rebooting into the new build"
timeout 20 adb reboot; sleep 15
wait_boot || die "the phone did not finish booting; it may be in the bootloader"
NEW=$(prop ro.boot.slot_suffix)
echo "phone: slot $NEW, $(prop ro.lineage.version), $(timeout 5 adb shell getenforce | tr -d '\r')"
[ "$NEW" != "$OLD" ] || die "still on slot $OLD; the new slot did not boot"

log "GApps: the phone goes to recovery now"
timeout 20 adb reboot sideload
for i in $(seq 1 90); do adb devices | grep -q 'sideload$' && break; sleep 2; done
adb devices | grep -q 'sideload$' || die "the phone did not reach sideload mode"
echo
echo ">>> ON THE PHONE: when it says 'Signature verification failed', tap YES (install anyway)."
echo ">>> When it finishes, tap 'Reboot system now'. This script waits for that."
adb sideload "$ZIP" >/dev/null 2>&1   # ends at about 47%; that is normal
sleep 20
wait_boot || die "the phone did not boot after GApps; tap 'Reboot system now', then run: bash $B/boot-test/post-install-24.sh"

log "permissions and app compile (about 3 minutes)"
bash "$B/boot-test/post-install-24.sh"

log "DONE: $(prop ro.lineage.version) on slot $(prop ro.boot.slot_suffix)"
