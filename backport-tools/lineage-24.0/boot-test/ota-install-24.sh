#!/bin/bash
# Wait for the 24 build, then install its OTA into the inactive slot with
# update_engine from the running system. Does not reboot.
# Usage: ota-install-24.sh <build log>
BL=${1:?build log}
P=~/android/lineage-24.0/out/target/product/sargo
log() { echo "$(date +%T) $*"; }
until grep -q '^exit=' $BL; do sleep 30; done
grep '^exit=' $BL; grep -q '^exit=0' $BL || { log "BUILD FAILED"; grep -E "FAILED:|error:" $BL | head -20; tail -20 $BL; exit 1; }
Z=$P/lineage_sargo-ota.zip; ls -l --time-style=+%T $Z
M=$(unzip -p $Z META-INF/com/android/metadata)
OFF=$(echo "$M" | sed -n 's/.*[=,]payload.bin:\([0-9]*\):\([0-9]*\).*/\1/p' | head -1)
SIZE=$(echo "$M" | sed -n 's/.*[=,]payload.bin:\([0-9]*\):\([0-9]*\).*/\2/p' | head -1)
HDR=$(unzip -p $Z payload_properties.txt)
log "payload offset=$OFF size=$SIZE; running slot $(adb shell getprop ro.boot.slot_suffix | tr -d '\r')"
timeout 20 adb root >/dev/null; sleep 3; timeout 60 adb wait-for-device
adb shell 'rm -f /data/ota_package/update.zip'
adb push $Z /data/ota_package/update.zip | tail -1
adb shell 'chown system:cache /data/ota_package/update.zip; chmod 644 /data/ota_package/update.zip; restorecon /data/ota_package/update.zip'
log "update_engine install to the inactive slot"
adb shell "update_engine_client --update --follow --payload=file:///data/ota_package/update.zip --offset=$OFF --size=$SIZE --headers=\"$HDR\"" 2>&1 | grep -vE "UPDATE_STATUS_DOWNLOADING|progress" | tail -8
adb shell rm -f /data/ota_package/update.zip
log "installed; not rebooting"
