#!/bin/bash
# Check the hardware of a sargo running LineageOS 24.0, over adb. Each check prints
# PASS, FAIL or SKIP with the evidence; the counts at the end are computed here.
# Needs a booted userdebug build (adb root). Takes a photo with each camera, plays a ringtone
# for a few seconds and vibrates once. Restores the settings it changes, and disables again
# Aperture and Twelve if they were disabled.
# Usage: hw-check-24.sh [serial]   Output: ~/sargo-logs/<serial>-hwcheck-<time>/
set -u
S=${1:-$(adb devices | awk '$2=="device"{print $1}' | head -1)}
[ -n "$S" ] || { echo "no phone in adb devices"; exit 1; }
A="adb -s $S"
D=~/sargo-logs/$S-hwcheck-$(date +%Y%m%d-%H%M); mkdir -p "$D"
R=$D/results.txt; : > "$R"
sh_() { timeout 30 $A shell "$@" 2>/dev/null | tr -d '\r'; }
res() { printf '%-4s %-14s %s\n' "$1" "$2" "$3" | tee -a "$R"; }
wait_for() { # seconds 'test': true once the test (re-evaluated each second) succeeds
  local i; for i in $(seq "$1"); do eval "$2" && return 0; sleep 1; done; return 1; }

timeout 20 $A root >/dev/null 2>&1; sleep 2; timeout 60 $A wait-for-device
[ "$(sh_ id -u)" = 0 ] || { echo "adb root does not work"; exit 1; }
T0=$(sh_ date +%H:%M:%S)
STAY=$(sh_ settings get global stay_on_while_plugged_in)
ROT=$(sh_ settings get system accelerometer_rotation)
BRI=$(sh_ settings get system screen_brightness_mode)
OFFAPPS=$(sh_ pm list packages -d | sed 's/^package://')
use_app() { # enable an app the user disabled, for one test; restore() disables it again
  case " $(echo $OFFAPPS) " in *" $1 "*) sh_ pm enable "$1" >/dev/null; REENABLED="${REENABLED:-} $1" ;; esac; }
restore() {
  for p in ${REENABLED:-}; do sh_ pm disable-user "$p" >/dev/null; done
  sh_ settings put global stay_on_while_plugged_in "$STAY"
  sh_ settings put system accelerometer_rotation "$ROT"
  sh_ settings put system screen_brightness_mode "$BRI"
}
trap restore EXIT
sh_ settings put global stay_on_while_plugged_in 7
sh_ input keyevent KEYCODE_WAKEUP; sh_ wm dismiss-keyguard
echo "phone $S, $(sh_ getprop ro.lineage.version), started $T0"; echo

# System
v=$(sh_ getprop ro.lineage.version); slot=$(sh_ getprop ro.boot.slot_suffix)
[ "$(sh_ getprop sys.boot_completed)" = 1 ] && res PASS boot "$v on slot $slot, up $(( $(sh_ cut -d. -f1 /proc/uptime) / 60 )) min" || res FAIL boot "sys.boot_completed is not 1"
e=$(sh_ getenforce); [ "$e" = Enforcing ] && res PASS selinux "$e" || res FAIL selinux "$e"

# Wi-Fi: connected, then traffic to the router, an IP and a name
w=$(sh_ cmd wifi status | grep -m1 -o 'Wifi is connected to "[^"]*"')
if [ -n "$w" ]; then
  gw=$(sh_ ip route show table all | awk '/^default .*wlan0/{print $3; exit}')
  info=$(sh_ cmd wifi status | grep -m1 -oE 'RSSI: -?[0-9]+, Link speed: [0-9]+Mbps')
  res PASS wifi-link "${w#Wifi is }, $info"
  for t in ${gw:-} 1.1.1.1 dns.google; do
    o=$(sh_ ping -c 3 -W 2 "$t"); rx=$(echo "$o" | grep -oE '[0-9]+ received' | cut -d' ' -f1)
    avg=$(echo "$o" | awk -F/ '/^rtt/{print $5}')
    [ "${rx:-0}" -gt 0 ] && res PASS wifi-ping "$t: $rx/3 replies, average ${avg} ms" || res FAIL wifi-ping "$t: no replies"
  done
else
  res FAIL wifi-link "not connected: $(sh_ cmd wifi status | head -1)"
fi

