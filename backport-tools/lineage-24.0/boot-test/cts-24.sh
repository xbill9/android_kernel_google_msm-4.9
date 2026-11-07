#!/bin/bash
# CTS modules on a booted 24.0 phone, run module by module with atest from the LineageOS
# tree (atest builds each module and its test runner first). Same report format as
# vts-kernel-24.sh: PASS / FAIL per module with its test counts, SKIP when no test ran.
# Usage: cts-24.sh [core|<module> ...] [report dir via CTS_DIR=]
#   core   system, security, networking and this phone's hardware: see CORE below
# Each module is stopped after CTS_LIMIT seconds (default 3600; CtsLibcoreTestCases 3x that);
# a module stopped this way reports no result, so count its log lines instead.
# The camera tests need the screen on and unlocked without a PIN.
set -u
TREE=${LINEAGE_TREE:-$HOME/android/lineage-24.0}
LIMIT=${CTS_LIMIT:-3600}
CORE="CtsKernelConfigTestCases CtsSelinuxTargetSdkCurrentTestCases CtsBionicTestCases
  CtsSecurityTestCases CtsSecurityHostTestCases CtsPermissionTestCases CtsOsTestCases
  CtsNetTestCases CtsGraphicsTestCases CtsMediaMiscTestCases CtsSensorTestCases
  CtsHardwareTestCases CtsCameraTestCases CtsBluetoothTestCases CtsLocationFineTestCases
  CtsLibcoreTestCases"
# Not in core: CtsTelephonyTestCases swaps the phone's modem for a simulated one; stopped at the
# time limit on 2026-11-07, it left the phone with no cellular service. Run it alone, with no limit:
#   CTS_LIMIT=86400 cts-24.sh CtsTelephonyTestCases
case ${1:-core} in
  core) MODS=$CORE; SET=core ;;
  -*) sed -n '2,8p' "$0"; exit 1 ;;
  *) MODS="$*"; SET=custom ;;
esac

[ "$(adb devices | grep -c 'device$')" = 1 ] || { echo "need exactly one phone (adb devices)"; exit 1; }
SERIAL=$(adb get-serialno)
D=${CTS_DIR:-$HOME/sargo-logs/$SERIAL-cts-$SET-$(date +%Y%m%d-%H%M)}
mkdir -p "$D"
R=$D/report.txt; : > "$R"
cd "$TREE" && source build/envsetup.sh >/dev/null 2>&1 && lunch lineage_sargo cp2a userdebug >/dev/null 2>&1 \
  || { echo "lunch failed in $TREE"; exit 1; }
echo "phone $SERIAL, $(adb shell getprop ro.lineage.version | tr -d '\r'), kernel $(adb shell uname -r | tr -d '\r'), set $SET" | tee -a "$R"

pass=0 fail=0 skip=0 tp=0 tf=0
for m in $MODS; do
  log=$D/$m.log; t=$(date +%s)
  lim=$LIMIT; [ "$m" = CtsLibcoreTestCases ] && lim=$((LIMIT*3))
  timeout $lim atest "$m" > "$log" 2>&1; rc=$?
  # timeout stops atest but not the test run under it, which keeps the phone's UI automation
  # connection and breaks the next module: stop what is left.
  [ $rc = 124 ] && { pkill -9 -f atest_tradefed; pkill -9 -f "tradefed.*HeapDumpOnOutOfMemoryError"; sleep 3; }
  secs=$(( $(date +%s) - t ))
  sed -i 's/\x1b\[[0-9;]*m//g' "$log"
  # atest prints one line per ABI: "arm64-v8a <module>: Passed: 1, Failed: 0, Ignored: 0, Assumption Failed: 0"
  sum=$(grep -E "(^| )$m: Passed: [0-9]+" "$log" | awk -F'[:,] *' '{p+=$3; f+=$5; i+=$7; a+=$9} END {if (NR) print p, f, i, a}')
  if [ -z "$sum" ]; then
    # The runner skips a whole module the phone is exempt from (e.g. first API level too low)
    rdir=$(grep -m1 -oE '/tmp/atest_result_[^ ]+/log' "$log")
    why=$( [ -n "$rdir" ] && grep -rhoE "Skipping module $m because .*" "$rdir" 2>/dev/null | head -1 | sed "s/Skipping module $m because //")
    if [ -n "$why" ]; then
      printf '%-5s %-34s %s\n' SKIP "$m" "runner skipped it: $why" | tee -a "$R"; skip=$((skip+1)); continue
    fi
    why=$( [ $rc = 124 ] && echo "timed out after ${lim}s" \
      || grep -m1 -E '^FAILED: |error: |No test results available' "$log" | cut -c1-100)
    printf '%-5s %-34s %s\n' FAIL "$m" "no result: ${why:-exit $rc}" | tee -a "$R"; fail=$((fail+1)); continue
  fi
  read p f i a <<< "$sum"; tp=$((tp+p)); tf=$((tf+f))
  if [ "$f" != 0 ]; then s=FAIL; fail=$((fail+1)); elif [ "$p" = 0 ]; then s=SKIP; skip=$((skip+1)); else s=PASS; pass=$((pass+1)); fi
  printf '%-5s %-34s %s passed, %s failed, %s ignored, %s skipped (%ss)\n' $s "$m" $p $f $i $a $secs | tee -a "$R"
  [ "$f" = 0 ] || grep -E ': FAILED' "$log" | sed 's/^/        /' | head -20 | tee -a "$R"
  grep -m1 -oE '/tmp/atest_result_[^ ]+/log' "$log" | sed 's/^/        logs: /' >> "$R"
done

echo | tee -a "$R"
echo "$pass modules pass, $fail fail, $skip skip; $tp tests passed, $tf failed. Report and logs: $D" | tee -a "$R"
[ $fail = 0 ]
