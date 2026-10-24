#!/bin/bash
# RAM-boot a logged image on slot b (24), then return to slot a (23.2) and pull the log.
IMG=${1:?boot.img}; OUT=~/sargo-bpf-test/logs/boot-tests/${2:?label}; mkdir -p $OUT
log() { echo "$(date +%T) $*" | tee -a $OUT/run.log; }
usb() { lsusb | grep -o '18d1:[0-9a-f]*' | head -1; }
# Set aside logs from earlier boots so this run's files start clean.
timeout 20 adb root >/dev/null; sleep 3; timeout 30 adb wait-for-device
# /metadata is 10 MB and holds vold keys and aconfig flags: never keep old logs there.
timeout 10 adb shell 'rm -f /metadata/boot-kmsg* /metadata/logcat.txt*; df -h /metadata | tail -1' | tee -a $OUT/run.log
adb reboot bootloader
for i in $(seq 1 60); do [ "$(usb)" = 18d1:4ee0 ] && break; sleep 1; done
[ "$(usb)" = 18d1:4ee0 ] || { log "FAIL: no bootloader"; exit 1; }
timeout 10 fastboot getvar all 2>&1 | grep -E "current-slot|slot-(successful|unbootable|retry-count)" | tee -a $OUT/run.log
timeout 10 fastboot --set-active=b 2>&1 | tail -1
# fastboot boot uses the slot chosen when the bootloader started, so restart it on b.
timeout 10 fastboot reboot bootloader 2>&1 | tail -1
sleep 3; for i in $(seq 1 60); do [ "$(usb)" = 18d1:4ee0 ] && break; sleep 1; done
cur=$(timeout 10 fastboot getvar current-slot 2>&1 | awk '/current-slot/{print $2}')
log "current-slot after set-active: $cur"
[ "$cur" = b ] || { log "FAIL: slot b not active"; timeout 10 fastboot reboot; exit 3; }
log "RAM-booting $IMG on slot b"
timeout 60 fastboot boot "$IMG" 2>&1 | tail -1 | tee -a $OUT/run.log
for i in $(seq 1 30); do [ "$(usb)" != 18d1:4ee0 ] && break; sleep 1; done
seen=""; t0=$(date +%s); res=timeout
while [ $(( $(date +%s) - t0 )) -lt 420 ]; do
  u=$(usb); [ "$u" != "$seen" ] && { log "usb: ${u:-none}"; seen=$u; }
  [ "$u" = 18d1:4ee0 ] && { res=bootloader; break; }
  if [ -n "$u" ] && [ "$(timeout 5 adb get-state 2>/dev/null)" = device ]; then res=adb; break; fi
  sleep 2
done
log "result: $res"
if [ $res = adb ]; then
  timeout 20 adb shell 'getprop ro.lineage.version; getprop sys.boot_completed; uname -r' | tee -a $OUT/run.log
  timeout 30 adb pull /metadata/boot-kmsg.txt $OUT/ | tail -1
  timeout 60 adb logcat -d > $OUT/logcat.txt 2>&1
  exit 0
fi
if [ $res != bootloader ]; then
  log "no USB: waiting for Power+VolDown"
  until [ "$(usb)" = 18d1:4ee0 ]; do sleep 2; done
  log "bootloader seen"
fi
timeout 10 fastboot --set-active=a; timeout 10 fastboot reboot
timeout 240 adb wait-for-device; timeout 20 adb root >/dev/null; sleep 4; timeout 60 adb wait-for-device
timeout 30 adb pull /metadata/boot-kmsg.txt $OUT/ 2>&1 | tail -1 | tee -a $OUT/run.log
for f in logcat.txt logcat.txt.1 logcat.txt.2; do timeout 60 adb pull /metadata/$f $OUT/ 2>&1 | tail -1 | tee -a $OUT/run.log; done
timeout 10 adb shell 'rm -f /metadata/boot-kmsg* /metadata/logcat.txt*'
log "back on $(timeout 10 adb shell getprop ro.lineage.version)"
# Every boot after --set-active costs slot a a try; mark it so it never runs out.
until [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do sleep 3; done
timeout 10 adb shell bootctl mark-boot-successful && log "slot a marked successful"
