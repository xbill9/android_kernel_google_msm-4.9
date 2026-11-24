#!/bin/bash
# Test this kernel on the Pixel 3a phones in phones.conf without flashing anything: build HEAD
# (kbuild.sh), put it in each phone's own boot image, RAM-boot it (fastboot boot), run that
# phone's share of the checks, reboot it to its installed kernel, print one report.
# The phones run at the same time; a phone on another machine is reached over SSH (adb through
# a tunnel, fastboot on that machine). A panic (kernel.panic=5) or a hang reboots the installed
# kernel; nothing on a phone changes.
# Run as `sargo test` (sargo.sh); `sargo update` runs it on the ROM's own kernel. Usage:
#   sargo test                 build, then test (about 25 minutes with three or four phones)
#   sargo test --push          build, test, and on PASS commit results.tsv and push main
#   sargo test --no-build      test the last build (KOUT=<kernel build dir> to pick another)
#   sargo test --local         every check on this machine's phone only, ignoring phones.conf
#   CTS_MODS="CtsNetTestCases CtsBluetoothTestCases" sargo test   CTS modules (default: net)
# Result: ~/sargo-logs/test-<kernel>-<time>/summary.txt (one directory per phone beside it),
# last line PASS or FAIL, and one line per run in boot-test/results.tsv.
# FAIL means: a unit test failed, hw-check or VTS net has a failure, a phone did not boot or
# return, or the kernel logged an oops/BUG/WARNING. CTS failures are listed, not judged (6
# CtsNetTestCases failures are known, see boot-test/README.md).
set -u
L24=$(dirname "$(readlink -f "$0")")
BT=$L24/boot-test
SRC=$(cd "$L24/../.." && pwd)
KOUT=${KOUT:-/android/sargo-bpf-test/kout}
export KOUT
CTS_MODS=${CTS_MODS-CtsNetTestCases}
UNIT="qmaptest xdptest ringbuftest rawtptest clone3test clone3test32 atomictest udp0test fsveritytest"
TREE=${LINEAGE_TREE:-$HOME/android/lineage-24.0}
PUSH=; BUILD=1; CHILD=; HOST=local; ONLY=unit,smoke,hw,vts,cts; LOCAL=
for a in "$@"; do case $a in
  --push) PUSH=1 ;; --no-build) BUILD= ;; --child) CHILD=1 ;; --local) LOCAL=1 ;;
  --host=*) HOST=${a#*=} ;; --only=*) ONLY=${a#*=} ;;
  *) sed -n '2,18p' "$0"; exit 1 ;;
esac; done
has() { case ",$ONLY," in *",$1,"*) return 0 ;; esac; return 1; }
WANT=; [ -f "$KOUT/include/config/kernel.release" ] && WANT=$(cat "$KOUT/include/config/kernel.release")

