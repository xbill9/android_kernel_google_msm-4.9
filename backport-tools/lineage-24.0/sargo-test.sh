#!/bin/bash
# Test this kernel tree on the plugged-in Pixel 3a without flashing anything:
# build HEAD (kbuild.sh), put it in the phone's own boot image, RAM-boot it (fastboot boot),
# run every check, reboot to the installed kernel, print one report.
# A panic (kernel.panic=5) or a hang reboots the installed kernel; nothing on the phone changes.
# Installed as ~/bin/sargo-test. Usage:
#   sargo-test                 build, then test (about 35 minutes)
#   sargo-test --push          build, test, and on PASS commit results.tsv and push main
#   sargo-test --no-build      test the last build
#   CTS_MODS="CtsNetTestCases CtsBluetoothTestCases" sargo-test   CTS modules to run (default: net)
#   CTS_MODS= sargo-test       no CTS
# Result: ~/sargo-logs/<serial>-test-<kernel>-<time>/summary.txt, last line PASS or FAIL, and one
# line per run in boot-test/results.tsv (commit it with the change it tested).
# The kernel source must be committed: the result is recorded against the commit.
# FAIL means: a unit test failed, hw-check or VTS net has a failure, or the kernel logged an
# oops/BUG/WARNING. CTS failures are listed, not judged (6 CtsNetTestCases failures are known,
# see boot-test/README.md).
set -u
L24=$(dirname "$(readlink -f "$0")")
BT=$L24/boot-test
KOUT=${KOUT:-/android/sargo-bpf-test/kout}
CTS_MODS=${CTS_MODS-CtsNetTestCases}
UNIT="qmaptest xdptest ringbuftest rawtptest clone3test clone3test32 atomictest udp0test"

