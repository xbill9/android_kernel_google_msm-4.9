#!/bin/bash
# Smoke test of a booted LineageOS 24.0 phone: boot state, crashes, radios, sensors, camera,
# Google apps, and the fixes this port depends on. Prints PASS / FAIL / CHECK (needs a person
# or depends on the setting) per item and saves the report and logs.
# The phone must be unlocked without a PIN for the camera test (it takes one photo).
# Usage: smoke-test-24.sh [report dir]
set -u
for d in ~/android/lineage-24.0/out/host/linux-x86/bin ~/platform-tools; do [ -x $d/adb ] && PATH=$d:$PATH; done
D=${1:-$HOME/sargo-bpf-test/logs/smoke-$(date +%m%d-%H%M)}
mkdir -p "$D"
R=$D/report.txt; : > "$R"
pass=0 fail=0 check=0
res() {  # res PASS|FAIL|CHECK name detail
  printf '%-5s %-26s %s\n' "$1" "$2" "$3" | tee -a "$R"
  case $1 in PASS) pass=$((pass+1));; FAIL) fail=$((fail+1));; *) check=$((check+1));; esac
}
sh() { timeout 30 adb shell "$@" 2>/dev/null | tr -d '\r'; }
prop() { sh getprop "$1"; }

[ "$(adb devices | grep -c 'device$')" = 1 ] || { echo "need exactly one phone (adb devices)"; exit 1; }
timeout 20 adb root >/dev/null 2>&1; sleep 3; timeout 60 adb wait-for-device
echo "phone $(prop ro.serialno), $(prop ro.lineage.version), slot $(prop ro.boot.slot_suffix), up $(sh uptime | sed 's/.*up *//; s/,.*//')" | tee -a "$R"
T0=$(sh date +%s)

# --- System
[ "$(prop sys.boot_completed)" = 1 ] && res PASS boot "boot completed" || res FAIL boot "not completed"
e=$(sh getenforce); [ "$e" = Enforcing ] && res PASS selinux "$e" || res FAIL selinux "$e"
up=$(sh cat /proc/uptime | cut -d. -f1); since=$(sh "date -d @$((T0-up)) '+%Y-%m-%d %H:%M:%S'")  # one string: adb would split the format
crashes=$(sh "dumpsys dropbox --print" | awk -v s="$since" '/^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9:]+ [a-zA-Z_]+ / && ($1" "$2)>=s {print $3}' | grep -E 'crash|anr|tombstone|watchdog' | sort | uniq -c | tr -s ' ' | tr '\n' ';')
[ -z "$crashes" ] && res PASS "crashes since boot" "none" || res FAIL "crashes since boot" "$crashes"
den=$(sh "logcat -d -b all" | grep -a 'avc: *denied' | grep -c 'permissive=0')
res CHECK "selinux denials" "$den since the log began (known: qtelephony, nfc, camera, vendor_init)"
[ "$(prop init.svc.vendor.chre)" = stopped ] && res PASS "chre stopped" "stopped after boot" || res FAIL "chre stopped" "$(prop init.svc.vendor.chre)"
sh pidof com.qualcomm.qti.qtidataservices >/dev/null || sh "ps -A -o NAME" | grep -q qtidataservices && res PASS qtidataservices "running" || res FAIL qtidataservices "not running"
nz=$(sh "logcat -d" | grep -ac 'zygote-child')
[ "$nz" = 0 ] && res PASS "native app zygote" "no aborts" || res FAIL "native app zygote" "$nz aborts"

# --- Radios
wifi=$(sh "dumpsys wifi" | grep -m1 -oE 'Wi-Fi is (enabled|disabled)')
if sh "ping -c 2 -W 3 8.8.8.8" >/dev/null && sh "ping -c 1 -W 3 google.com" >/dev/null; then
  res PASS wifi "$wifi; ping and DNS work ($(sh ip -4 -o addr show wlan0 | awk '{print $4}'))"
else res FAIL wifi "$wifi; no internet"; fi
bt=$(sh settings get global bluetooth_on); [ "$bt" = 1 ] || sh cmd bluetooth_manager enable >/dev/null
for i in $(seq 20); do  # dumpsys prints "State: ON"
  bts=$(sh "dumpsys bluetooth_manager" | grep -m1 -iE '^ +state:' | awk '{print $2}'); [ "$bts" = ON ] && break; sleep 1
done
[ "$bts" = ON ] && res PASS bluetooth "on" || res FAIL bluetooth "state ${bts:-unknown}"
[ "$bt" = 1 ] || sh cmd bluetooth_manager disable >/dev/null
sim=$(prop gsm.sim.state); bb=$(prop gsm.version.baseband)
[ -n "$bb" ] && res CHECK "modem" "baseband $bb, SIM ${sim:-?}; calls and data need a SIM" || res FAIL modem "no baseband"
nfc=$(sh "dumpsys nfc" | grep -m1 -oE 'mState=[a-z_]+'); res CHECK nfc "${nfc:-no state}; tap a tag or pay to test"

# --- Location
loc=$(sh "dumpsys location" | grep -m1 -oE 'last location=Location\[(network|fused|gps) [^ ]+ hAcc=[0-9.]+')
[ -n "$loc" ] && res PASS location "${loc#last location=}" || res FAIL location "no fix in dumpsys location"
sats=$(sh "dumpsys location" | grep -m1 -oE 'Number of TTFF reports: [0-9]+')
res CHECK "satellite gps" "${sats:-?}; needs open sky"