if [ -n "$CHILD" ]; then
  # ---- one phone: $HOST (local, or an SSH host with one phone), checks $ONLY, report in $TOP
  TOP=$SARGO_TOP; mkdir -p "$TOP"; S=$TOP/summary.txt
  if [ "$HOST" = local ]; then
    fb() { timeout 90 fastboot "$@"; }
    inbl() { lsusb | grep -q 18d1:4ee0; }
  else
    RP='PATH=$PATH:$HOME/android/lineage-24.0/out/host/linux-x86/bin:$HOME/platform-tools'
    rsh() { ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "$@"; }
    fb() { timeout 90 ssh -o BatchMode=yes "$HOST" "$RP; fastboot $*"; }
    inbl() { rsh lsusb | grep -q 18d1:4ee0; }
    # Exit 3: the machine is off or asleep (its phone is then skipped).
    rsh true 2>/dev/null || { echo "OFFLINE: $HOST does not answer on SSH"; exit 3; }
    rsh "$RP; adb start-server" >/dev/null 2>&1
    # adb and tradefed on this machine reach the phone through the other machine's adb server.
    PORT=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
    ssh -o BatchMode=yes -o ExitOnForwardFailure=yes -o ServerAliveInterval=15 -N -L "$PORT:localhost:5037" "$HOST" & TUN=$!
    trap 'kill $TUN 2>/dev/null' EXIT
    # No adb call before the tunnel listens: adb would start a server of its own on the port,
    # and that server takes this machine's phone.
    up() { python3 -c 'import socket, sys; socket.create_connection(("127.0.0.1", int(sys.argv[1])), 1)' "$PORT" 2>/dev/null; }
    for i in $(seq 1 30); do up && break; sleep 1; done
    up || { echo "FAIL: no SSH tunnel to $HOST"; exit 1; }
    export ANDROID_ADB_SERVER_PORT=$PORT
    WANT_SERIAL=$(rsh "$RP; adb get-serialno" 2>/dev/null | tr -d '\r')
    [ -n "$WANT_SERIAL" ] && [ "$(adb get-serialno 2>/dev/null)" = "$WANT_SERIAL" ] \
      || { echo "FAIL: the tunnel to $HOST does not reach its phone ($WANT_SERIAL)"; exit 1; }
  fi
  say() { echo "$(date '+%F %T') [$PHONE] $*" | tee -a "$S"; }
  kern() { timeout 20 adb shell uname -r 2>/dev/null | tr -d '\r'; }
  wake() { adb shell 'svc power stayon usb; input keyevent KEYCODE_WAKEUP; wm dismiss-keyguard' >/dev/null 2>&1; }
  booted() { for i in $(seq 1 "$1"); do [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && return 0; sleep 3; done; return 1; }
  bad=""; fail() { bad="$bad $1"; }
  PHONE=$HOST
  [ "$(adb devices | grep -c 'device$')" = 1 ] || { say "FAIL: need exactly one phone on $HOST (adb devices)"; exit 1; }
  PHONE=$(adb get-serialno)
  INSTALLED=$(kern)
  say "on $HOST, $INSTALLED ($(adb shell getprop ro.lineage.version | tr -d '\r')); testing $WANT: $ONLY"

  # The phone's own boot image (current slot), so the ramdisk matches its installed system.
  timeout 30 adb root >/dev/null; sleep 3; timeout 30 adb wait-for-device
  # VTS and CTS install test apps over adb; Google's app check blocks them
  # (INSTALL_FAILED_VERIFICATION_FAILURE) unless USB installs are exempt. The setting persists.
  adb shell settings put global verifier_verify_adb_installs 0
  SLOT=$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')
  # The phone's own boot image; retried, since one adb call can fail while other phones start.
  for i in 1 2 3; do
    adb shell "dd if=/dev/block/by-name/boot$SLOT of=/data/local/tmp/boot-base.img bs=4m" >> "$TOP/dump.log" 2>&1 \
      && adb pull /data/local/tmp/boot-base.img "$TOP/boot-base.img" >> "$TOP/dump.log" 2>&1 && break
    sleep 5
  done
  adb shell rm -f /data/local/tmp/boot-base.img
  [ -s "$TOP/boot-base.img" ] || { say "FAIL: boot image dump (see dump.log)"; exit 1; }
  NOLOG=1 KERNEL=$KOUT/arch/arm64/boot/Image.lz4 bash "$BT/add-logger.sh" "$TOP/boot-base.img" "$TOP/boot-test.img" \
    > "$TOP/repack.log" 2>&1 || { say "FAIL: repack (see repack.log)"; exit 1; }
  rm -f "$TOP/boot-base.img"
  IMG=$TOP/boot-test.img
  if [ "$HOST" != local ]; then
    IMG=/tmp/sargo-boot-test.img
    scp -q -o BatchMode=yes "$TOP/boot-test.img" "$HOST:$IMG" || { say "FAIL: copy to $HOST"; exit 1; }
  fi

  # From here the phone may be on the test kernel: on an interrupt, put it back.
  back() { say "interrupted: rebooting to the installed kernel"; fb reboot >/dev/null 2>&1; timeout 30 adb reboot >/dev/null 2>&1; exit 1; }
  trap back INT TERM HUP

  timeout 30 adb reboot bootloader
  for i in $(seq 1 90); do inbl && break; sleep 1; done
  inbl || { say "FAIL: phone not in the bootloader after 90 s"; timeout 30 adb reboot >/dev/null 2>&1; exit 1; }
  if ! fb boot "$IMG" > "$TOP/fastboot.log" 2>&1; then
    say "FAIL: fastboot boot: $(tail -1 "$TOP/fastboot.log")"; fb reboot >/dev/null 2>&1; booted 150; exit 1
  fi
  if ! booted 150; then
    say "NOT BOOTED after 450 s"; fb reboot >/dev/null 2>&1
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
    if has unit; then
      for t in $UNIT; do
        adb push "$BT/$t" /data/local/tmp/ >/dev/null
        adb shell "/data/local/tmp/$t" > "$TOP/$t.txt" 2>&1; rc=$?
        say "$t exit $rc: $(tail -1 "$TOP/$t.txt" | tr -d '\r')"; [ $rc = 0 ] || fail "$t"
      done
    fi
    if has smoke; then wake; bash "$BT/smoke-test-24.sh" "$TOP/smoke" > "$TOP/smoke.log" 2>&1; say "smoke: $(tail -1 "$TOP/smoke.log")"; fi
    if has hw; then
      wake; bash "$BT/hw-check-24.sh" > "$TOP/hwcheck.log" 2>&1
      h=$(grep -E 'PASS +[0-9]+ +FAIL' "$TOP/hwcheck.log" | tail -1); say "hw-check: $h"
      # The count line reads "21 PASS  0 FAIL  1 SKIP".
      echo "$h" | grep -qE '(^| )0 FAIL' || fail hw-check
    fi
    if has vts && [ "$(kern)" = "$WANT" ]; then
      wake; bash "$BT/vts-kernel-24.sh" vts_kernel_net_tests "$TOP/vts-net" > /dev/null 2>&1
      v=$(grep -E '^(PASS|FAIL|SKIP) ' "$TOP/vts-net/report.txt" | head -1); say "VTS net: $v"
      case $v in PASS*) ;; *) fail vts-net ;; esac
    fi
    if has cts; then
      for m in $CTS_MODS; do
        [ "$(kern)" = "$WANT" ] || break
        wake; CTS_NOREBOOT=1 CTS_DIR="$TOP/cts-$m" bash "$BT/cts-24.sh" "$m" > /dev/null 2>&1
        c=$(grep -E '^(PASS|FAIL|SKIP) ' "$TOP/cts-$m/report.txt" | head -1); say "CTS: $c"
        # Failures are listed, not judged; a module that ran no test at all is a broken run.
        echo "$c" | grep -qE ' [1-9][0-9]* passed' || fail "cts-$m-not-run"
        grep -E 'FAILED' "$TOP/cts-$m/report.txt" | head -10 >> "$S"
      done
    fi
    [ "$(kern)" = "$WANT" ] || { say "kernel changed to $(kern) during the tests (panic?)"; fail panic; }
    adb shell dmesg > "$TOP/dmesg-end.txt"
    # Known and not counted: kswapd page allocation failures when the camera starts (zram,
    # GFP_NOIO; since 2026-10-28).
    grep -hE 'Oops|BUG:|WARNING:' "$TOP/boot-dmesg.txt" "$TOP/dmesg-end.txt" | sort -u \
      | grep -vE 'page allocation failure' > "$TOP/dmesg-oops.txt"
    n=$(wc -l < "$TOP/dmesg-oops.txt"); say "kernel oops/BUG/WARNING lines: $n (dmesg-oops.txt)"; [ "$n" = 0 ] || fail dmesg
  fi

  # Files that the test kernel's fs-verity protected (Android protects packages.xml and a few
  # more) can't be opened by a kernel with the 2018 fs-verity prototype (before 2026-11-24), and
  # Android then deletes packages.xml. Make them plain files again with Android stopped.
  adb push "$BT/fsveritytest" /data/local/tmp/ >/dev/null
  say "$(adb shell 'stop; /data/local/tmp/fsveritytest strip /data/system /data/system_de /data/system_ce /data/misc /data/misc_de /data/misc_ce' 2>&1 | tail -1 | tr -d '\r')"
  timeout 30 adb reboot; sleep 20
  trap - INT TERM HUP
  booted 150 && say "back on $(kern)" || { say "phone not back after reboot"; fail return; }
  [ "$(kern)" = "$INSTALLED" ] || { say "expected $INSTALLED after the reboot"; fail return; }
  wake
  [ "$HOST" = local ] || rsh "rm -f $IMG"
  [ -z "$bad" ] && { say "PASS"; exit 0; }
  say "FAIL:$bad"; exit 1
