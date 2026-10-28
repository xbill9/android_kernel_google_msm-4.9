#!/bin/bash
# VTS kernel tests on a booted 24.0 phone, run module by module with atest from the
# LineageOS tree (atest builds each module and its test runner first). Prints PASS / FAIL
# per module with its test counts, or SKIP when no test ran (e.g. the feature is absent),
# and saves each module's atest output.
# Left out: the GKI, 16 KB page size and bootloader tests, which need a newer kernel or device.
# Usage: vts-kernel-24.sh [quick|ltp|kselftest|all] [report dir]
#   quick      the kernel API, file system, encryption, networking and BPF modules
#   ltp        the Linux Test Project (long)
#   kselftest  the kernel's own self-tests
set -u
TREE=${LINEAGE_TREE:-$HOME/android/lineage-24.0}
SET=${1:-quick}
QUICK="drop_caches_test vts_kernel_net_tests vts_kernel_encryption_test vts_kernel_loopconfig_test
  vts_memfd_class_selinux_test KernelApiSysfsTest KernelAbilistTest KernelDynamicPartitionsTest
  vts_kernel_checkpoint_test vts_kernel_fuse_bpf_test f2fs_test vts_kernel_toolchain
  vts_kernel_isa_test zram_module_test vts_test_binary_zram_module vts_virtual_ab_test
  vts_test_binary_bow_module"
case $SET in
  quick) MODS=$QUICK; LIMIT=1800 ;;
  ltp) MODS=vts_ltp_test_arm_64; LIMIT=28800 ;;
  kselftest) MODS=vts_linux_kselftest_arm_64; LIMIT=7200 ;;
  all) MODS="$QUICK vts_linux_kselftest_arm_64 vts_ltp_test_arm_64"; LIMIT=28800 ;;
  *) sed -n '7,10p' "$0"; exit 1 ;;
esac

[ "$(adb devices | grep -c 'device$')" = 1 ] || { echo "need exactly one phone (adb devices)"; exit 1; }
SERIAL=$(adb get-serialno)
D=${2:-$HOME/sargo-logs/$SERIAL-vts-kernel-$SET-$(date +%Y%m%d-%H%M)}
mkdir -p "$D"
R=$D/report.txt; : > "$R"
cd "$TREE" && source build/envsetup.sh >/dev/null 2>&1 && lunch lineage_sargo cp2a userdebug >/dev/null 2>&1 \
  || { echo "lunch failed in $TREE"; exit 1; }
echo "phone $SERIAL, $(adb shell getprop ro.lineage.version | tr -d '\r'), kernel $(adb shell uname -r | tr -d '\r'), set $SET" | tee -a "$R"

pass=0 fail=0 skip=0 tp=0 tf=0
for m in $MODS; do
  log=$D/$m.log; t=$(date +%s)
  timeout $LIMIT atest "$m" > "$log" 2>&1; rc=$?
  secs=$(( $(date +%s) - t ))
  sed -i 's/\x1b\[[0-9;]*m//g' "$log"
  # atest prints one line per ABI: "arm64-v8a <module>: Passed: 1, Failed: 0, Ignored: 0, Assumption Failed: 0"
  sum=$(grep -E "(^| )$m: Passed: [0-9]+" "$log" | awk -F'[:,] *' '{p+=$3; f+=$5; i+=$7; a+=$9} END {if (NR) print p, f, i, a}')
  if [ -z "$sum" ]; then
    why=$( [ $rc = 124 ] && echo "timed out after ${LIMIT}s" || grep -m1 -iE 'error|fail' "$log" | cut -c1-80)
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
