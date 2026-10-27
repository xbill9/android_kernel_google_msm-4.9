#!/bin/bash
# Install a LineageOS 24.0 OTA zip on the attached phone, put GApps back and compile apps.
# The phone must be booted into one of these 24.0 userdebug builds (adb root works). The OTA goes
# into its inactive slot with update_engine, so each run flips the slot.
# The only manual step is on the phone, in recovery: "Install anyway", then "Reboot system now".
# Usage: phone-install-24.sh lineage-24.0-<date>-UNOFFICIAL-sargo.zip [gapps.zip]
#   gapps.zip: the repacked MindTheGapps-17...-sargo.zip, or the original MindTheGapps 17 zip
#   (repacked first). Default: the newest one in ~/sargo-bpf-test/gapps or next to the OTA.
set -u
B=$(dirname "$(readlink -f "$0")")
Z=$(realpath "${1:?usage: phone-install-24.sh rom.zip [gapps.zip]}")
for d in ~/android/lineage-24.0/out/host/linux-x86/bin ~/platform-tools; do [ -x $d/adb ] && PATH=$d:$PATH; done
log() { echo; echo "=== $(date +%T) $*"; }
die() { echo "!!! STOPPED: $*"; exit 1; }
prop() { timeout 5 adb shell getprop "$1" 2>/dev/null | tr -d '\r'; }
wait_boot() {
  for i in $(seq 1 200); do [ "$(prop sys.boot_completed)" = 1 ] && return 0; sleep 3; done
  return 1
}

log "checking"
command -v adb >/dev/null || die "adb not found; install Android platform-tools"
[ -f "$Z" ] && unzip -l "$Z" payload.bin >/dev/null 2>&1 || die "$Z is not an OTA zip with payload.bin"
[ "$(adb devices | grep -c 'device$')" = 1 ] || die "need exactly one phone connected and booted (adb devices)"
[ "$(prop ro.product.device)" = sargo ] || die "the connected phone is not a Pixel 3a (sargo)"
echo "phone: slot $(prop ro.boot.slot_suffix), $(prop ro.lineage.version)"
timeout 20 adb root >/dev/null 2>&1; sleep 3; timeout 60 adb wait-for-device
[ "$(timeout 5 adb shell id -u | tr -d '\r')" = 0 ] || die "adb root does not work: the phone must run a userdebug build of this ROM (see INSTALL.md for a first install)"

G=${2:-}
if [ -z "$G" ]; then
  G=$(ls ~/sargo-bpf-test/gapps/MindTheGapps-17*-sargo.zip "$(dirname "$Z")"/MindTheGapps-17*-sargo.zip 2>/dev/null | tail -1)
  [ -n "$G" ] || G=$(ls ~/sargo-bpf-test/gapps/MindTheGapps-17*-arm64-*.zip "$(dirname "$Z")"/MindTheGapps-17*-arm64-*.zip 2>/dev/null | grep -v -- -sargo | tail -1)
fi
[ -n "$G" ] && [ -f "$G" ] || die "no MindTheGapps 17 zip found; download it (INSTALL.md) and pass it as the second argument"
G=$(realpath "$G")
if [[ "$G" != *-sargo.zip ]]; then
  log "repacking $G for this phone"
  bash "$B/repack-gapps.sh" "$G" || die "repack-gapps.sh failed"
  G=${G%.zip}-sargo.zip
fi
echo "rom:   $Z"
echo "gapps: $G"

log "installing the update into the other slot (about 7 minutes; do not use the phone)"
OLD=$(prop ro.boot.slot_suffix)
M=$(unzip -p "$Z" META-INF/com/android/metadata)
OFF=$(echo "$M" | sed -n 's/.*[=,]payload.bin:\([0-9]*\):\([0-9]*\).*/\1/p' | head -1)
SIZE=$(echo "$M" | sed -n 's/.*[=,]payload.bin:\([0-9]*\):\([0-9]*\).*/\2/p' | head -1)
HDR=$(unzip -p "$Z" payload_properties.txt)
adb shell 'rm -f /data/ota_package/update.zip'
adb push "$Z" /data/ota_package/update.zip | tail -1
adb shell 'chown system:cache /data/ota_package/update.zip; chmod 644 /data/ota_package/update.zip; restorecon /data/ota_package/update.zip'
R=$(adb shell "update_engine_client --update --follow --payload=file:///data/ota_package/update.zip --offset=$OFF --size=$SIZE --headers=\"$HDR\"" 2>&1 | grep -vE "UPDATE_STATUS_DOWNLOADING|progress" | tail -4)
adb shell rm -f /data/ota_package/update.zip
echo "$R"
echo "$R" | grep -q 'kSuccess' || die "the install did not report kSuccess"

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
adb sideload "$G" >/dev/null 2>&1   # ends at about 47%; that is normal
sleep 20
wait_boot || die "the phone did not boot after GApps; tap 'Reboot system now', then run: bash $B/boot-test/post-install-24.sh"

log "permissions and app compile (about 3 minutes)"
bash "$B/boot-test/post-install-24.sh"

log "DONE: $(prop ro.lineage.version) on slot $(prop ro.boot.slot_suffix)"