fi

# ---- the run: build, then every phone in phones.conf at once
# One sargo command at a time: they share the tree and the phones.
. "$L24/sargo-lock.sh"; sargo_lock sargo-test
export SARGO_LOCK_OWNER=$(cut -d' ' -f1 "$SARGO_LOCK")
if [ -n "$BUILD" ] && [ -n "$(git -C "$SRC" status --porcelain -- ':(top)' ':(top,exclude)backport-tools' ':(top,exclude,glob)*.md')" ]; then
  echo "uncommitted kernel changes; commit them first:"; git -C "$SRC" status --short -- . ':!backport-tools' | head; exit 1
fi
if [ -n "$BUILD" ]; then
  echo "building $(git -C "$SRC" log -1 --format='%h %s' | cut -c1-70)"
  bash "$L24/kbuild.sh" > "$KOUT.log" 2>&1 || { tail -20 "$KOUT.log"; echo "FAIL: kernel build (log: $KOUT.log)"; exit 1; }
  WANT=$(cat "$KOUT/include/config/kernel.release")
fi
PH=$(grep -vE '^\s*(#|$)' "$L24/phones.conf" 2>/dev/null || echo "local unit,smoke,hw,vts,cts")
[ -n "$LOCAL" ] && PH="local unit,smoke,hw,vts,cts"
# A line naming this machine is its own phone: phones.conf then reads the same on every machine.
PH=$(echo "$PH" | awk -v me="$(hostname)" '$1 == me { $1 = "local" } { print }')
TOP=$HOME/sargo-logs/test-${WANT#4.9.337-}-$(date +%Y%m%d-%H%M)
mkdir -p "$TOP"; S=$TOP/summary.txt
say() { echo "$(date '+%F %T') $*" | tee -a "$S"; }
say "testing $WANT on $(echo "$PH" | awk '{printf "%s%s (%s)", (NR > 1 ? ", " : ""), $1, $2}')"

# A tradefed left running reboots a phone it finds in fastboot.
for p in $(pgrep -f com.android.tradefed); do kill -9 "$p" 2>/dev/null; done
# With more than one phone running VTS or CTS, build the test modules once first: the phones
# then install and run them at the same time without building (atest -i -t).
if [ "$(echo "$PH" | grep -cE 'vts|cts')" -gt 1 ]; then
  say "building the test modules"
  ( cd "$TREE" && source build/envsetup.sh >/dev/null 2>&1 && lunch lineage_sargo cp2a userdebug >/dev/null 2>&1 \
    && atest -b vts_kernel_net_tests $CTS_MODS ) > "$TOP/atest-build.log" 2>&1 \
    || { say "FAIL: test module build (atest-build.log)"; exit 1; }
  export ATEST_STEPS="-i -t"
fi

i=0; pids=""
while read -r host only _; do
  i=$((i+1)); ONLY_OF[$i]=$only
  SARGO_TOP=$TOP/phone$i bash "$0" --child --host="$host" --only="$only" > "$TOP/phone$i.out" 2>&1 &
  pids="$pids $!"
done <<< "$PH"
# A phone whose machine is off or asleep (exit 3) is skipped; the run fails only when a check
# that phones.conf lists ran on no phone (2026-11-22: 192.168.50.58 was off and stopped a release
# whose checks had all run on the other phones).
bad=""; i=0; ran=,; want=,
for p in $pids; do
  i=$((i+1)); want="$want${ONLY_OF[$i]},"
  wait "$p"; rc=$?
  if [ "$rc" = 3 ]; then
    say "phone$i skipped: $(tail -1 "$TOP/phone$i.out")"; continue
  fi
  ran="$ran${ONLY_OF[$i]},"
  [ "$rc" = 0 ] || bad="$bad phone$i"
done
for c in $(tr , '\n' <<< "$want" | sort -u); do
  case $ran in *",$c,"*) ;; *) say "FAIL: no phone ran $c"; bad="$bad no-$c" ;; esac
