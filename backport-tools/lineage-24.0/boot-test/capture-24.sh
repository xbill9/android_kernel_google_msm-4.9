#!/bin/bash
# Long-running log capture for a booted 24.0 phone, to catch Wi-Fi/Bluetooth
# trouble that shows up after 10-15 minutes. Streams logcat (all buffers) and
# the kernel log to the host, so nothing is lost to on-device ring buffers,
# and every 30 s records Wi-Fi link, ping times and Bluetooth state.
# Survives adb drops and reboots: each stream reconnects.
# Usage: capture-24.sh [label]    Stop: kill the process group (pid in $OUT/pid).
LABEL=${1:-cap-$(date +%m%d-%H%M)}
OUT=~/sargo-bpf-test/logs/captures/$LABEL; mkdir -p $OUT
echo $$ > $OUT/pid
log() { echo "$(date '+%F %T') $*" | tee -a $OUT/run.log; }
usb() { lsusb | grep -o '18d1:[0-9a-f]*' | head -1; }

# Wait for boot (report a bootloader landing; do not touch slots here).
seen=""
until [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ]; do
  u=$(usb); [ "$u" != "$seen" ] && { log "usb: ${u:-none}"; seen=$u; }
  sleep 2
done
log "boot_completed: $(timeout 10 adb shell 'getprop ro.boot.slot_suffix; getprop ro.lineage.version; getenforce' | tr -d '\r' | tr '\n' ' ')"
timeout 30 adb root >/dev/null 2>&1; sleep 3; timeout 30 adb wait-for-device

stream() {   # name, command...: rerun the command whenever adb drops
  local name=$1; shift
  while :; do
    timeout 30 adb wait-for-device
    echo "=== $(date '+%F %T') connect" >> $OUT/$name.txt
    "$@" >> $OUT/$name.txt 2>&1
    sleep 2
  done
}
stream logcat adb logcat -b all -v threadtime &
stream kmsg   adb shell dmesg -w &

probe() {
  adb shell '
    echo "--- $(date "+%F %T")"
    iw dev wlan0 link 2>&1 | grep -E "Connected|Not connected|signal|tx bitrate"
    gw=$(ip -4 route show table all | awk "/default/{print \$3; exit}")
    echo "gw=$gw"; [ -n "$gw" ] && ping -c 5 -i 0.2 -W 2 $gw | tail -2
    ping -c 3 -W 3 8.8.8.8 | tail -2
    getprop init.svc.vendor.wifi_hal_legacy; getprop init.svc.wpa_supplicant
    dumpsys bluetooth_manager | grep -m3 -E "^  state:|curState=|crashed"
  ' 2>&1
}
while :; do
  timeout 60 probe >> $OUT/probe.txt
  sleep 30
done