# Bluetooth: switch to the other state and back, timing each. BLE_ON counts as off: it is
# where the adapter stays when "Bluetooth scanning" (ble_scan_always_enabled) is on.
bt() { sh_ dumpsys bluetooth_manager | grep -m1 -iE '^ +state:' | awk '{print ($2=="BLE_ON") ? "OFF" : $2}'; }
bt_to() { # ON|OFF: echo the seconds it took, or fail after 20 s
  local t=$(date +%s); sh_ cmd bluetooth_manager $([ $1 = ON ] && echo enable || echo disable) >/dev/null
  wait_for 20 "[ \"\$(bt)\" = $1 ]" && echo $(( $(date +%s) - t )); }
s0=$(bt); [ "$s0" = ON ] && s1=OFF || s1=ON
if a=$(bt_to $s1); then
  b=$(bt_to $s0) && res PASS bluetooth "was $s0; $s1 in ${a}s, back $s0 in ${b}s" \
    || res FAIL bluetooth "did not return to $s0 within 20 s (state $(bt))"
else res FAIL bluetooth "did not turn $s1 within 20 s (state $(bt))"; bt_to $s0 >/dev/null; fi

# NFC
n=$(sh_ dumpsys nfc | grep -m1 -o 'mState=[a-z_]*')
[ "$n" = mState=on ] && res PASS nfc "$n" || res FAIL nfc "${n:-no nfc service}"

# Modem and SIM
bb=$(sh_ getprop gsm.version.baseband); sim=$(sh_ getprop gsm.sim.state)
[ -n "$bb" ] && res PASS modem "baseband $bb, $(sh_ getprop gsm.version.ril-impl)" || res FAIL modem "no baseband version (modem not up)"
case "$sim" in *READY*|*LOADED*) res PASS sim "$sim, network $(sh_ getprop gsm.operator.alpha) $(sh_ getprop gsm.network.type)" ;;
  *) res SKIP sim "SIM state $sim: calls and mobile data not tested" ;; esac

# GNSS: the HAL is registered; a fix needs open sky and is not attempted
g=$(sh_ lshal | grep -m1 -oE 'android.hardware.gnss@[0-9.]+::IGnss/default')
[ -n "$g" ] && res PASS gnss-hal "$g; location $(sh_ cmd location is-location-enabled); fix not tested" || res FAIL gnss-hal "no IGnss HAL"