# --- Sensors (the screen on starts the light sensor and accelerometer)
sh input keyevent KEYCODE_WAKEUP; sh wm dismiss-keyguard; sleep 4
ss=$(sh "dumpsys sensorservice")
for s in "BMI160 Accelerometer" "TMD2725 Ambient Light" "BMP380 Barometer"; do
  n=$(echo "$ss" | grep -A60 "^$s: last" | grep -oE 'ts=[0-9.]+' | cut -d= -f2 | sort -n | tail -1 | cut -d. -f1)
  [ -n "$n" ] && [ $((up - n)) -lt 60 ] 2>/dev/null && res PASS "sensor ${s%% *} ${s##* }" "event $((up - n)) s ago" || res FAIL "sensor ${s%% *} ${s##* }" "no event in the last minute"
done
ns=$(echo "$ss" | grep -cE '^0x[0-9a-f]+\) .*\| ')
res CHECK "sensor list" "$ns sensors registered"

# --- Camera: take one photo with the default camera
before=$(sh "ls /sdcard/DCIM/Camera 2>/dev/null | wc -l")
ap=org.lineageos.aperture  # enable Aperture for the photo if the user disabled it
off=$(sh "pm list packages -d" | grep -c "^package:$ap$"); [ "$off" = 1 ] && sh pm enable $ap >/dev/null
sh am start -W -a android.media.action.STILL_IMAGE_CAMERA >/dev/null
for i in $(seq 15); do sh "dumpsys activity activities" | grep -m1 topResumedActivity | grep -q $ap && break; sleep 1; done
if sh "dumpsys activity activities" | grep -m1 topResumedActivity | grep -q $ap; then
  sleep 4; sh input keyevent KEYCODE_CAMERA
  for i in $(seq 10); do after=$(sh "ls /sdcard/DCIM/Camera 2>/dev/null | wc -l"); [ "${after:-0}" -gt "${before:-0}" ] && break; sleep 1; done
  [ "${after:-0}" -gt "${before:-0}" ] && res PASS camera "photo saved ($(sh "ls -t /sdcard/DCIM/Camera | head -1"))" || res FAIL camera "camera open, but no new photo in DCIM/Camera"
else res FAIL camera "Aperture did not come to the front within 15 s (phone locked?)"; fi
sh input keyevent KEYCODE_HOME; sh am force-stop $ap; [ "$off" = 1 ] && sh pm disable-user $ap >/dev/null
res CHECK "camera front" "$(sh "dumpsys media.camera" | grep -m1 -oE 'Number of camera devices: [0-9]+'); switch cameras by hand"

# --- Other hardware
sh cmd vibrator_manager synced oneshot 200 >/dev/null && res PASS vibrator "200 ms buzz" || res FAIL vibrator "command failed"
fp=$(sh "dumpsys fingerprint" | grep -m1 -oE '"count":[0-9]+'); res CHECK fingerprint "HAL up, ${fp:-no sensor}; enrol one to test"
ao=$(sh "dumpsys media.audio_flinger" | grep -c 'Output thread'); res CHECK audio "$ao output threads; listen to speaker, earpiece, jack, mic"
bat=$(sh "dumpsys battery" | grep -E 'level|status:|temperature' | tr -s ' ' | tr '\n' ' ')
res PASS battery "$bat"
for s in vendor.citadeld vendor.keymaster-4-1-citadel vendor.weaver_hal gatekeeperd; do
  [ "$(prop init.svc.$s)" = running ] || { res FAIL "titan m" "$s not running"; s=; break; }
done; [ -n "$s" ] && res PASS "titan m" "citadeld, keymaster, weaver, gatekeeper running"

# --- Google apps and apps
if sh pm path com.google.android.gms >/dev/null; then
  ng=$(sh "dumpsys package com.google.android.gms" | grep -cE '^ +android.permission\.[A-Z_]+: granted=false')
  sh pidof com.google.android.gms >/dev/null && res PASS "play services" "running, $ng runtime permissions ungranted" || res FAIL "play services" "not running"
else res CHECK "play services" "not installed (sideload GApps)"; fi
sh am force-stop com.android.chrome; sh "logcat -c"
sh am start -a android.intent.action.VIEW -d https://example.com com.android.chrome >/dev/null; sleep 20
nr=$(sh "ps -A -o USER,NAME" | grep -cE '^u0_i[0-9]+ +com.android.chrome:sandboxed')
[ "$nr" -gt 0 ] && res PASS chrome "$nr sandboxed renderer(s)" || res FAIL chrome "no renderer"
sh input keyevent KEYCODE_HOME
nv=$(sh "pm art dump" | grep -cE 'arm64: \[status=verify\]'); np=$(sh "pm art dump" | grep -cE 'arm64: \[status=speed-profile\]')
[ "$np" -gt 20 ] && res PASS "apps compiled" "$np speed-profile, $nv verify" || res CHECK "apps compiled" "$np speed-profile; run post-install-24.sh"

# --- Logs
sh "logcat -d -b all -v threadtime" > "$D/logcat.txt"; sh dmesg > "$D/dmesg.txt"
echo | tee -a "$R"; echo "$pass pass, $fail fail, $check check. Report and logs: $D" | tee -a "$R"
[ $fail = 0 ]
