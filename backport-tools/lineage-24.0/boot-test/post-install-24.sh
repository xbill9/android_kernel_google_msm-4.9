#!/bin/bash
# Run once after an OTA and the GApps sideload, on the booted new system.
# After an OTA, apps stay at the "verify" compiler filter (pm.dexopt.boot-after-ota=verify) until
# the background job runs while the phone is idle and charging; until then they run interpreted
# or JIT-compiled. This grants Play Services any runtime permission it lacks and runs that job.
# It also checks mobile data: CarrierSettings (com.google.android.carrier) must be enabled, since it
# reads fi_us.pb with the VoLTE key; with it disabled, IMS goes off in the modem and LTE drops.
# A disabled CarrierSettings is enabled again (with a reboot). With a SIM in, it exits 1 if data is
# not in service within 3 minutes.
# Usage: post-install-24.sh
log() { echo "$(date +%T) $*"; }
for i in $(seq 1 120); do
  [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && break; sleep 3
done
boot_wait() { for i in $(seq 1 120); do
  [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && return 0; sleep 3; done; return 1; }
timeout 20 adb root >/dev/null; sleep 3; timeout 60 adb wait-for-device

# CarrierSettings must be on; it survives OTAs disabled, so check it on every install
if adb shell pm list packages -d | tr -d '\r' | grep -qx 'package:com.google.android.carrier'; then
  log "CarrierSettings was disabled: enabling it and rebooting"
  adb shell pm enable com.google.android.carrier >/dev/null
  timeout 20 adb reboot; sleep 15; boot_wait || { log "the phone did not boot after the reboot"; exit 1; }
  timeout 20 adb root >/dev/null; sleep 3; timeout 60 adb wait-for-device
fi
log "$(adb shell 'echo $(getprop ro.boot.slot_suffix) $(getenforce) $(getprop ro.lineage.version)' | tr -d '\r')"

adb shell 'pm path com.google.android.gms >/dev/null' || { log "no Play Services: sideload GApps first"; exit 1; }
log "Play Services permissions granted: $(adb shell 'n=0; for p in $(dumpsys package com.google.android.gms | grep -E "^ +android.permission\.[A-Z_]+: granted=false" | sed "s/:.*//" | sort -u); do pm grant com.google.android.gms $p 2>/dev/null && n=$((n+1)); done; echo $n' | tr -d '\r')"

count() { adb shell "pm art dump 2>/dev/null | grep -cE 'arm64: \[status=$1\]'" | tr -d '\r'; }
log "before: verify=$(count verify) speed-profile=$(count speed-profile)"
adb shell pm bg-dexopt-job | tail -1
log "after: verify=$(count verify) speed-profile=$(count speed-profile)"

# Mobile data: with a SIM in, it must be in service, and a Fi SIM must get VoLTE from fi_us.pb
if [ "$(adb shell getprop gsm.sim.state | tr -d '\r')" != LOADED ]; then
  log "mobile data: no SIM loaded, not checked"; exit 0; fi
for i in $(seq 1 36); do
  st=$(adb shell 'dumpsys telephony.registry 2>/dev/null' | grep -m1 -oE 'mDataRegState=[0-9]\([A-Z_]+\)'); case $st in *IN_SERVICE*) break ;; esac; sleep 5
done
volte=$(adb shell 'grep -h carrier_volte_available_bool /data/user_de/0/com.android.phone/files/carrierconfig-com.google.android.carrier-*.xml 2>/dev/null' | grep -oE 'value="[a-z]+"' | head -1)
sim=$(adb shell getprop gsm.sim.operator.alpha | tr -d '\r')
log "mobile data: $st, $sim, VoLTE ${volte:-not set}"
if [[ "$sim" == *" Fi" ]] && [ "$volte" != 'value="true"' ]; then
  log "Fi SIM without carrier_volte_available_bool=true: CarrierSettings did not load fi_us.pb"; exit 1; fi
case $st in *IN_SERVICE*) ;; *)
  log "mobile data is not in service after 3 minutes. If CarrierSettings was disabled, the modem keeps IMS off"
  log "across reboots and reinstalls: flash stock (flash-all.sh), check data, then reinstall the ROM."
  log "See backport-tools/lineage-24.0/PORTING-NOTES-mobile-data.md"; exit 1 ;; esac
