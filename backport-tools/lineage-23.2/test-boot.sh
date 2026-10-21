#!/bin/bash
# One unattended boot test: RAM-boot an image on slot a, which also holds the
# flashed known-good 23.2 boot image. A failed test reboots into that image on
# its own; the script then saves /metadata/boot-kmsg.txt (kmsglog images).
# Slot b (22.2) must not boot: /data belongs to Android 16 since 2026-10-21.
# Needs: fastboot oem off-mode-charge 0 (a power-off with USB in boots straight up).
# Usage: test-boot.sh <boot.img> [label]
IMG=${1:?boot.img}
LABEL=${2:-$(date +%H%M%S)}
OUT=~/sargo-bpf-test/logs/boot-tests/$LABEL; mkdir -p $OUT
log() { echo "$(date +%T) $*" | tee -a $OUT/run.log; }
usb() { lsusb | grep -o '18d1:[0-9a-f]*' | head -1; }
# Kernel build string of an image, to tell the tested kernel from the flashed one.
kver() { ~/android/lineage-23.2/out/host/linux-x86/bin/unpack_bootimg --boot_img "$1" --out $OUT/u >/dev/null &&
         lz4 -dc $OUT/u/kernel 2>/dev/null | strings | grep -m1 "^Linux version 4" | cut -d' ' -f3,12-; rm -rf $OUT/u; }
WANT=$(kver "$IMG")
log "testing kernel: $WANT"

if [ "$(usb)" != 18d1:4ee0 ]; then
  adb reboot bootloader
  for i in $(seq 1 60); do [ "$(usb)" = 18d1:4ee0 ] && break; sleep 1; done
fi
[ "$(usb)" = 18d1:4ee0 ] || { log "FAIL: no bootloader"; exit 1; }
[ "$(timeout 10 fastboot getvar current-slot 2>&1 | awk '/current-slot/{print $2}')" = a ] ||
  timeout 10 fastboot --set-active=a
log "RAM-booting $IMG"
timeout 60 fastboot boot "$IMG" 2>&1 | tail -1 | tee -a $OUT/run.log

# The bootloader stays enumerated briefly after the boot command; wait for it to go.
for i in $(seq 1 30); do [ "$(usb)" != 18d1:4ee0 ] && break; sleep 1; done

seen=""; t0=$(date +%s)
while [ $(( $(date +%s) - t0 )) -lt 600 ]; do
  u=$(usb)
  [ "$u" != "$seen" ] && { log "usb: ${u:-none}"; seen=$u; }
  if [ "$u" = 18d1:4ee7 ] && timeout 5 adb get-state >/dev/null 2>&1; then
    v=$(timeout 10 adb shell cat /proc/version | cut -d' ' -f3,12- | tr -d '\r')
    log "adb up: $v"
    if [ "$v" = "$WANT" ]; then log "tested image is running"; echo "$v" > $OUT/booted; exit 0; fi
    break                                         # fell back to the flashed image
  fi
  [ "$u" = 18d1:4ee0 ] && { log "landed in bootloader; rebooting slot a"; timeout 10 fastboot --set-active=a; timeout 10 fastboot reboot; }
  sleep 2
done

timeout 30 adb root >/dev/null; sleep 3; timeout 30 adb wait-for-device
timeout 30 adb pull /metadata/boot-kmsg.txt $OUT/ 2>&1 | tail -1 | tee -a $OUT/run.log
log "saved to $OUT"