done
cat "$TOP"/phone*/summary.txt >> "$S" 2>/dev/null
grep -h 'FAIL' "$TOP"/phone*/summary.txt | sed 's/^/  /'

verdict=PASS; [ -z "$bad" ] || verdict="FAIL:$bad"
# results.tsv: date, kernel commit (the last commit that changed kernel source, so later
# commits to backport-tools keep the result), verdict, phone build, report directory.
R=$BT/results.tsv
[ -f "$R" ] || printf 'date\tcommit\tverdict\tinstalled build\treport\n' > "$R"
printf '%s\t%s\t%s\t%s\t%s\n' "$(date '+%F %H:%M')" "$(git -C "$SRC" log -1 --format=%h --abbrev=12 -- ':(top)' ':(top,exclude)backport-tools' ':(top,exclude,glob)*.md')" "$verdict" \
  "$(adb shell getprop ro.lineage.version 2>/dev/null | tr -d '\r')" "${TOP#$HOME/}" >> "$R"
say "$verdict: $WANT (report: $S)"
if [ -n "$PUSH" ] && [ "$verdict" = PASS ]; then
  git -C "$SRC" add "$R" && git -C "$SRC" commit -q -m "boot-test: sargo-test PASS on $(cut -f2 <<< "$(tail -1 "$R")")" -- "$R" \
    && git -C "$SRC" push -q origin main && say "pushed: $(git -C "$SRC" log -1 --format='%h %s')" \
    || say "push failed: commit and push by hand"
fi
[ "$verdict" = PASS ]