kern() { timeout 20 adb shell uname -r 2>/dev/null | tr -d '\r'; }
wake() { adb shell 'svc power stayon usb; input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard' >/dev/null 2>&1; }
booted() { for i in $(seq 1 "$1"); do [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && return 0; sleep 3; done; return 1; }
bad=""
fail() { bad="$bad $1"; }
# One of sargo-build, sargo-test, sargo-install at a time: they share the tree and the phone.
. "$L24/sargo-lock.sh"; sargo_lock sargo-test
SRC=$(cd "$L24/../.." && pwd)
PUSH=; [ "${1:-}" = --push ] && PUSH=1
if [ "${1:-}" != --no-build ] && [ -n "$(git -C "$SRC" status --porcelain -- ':(top)' ':(top,exclude)backport-tools' ':(top,exclude,glob)*.md')" ]; then
  echo "uncommitted kernel changes; commit them first:"; git -C "$SRC" status --short -- . ':!backport-tools' | head; exit 1
fi

[ "$(adb devices | grep -c 'device$')" = 1 ] || { echo "need exactly one phone (adb devices)"; exit 1; }
SERIAL=$(adb get-serialno)
INSTALLED=$(kern)

if [ "${1:-}" != --no-build ]; then
  echo "building $(git -C "$L24" log -1 --format='%h %s' | cut -c1-70)"
  bash "$L24/kbuild.sh" > "$KOUT.log" 2>&1 || { tail -20 "$KOUT.log"; echo "FAIL: kernel build (log: $KOUT.log)"; exit 1; }
fi
WANT=$(cat "$KOUT/include/config/kernel.release")
TOP=$HOME/sargo-logs/$SERIAL-test-${WANT#4.9.337-}-$(date +%Y%m%d-%H%M)
mkdir -p "$TOP"; S=$TOP/summary.txt
say() { echo "$(date '+%F %T') $*" | tee -a "$S"; }
say "phone $SERIAL on $INSTALLED ($(adb shell getprop ro.lineage.version | tr -d '\r')); testing $WANT"

# The phone's own boot image (current slot), so the ramdisk matches the installed system.
timeout 30 adb root >/dev/null; sleep 3; timeout 30 adb wait-for-device
SLOT=$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')
adb shell "dd if=/dev/block/by-name/boot$SLOT of=/data/local/tmp/boot-base.img bs=4m" >/dev/null 2>&1
adb pull /data/local/tmp/boot-base.img "$TOP/boot-base.img" >/dev/null && adb shell rm /data/local/tmp/boot-base.img
NOLOG=1 KERNEL=$KOUT/arch/arm64/boot/Image.lz4 bash "$BT/add-logger.sh" "$TOP/boot-base.img" "$TOP/boot-test.img" \
  > "$TOP/repack.log" 2>&1 || { say "FAIL: repack (see repack.log)"; exit 1; }
rm -f "$TOP/boot-base.img"

# A tradefed left running reboots a phone it finds in fastboot.
for p in $(pgrep -f com.android.tradefed); do kill -9 "$p" 2>/dev/null; done

# From here the phone may be on the test kernel: on an interrupt, put it back.
back() { say "interrupted: rebooting to the installed kernel"; timeout 20 fastboot reboot >/dev/null 2>&1; timeout 30 adb reboot >/dev/null 2>&1; exit 1; }
trap back INT TERM HUP

say "== RAM boot"
timeout 30 adb reboot bootloader
for i in $(seq 1 90); do lsusb | grep -q 18d1:4ee0 && break; sleep 1; done
lsusb | grep -q 18d1:4ee0 || { say "FAIL: phone not in the bootloader after 90 s"; timeout 30 adb reboot >/dev/null 2>&1; exit 1; }
if ! timeout 90 fastboot boot "$TOP/boot-test.img" > "$TOP/fastboot.log" 2>&1; then
  say "FAIL: fastboot boot: $(tail -1 "$TOP/fastboot.log")"; timeout 20 fastboot reboot >/dev/null 2>&1; booted 150; exit 1
fi
if ! booted 150; then
  say "NOT BOOTED after 450 s"
  timeout 20 fastboot reboot >/dev/null 2>&1
  booted 150 && say "back on $(kern)" || say "phone not back"
  say "FAIL: no boot"; exit 1
fi
K=$(kern); say "booted: $K"
if [ "$K" != "$WANT" ]; then
  say "wrong kernel: panic reboot? (boot logs: run boot-test/test-boot-a24.sh)"; fail boot
else
  timeout 30 adb root >/dev/null; sleep 3; timeout 30 adb wait-for-device; wake
  adb shell 'logcat -b all -d' > "$TOP/boot-logcat.txt"; adb shell dmesg > "$TOP/boot-dmesg.txt"
  say "bpfloader errors: $(grep -E 'BpfLoader|bpfloader|NetBpfLoad' "$TOP/boot-logcat.txt" | grep -E ' E ' | grep -oE '/system[^ :]*\.(bpf|o)' | sort -u | tr '\n' ' ')"
  say "wlan module: $(adb shell 'lsmod | grep -c ^wlan' | tr -d '\r') loaded; symbol errors: $(grep -cE 'disagrees about version|Unknown symbol' "$TOP/boot-dmesg.txt")"
  for t in $UNIT; do
    adb push "$BT/$t" /data/local/tmp/ >/dev/null
    adb shell "/data/local/tmp/$t" > "$TOP/$t.txt" 2>&1; rc=$?
    say "$t exit $rc: $(tail -1 "$TOP/$t.txt" | tr -d '\r')"; [ $rc = 0 ] || fail "$t"
  done
  wake; bash "$BT/smoke-test-24.sh" "$TOP/smoke" > "$TOP/smoke.log" 2>&1; say "smoke: $(tail -1 "$TOP/smoke.log")"
  wake; bash "$BT/hw-check-24.sh" > "$TOP/hwcheck.log" 2>&1
  h=$(grep -E 'PASS +[0-9]+ +FAIL' "$TOP/hwcheck.log" | tail -1); say "hw-check: $h"
  # The count line reads "21 PASS  0 FAIL  1 SKIP".
  echo "$h" | grep -qE '(^| )0 FAIL' || fail hw-check
  if [ "$(kern)" = "$WANT" ]; then
    wake; bash "$BT/vts-kernel-24.sh" vts_kernel_net_tests "$TOP/vts-net" > /dev/null 2>&1
    v=$(grep -E '^(PASS|FAIL|SKIP) ' "$TOP/vts-net/report.txt" | head -1); say "VTS net: $v"
    case $v in PASS*) ;; *) fail vts-net ;; esac
  fi
  for m in $CTS_MODS; do
    [ "$(kern)" = "$WANT" ] || break
    wake; CTS_NOREBOOT=1 CTS_DIR="$TOP/cts-$m" bash "$BT/cts-24.sh" "$m" > /dev/null 2>&1
    say "CTS: $(grep -E '^(PASS|FAIL|SKIP) ' "$TOP/cts-$m/report.txt" | head -1)"
    grep -E 'FAILED' "$TOP/cts-$m/report.txt" | head -10 >> "$S"
  done
  [ "$(kern)" = "$WANT" ] || { say "kernel changed to $(kern) during the tests (panic?)"; fail panic; }
  adb shell dmesg > "$TOP/dmesg-end.txt"
  # Known and not counted: the ion cache-sync warning at boot (since 2026-11-09) and kswapd
  # page allocation failures when the camera starts (zram, GFP_NOIO; since 2026-10-28).
  grep -hE 'Oops|BUG:|WARNING:' "$TOP/boot-dmesg.txt" "$TOP/dmesg-end.txt" | sort -u \
    | grep -vE 'ion\.c:1215|page allocation failure' > "$TOP/dmesg-oops.txt"
  n=$(wc -l < "$TOP/dmesg-oops.txt"); say "kernel oops/BUG/WARNING lines: $n (dmesg-oops.txt)"; [ "$n" = 0 ] || fail dmesg
fi

say "== back to the installed kernel"
timeout 30 adb reboot; sleep 20
trap - INT TERM HUP
booted 150 && say "booted: $(kern)" || { say "phone not back after reboot"; fail return; }
[ "$(kern)" = "$INSTALLED" ] || { say "expected $INSTALLED after the reboot"; fail return; }
wake
verdict=PASS; [ -z "$bad" ] || verdict="FAIL:$bad"
# results.tsv: date, kernel commit (the last commit that changed kernel source, so later
# commits to backport-tools keep the result), verdict, phone build, report directory.
R=$BT/results.tsv
[ -f "$R" ] || printf 'date\tcommit\tverdict\tinstalled build\treport\n' > "$R"
printf '%s\t%s\t%s\t%s\t%s\n' "$(date '+%F %H:%M')" "$(git -C "$SRC" log -1 --format=%h --abbrev=12 -- ':(top)' ':(top,exclude)backport-tools' ':(top,exclude,glob)*.md')" "$verdict" \
  "$(adb shell getprop ro.lineage.version | tr -d '\r')" "${TOP#$HOME/}" >> "$R"
say "$verdict: $WANT (report: $S)"
if [ -n "$PUSH" ] && [ "$verdict" = PASS ]; then
  git -C "$SRC" add "$R" && git -C "$SRC" commit -q -m "boot-test: sargo-test PASS on $(cut -f2 <<< "$(tail -1 "$R")")" -- "$R" \
    && git -C "$SRC" push -q origin main && say "pushed: $(git -C "$SRC" log -1 --format='%h %s')" \
    || say "push failed: commit and push by hand"
fi
[ "$verdict" = PASS ]
