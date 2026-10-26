#!/bin/bash
# RAM-boot <img> on slot a (24.0 full build). On a hang the kmsglog timer returns
# to the bootloader; then boot slot b with the logger-free test image and pull
# /metadata logs. Usage: test-boot-a24.sh <img> <label>
# FALLBACK= names the slot b image (default boot-24-perm-nolog.img).
FALLBACK=${FALLBACK:-~/sargo-bpf-test/kmsglog/boot-24-perm-nolog.img}
IMG=$1; OUT=~/sargo-bpf-test/logs/boot-tests/$2; mkdir -p $OUT
log() { echo "$(date +%T) $*" | tee -a $OUT/run.log; }
usb() { lsusb | grep -o '18d1:[0-9a-f]*' | head -1; }
waitusb() { for i in $(seq 1 $2); do [ "$(usb)" = $1 ] && return 0; sleep 1; done; return 1; }
toslot() { timeout 10 fastboot --set-active=$1; timeout 20 fastboot reboot bootloader; sleep 3; waitusb 18d1:4ee0 60; }
[ "$(usb)" = 18d1:4ee0 ] || { timeout 20 adb reboot bootloader; waitusb 18d1:4ee0 90 || { log "FAIL no bootloader"; exit 1; }; }
toslot a; log "slot $(timeout 5 fastboot getvar current-slot 2>&1 | awk '/current-slot/{print $2}'); booting $IMG"
timeout 60 fastboot boot "$IMG" >/dev/null 2>&1; sleep 10
t0=$(date +%s)
while [ $(( $(date +%s) - t0 )) -lt 400 ]; do
  if [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; then
    log "BOOTED $(timeout 5 adb shell getprop ro.boot.slot_suffix | tr -d '\r')"; exit 0; fi
  [ "$(usb)" = 18d1:4ee0 ] && { log "HUNG: back in bootloader"; break; }
  sleep 3
done
[ "$(usb)" = 18d1:4ee0 ] || { log "no bootloader after 400s"; exit 1; }
toslot b; timeout 60 fastboot boot "$FALLBACK" >/dev/null 2>&1
for i in $(seq 1 100); do [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && break; sleep 3; done
timeout 30 adb root >/dev/null; sleep 3; timeout 30 adb wait-for-device
adb pull /metadata/boot-kmsg.txt $OUT/ >/dev/null && adb pull /metadata/logcat.txt $OUT/ >/dev/null
# pstore holds the console of the hung boot until the next reboot
adb pull /sys/fs/pstore $OUT/pstore >/dev/null 2>&1
log "logs pulled to $OUT (now on slot b test boot)"