# Sensors: which ones report while the phone uses them (auto-rotate, auto-brightness)
sh_ settings put system accelerometer_rotation 1; sh_ settings put system screen_brightness_mode 1
sleep 8
sh_ dumpsys sensorservice > "$D/sensorservice.txt"
total=$(grep -m1 -oE 'Total [0-9]+ h/w sensors' "$D/sensorservice.txt")
recent=$(awk '/^Recent Sensor events:/{f=1;next} f&&/^[A-Za-z].*: last [0-9]+ events/{sub(/: last.*/,"");print}' "$D/sensorservice.txt" | sort -u)
fresh=$(awk -v t0="$T0" '/^Recent Sensor events:/{f=1;next}
  f&&/^[A-Za-z].*: last [0-9]+ events/{n=$0;sub(/: last.*/,"",n);next}
  f&&match($0,/wall=[0-9:]+/){w=substr($0,RSTART+5,8); if(w>=t0) ok[n]=1}
  END{for(k in ok)print k}' "$D/sensorservice.txt" | sort)
nr=$(printf '%s\n' "$recent" | grep -c .); nf=$(printf '%s\n' "$fresh" | grep -c .)
[ "$nf" -gt 0 ] && res PASS sensors "$total; $nf reported during this check: $(printf '%s' "$fresh" | paste -sd, | sed 's/,/, /g')" \
  || res FAIL sensors "$total; none reported during this check"
res INFO sensors "$nr have reported since boot; the others had no user in this check"

# Camera: open Aperture, take a photo with the rear camera, flip to the front one, take another
cam=$(sh_ dumpsys media.camera | grep -m1 -oE 'Number of camera devices: [0-9]+')
use_app org.lineageos.aperture
for p in CAMERA RECORD_AUDIO ACCESS_FINE_LOCATION; do sh_ pm grant org.lineageos.aperture android.permission.$p; done
photos() { sh_ "ls /sdcard/DCIM/Camera 2>/dev/null | grep -c ."; }
sh_ am start -W -a android.media.action.STILL_IMAGE_CAMERA >/dev/null
# Aperture reopens the camera it used last: test that one first, then flip to the other
wait_for 15 "sh_ dumpsys media.camera | grep -q 'Device [01] is open'"
first=$(sh_ dumpsys media.camera | grep -m1 -oE 'Device [01] is open' | cut -d' ' -f2)
for dev in ${first:-0} $((1 - ${first:-0})); do
  if [ $dev != "${first:-0}" ]; then # tap Aperture's flip button, found by its resource id
    sh_ uiautomator dump /sdcard/hwcheck-ui.xml >/dev/null
    xy=$(sh_ cat /sdcard/hwcheck-ui.xml | grep -oE 'flipCameraButton"[^>]*bounds="\[[0-9]+,[0-9]+\]\[[0-9]+,[0-9]+\]' \
      | grep -oE '[0-9]+' | tail -4 | paste -sd' ' | awk '{print int(($1+$3)/2), int(($2+$4)/2)}')
    sh_ rm -f /sdcard/hwcheck-ui.xml
    [ -n "$xy" ] && sh_ input tap $xy || { res FAIL camera-$dev "Aperture's flip button not found"; continue; }
  fi
  if wait_for 15 "sh_ dumpsys media.camera | grep -q 'Device $dev is open'"; then
    before=$(photos); sleep 3; sh_ input keyevent KEYCODE_CAMERA
    if wait_for 10 '[ "$(photos)" -gt "$before" ]'; then
      f=$(sh_ "ls -t /sdcard/DCIM/Camera | head -1"); sz=$(sh_ stat -c %s "/sdcard/DCIM/Camera/$f")
      res PASS camera-$dev "device $dev ($([ $dev = 0 ] && echo rear || echo front)) open, photo $f ($sz bytes)"
    else res FAIL camera-$dev "device $dev open, but no new photo in DCIM/Camera after the shutter key"; fi
  else res FAIL camera-$dev "device $dev did not open ($cam; $(sh_ dumpsys media.camera | grep -m1 -oE 'Device [01] is open'))"; fi
done
sh_ am force-stop org.lineageos.aperture
$A pull /sdcard/DCIM/Camera "$D/photos" >/dev/null 2>&1

# Speaker: play a ringtone in Twelve and count the frames the speaker outputs take in 4 s
use_app org.lineageos.twelve
fw() { sh_ dumpsys media.audio_flinger | grep -E '^  Frames written' | awk '{s+=$3} END{print s+0}'; }
rt=$(sh_ "ls /product/media/audio/ringtones/*.ogg | head -1")
sh_ am start -W -a android.intent.action.VIEW -d "file://$rt" -t audio/ogg -n org.lineageos.twelve/.ViewActivity >/dev/null
sleep 1; f0=$(fw); sleep 4; df=$(( $(fw) - f0 ))
spk=$(sh_ dumpsys media.audio_flinger | grep -c 'Output devices: 0x2 (AUDIO_DEVICE_OUT_SPEAKER)')
[ "$df" -gt 48000 ] && res PASS speaker "$(basename "$rt"): $((df / 4)) frames/s to $spk speaker outputs; listen to confirm" \
  || res FAIL speaker "$(basename "$rt"): $df frames in 4 s to the speaker outputs"
sh_ am force-stop org.lineageos.twelve
res SKIP microphone "needs a person to speak; not tested"

# Vibrator
tv=$(sh_ date +%H:%M:%S.000); sh_ cmd vibrator_manager synced -f oneshot 300 >/dev/null; sleep 1
vib=$(sh_ dumpsys vibrator_manager | awk -v t="$tv" '/Recent vibrations:/{f=1;next} f&&/start: /{
  match($0,/start: [0-9:.]+/); st=substr($0,RSTART+7,RLENGTH-7); if(st>=t) l=$0} END{print l}')
case "$vib" in *finished*) res PASS vibrator "300 ms request: $(echo "$vib" | grep -oE 'finished \| duration: +[0-9]+ms' | tr -s ' ')" ;;
  *) res FAIL vibrator "no finished vibration recorded after $tv: ${vib:-none}" ;; esac

