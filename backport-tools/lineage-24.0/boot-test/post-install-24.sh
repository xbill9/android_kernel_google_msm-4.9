#!/bin/bash
# Run once after an OTA and the GApps sideload, on the booted new system.
# After an OTA, apps stay at the "verify" compiler filter (pm.dexopt.boot-after-ota=verify) until
# the background job runs while the phone is idle and charging; until then they run interpreted
# or JIT-compiled. This grants Play Services any runtime permission it lacks and runs that job.
# Usage: post-install-24.sh
log() { echo "$(date +%T) $*"; }
for i in $(seq 1 120); do
  [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && break; sleep 3
done
timeout 20 adb root >/dev/null; sleep 3; timeout 60 adb wait-for-device
log "$(adb shell 'echo $(getprop ro.boot.slot_suffix) $(getenforce) $(getprop ro.lineage.version)' | tr -d '\r')"

adb shell 'pm path com.google.android.gms >/dev/null' || { log "no Play Services: sideload GApps first"; exit 1; }
log "Play Services permissions granted: $(adb shell 'n=0; for p in $(dumpsys package com.google.android.gms | grep -E "^ +android.permission\.[A-Z_]+: granted=false" | sed "s/:.*//" | sort -u); do pm grant com.google.android.gms $p 2>/dev/null && n=$((n+1)); done; echo $n' | tr -d '\r')"

count() { adb shell "pm art dump 2>/dev/null | grep -cE 'arm64: \[status=$1\]'" | tr -d '\r'; }
log "before: verify=$(count verify) speed-profile=$(count speed-profile)"
adb shell pm bg-dexopt-job | tail -1
log "after: verify=$(count verify) speed-profile=$(count speed-profile)"