# Fingerprint: the HAL is up; enrolment needs a finger
fp=$(sh_ dumpsys fingerprint | grep -m1 -oE 'FingerprintProvider[^"]*')
[ -n "$fp" ] && res PASS fingerprint "${fp//\\/}; enrolment and unlock not tested" || res FAIL fingerprint "no fingerprint provider"

# Display: screen off and on again
st() { sh_ dumpsys display | grep -m1 -oE 'mScreenState=[A-Z]+' | cut -d= -f2; }
sh_ input keyevent KEYCODE_SLEEP
if wait_for 10 '[ "$(st)" = OFF ]'; then sh_ input keyevent KEYCODE_WAKEUP
  wait_for 10 '[ "$(st)" = ON ]' && res PASS display "screen OFF and back ON; $(sh_ dumpsys SurfaceFlinger | grep -m1 -oE 'peak-refresh-rate *: *[0-9.]+ Hz' | tr -s ' ')" \
    || res FAIL display "screen did not come back ON (state $(st))"
  sh_ wm dismiss-keyguard
else res FAIL display "screen did not turn OFF (state $(st))"; fi

# Battery and charging
bat() { sh_ cat /sys/class/power_supply/battery/$1; }
lvl=$(bat capacity); stat=$(bat status); tmp=$(bat temp); mv=$(( $(bat voltage_now) / 1000 )); usb=$(sh_ cat /sys/class/power_supply/usb/online)
if [ "$usb" = 1 ] && [ "$tmp" -gt 0 ] && [ "$tmp" -lt 450 ]; then
  res PASS battery "$lvl%, $stat, $((tmp/10)).$((tmp%10)) C, $mv mV, USB online ($(sh_ cat /sys/class/power_supply/usb/type))"
else res FAIL battery "$lvl%, $stat, temp $tmp (tenths C), $mv mV, USB online $usb"; fi

# Storage: write and read back 256 MB on /data
f=/data/local/tmp/hwcheck.bin
wt=$(sh_ "dd if=/dev/zero of=$f bs=1048576 count=256 conv=fsync 2>&1 | tail -1")
sh_ "echo 3 > /proc/sys/vm/drop_caches"
rt2=$(sh_ "dd if=$f of=/dev/null bs=1048576 2>&1 | tail -1"); sh_ rm -f $f
case "$wt$rt2" in *bytes*bytes*) res PASS storage "write: ${wt##*, }  read: ${rt2##*, }" ;; *) res FAIL storage "write: $wt read: $rt2" ;; esac

# Logs since boot: crashes and SELinux denials
$A logcat -b all -d > "$D/logcat-all.txt" 2>&1; sh_ dmesg > "$D/dmesg.txt"
fe=$(grep -c 'FATAL EXCEPTION' "$D/logcat-all.txt"); fs=$(grep -c 'Fatal signal' "$D/logcat-all.txt"); an=$(grep -c 'ANR in' "$D/logcat-all.txt")
up=$(( $(sh_ cut -d. -f1 /proc/uptime) / 60 + 1 ))
tb=$(sh_ "find /data/tombstones -name 'tombstone_[0-9]*' ! -name '*.pb' -mmin -$up | grep -c ."); tball=$(sh_ "ls /data/tombstones | grep -v pb | grep -c tombstone_")
[ $((fe + fs + an + tb)) = 0 ] && res PASS crashes "none since boot: logcat (app, native, ANR) and tombstones ($tball older ones kept)" \
  || res FAIL crashes "since boot: $fe app, $fs native, $an ANR in logcat; $tb new tombstones"
dn=$(cat "$D/logcat-all.txt" "$D/dmesg.txt" | grep 'avc:  denied' | sed -E 's/.*\{ ([^}]+)\}.*scontext=u:r:([a-z_0-9]+).*tcontext=u:[a-z_]+:([a-z_0-9]+).*tclass=([a-z_]+).*/\2 -> \3 \4 {\1}/' | sort | uniq -c | sort -rn)
printf '%s\n' "$dn" > "$D/denials.txt"
res INFO denials "$(printf '%s\n' "$dn" | awk '{s+=$1} END{print s+0}') SELinux denials in $(printf '%s\n' "$dn" | grep -c .) kinds (denials.txt)"

echo
for k in PASS FAIL SKIP; do printf '%s %s  ' "$(grep -c "^$k " "$R")" "$k"; done; echo
echo "results and logs: $D"
