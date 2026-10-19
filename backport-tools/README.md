# sargo BPF backport tooling

Scripts and bookkeeping used to backport newer eBPF support onto the
LineageOS 4.9 kernel for the Pixel 3a (`sargo`, `bonito_defconfig`).
`main` holds everything: the kernel tree from `bpf-backport-s2` with this
directory merged in as `backport-tools/`. The other branches are kept for
history:

- `bpf-backport-s2` — current work and the default branch: the 1642-commit
  stage-2 series replayed onto `lineage-22.2`, plus build fixes, a CFI fix,
  `CONFIG_BPF_EVENTS`, two verifier/ABI fixes from 4.15, arm64 uprobes (the
  4.10 upstream series and later fixes from 4.14, `CONFIG_UPROBE_EVENT=y`),
  the 4.11 `KPROBE_EVENTS`/`UPROBE_EVENTS` option names, and a fix for
  kprobing CFI function bodies. Commits that needed a hand merge say how in
  a `[ 4.9: ... ]` note. Runs daily on a Pixel 3a.
- `bpf-uprobes` — where the uprobe work was developed; now the same commit
  as `bpf-backport-s2`.
- `bpf-backport` — stage 1 (441-commit series in `series.txt`).

## Build

`build.sh` builds `Image.lz4` the way LineageOS does: clang r416183b with
GNU binutils from the Android 4.9 prebuilts, full LTO, CFI. Expects
`~/sargo-bpf-test/{ksargo,kout,tc}`. `build-base.sh` builds the unmodified
base tree for comparison.

## Status

Tested on a Pixel 3a running LineageOS 22.2, first RAM-booted and then
flashed for daily use (see below):

- Boots cleanly; no oops, BUG or CFI reports. The only kernel warning is an
  ION warning from a vendor HAL that the stock kernel logs too.
- Android's bpfloader loads every platform program except the three
  optional uprobestats objects, whose maps need a BPF ring buffer
  (`BPF_MAP_TYPE_RINGBUF`, kernel 5.8). bpfloader skips maps by kernel
  version, so the programs then fail to load. This is harmless. netd's cgroup/socket
  programs, timeInState (`sched_switch`, `cpu_frequency`,
  `sched_process_free`), gpuMem and gpuWork load, attach and run.
- Wi-Fi works with a `wlan.ko` built from this tree, and networking with
  per-UID traffic accounting works.
- The linux-5.4.y BPF verifier selftests pass: `test_verifier` from 5.4.302
  gives 1610 passed, 0 failed, 3 skipped (the skips are sockmap, sockhash
  and xskmap, which sargo does not configure).
- Uprobe and uretprobe trace events work, and a BPF
  program attached to a uprobe via `PERF_EVENT_IOC_SET_BPF` counts every
  hit (`uprobe_bpf.c`: 1000 calls, 1000 hits). BPF programs on the perf
  kprobe and uprobe PMUs count every hit too (`probe_pmu_bpf.c`). Each
  test kernel was built with no local changes.

## Replaying a series

`next.sh` (stage 1) and `next2.sh` (stage 2) cherry-pick `series*.txt` one
commit at a time, resuming from the index in `pos*.txt`, and stop at the
first conflict that needs a human. `next2.sh` also skips indices listed in
`skiplist2.txt` and auto-drops conflicts confined to subsystems that are
absent from the 4.9 tree or not built for sargo.

| File | Contents |
| --- | --- |
| `series.txt`, `series2.txt` | commits to replay, in order |
| `stage2-range.txt` | base and tip of the stage-2 source range |
| `skiplist2.txt` | series indices skipped by hand |
| `skipped2.tsv` | every skipped commit, with the reason |
| `autodropped2.tsv` | files dropped from conflicting commits automatically |
| `unbuilt2.tsv` | conflicting files in drivers not built for sargo |
| `groundwork2.txt` | prerequisite commits pulled in ahead of the series |
| `mapper.txt` | BPF helper ID → name table, for checking helper ABI order |

`conflict_test.sh REPO BASE TIP OUT` dry-runs a range and classifies each
commit as clean, already present, or conflicting. `resolve.py FILE
ours|theirs|union` resolves every conflict hunk in a file one way.

## Boot testing

Repack the stock boot image with the new kernel and RAM-boot it; nothing is
flashed, and a reboot returns to stock:

    python3 tools/mkbootimg.py <stock args, --kernel kout/arch/arm64/boot/Image.lz4> --output boot-test.img
    adb reboot bootloader && fastboot boot boot-test.img

If it panics, the phone falls back to the flashed kernel. Run
`adb bugreport` before rebooting again: on a user build, dumpstate copies
`/sys/fs/pstore/console-ramoops-0` into the report as `LAST KMSG`.
On a userdebug ROM with "Rooted debugging" enabled, `adb root` reads
pstore directly.

To check that a BPF program is actually running, not just loaded:

    adb root
    adb shell 'echo 1 > /proc/sys/kernel/bpf_stats_enabled'
    adb shell 'ls -l /proc/<pid>/fd | grep bpf-prog'   # find the holder's fds
    adb shell 'grep run_ /proc/<pid>/fdinfo/<fd>'      # run_cnt, run_time_ns

## Installing for daily use

Back up the active slot's boot partition first, and build the image from it
rather than from an older backup, since OTAs change the ramdisk:

    S=$(adb shell getprop ro.boot.slot_suffix | tr -d '\r')
    adb root
    adb shell "dd if=/dev/block/by-name/boot$S of=/data/local/tmp/boot.img"
    adb pull /data/local/tmp/boot.img img/boot-current.img
    python3 tools/unpack_bootimg.py --boot_img img/boot-current.img \
        --out img/cur --format=mkbootimg > img/cur.args
    # replace img/cur/kernel in cur.args with kout's Image.lz4, then mkbootimg
    adb reboot bootloader && fastboot flash boot$S boot-new.img

Wi-Fi needs a `wlan.ko` built from the same tree (`build.sh modules`,
then `strip --strip-debug`) in `/vendor/lib/modules/`. On LineageOS AVB is
already disabled, so as root `mount -o remount,rw /vendor` works. The
vendor filesystem is packed full, and ext4's reserved clusters make writes
fail with `ENOSPC` even with free blocks. Lower the reserve just for the
write, overwrite in place, and restore it:

    R=/sys/fs/ext4/dm-1/reserved_clusters; old=$(cat $R)
    mount -o remount,rw /vendor && echo 0 > $R
    dd if=/data/local/tmp/wlan.ko of=/vendor/lib/modules/wlan.ko bs=1M conv=notrunc,fsync
    echo $old > $R; mount -o remount,ro /vendor

Don't use `cat >`: it truncates first, and a failed write leaves a partial
module. Keep the stock `wlan.ko` and boot image to roll back. An OTA
installs a stock kernel and vendor to the other slot.

## Selftests

`build-selftests.sh [DIR]` sparse-clones linux-5.4.y, builds
`test_verifier` as a static arm64 binary against `kout`'s config, pushes it
to `/data/local/tmp` and runs it (needs `adb root`). The header of the
script lists the Debian packages for cross-building (arm64 multiarch
libelf, libcap, zlib, zstd). The backported verifier is roughly at 5.4 in
features, so 5.4.y is the matching suite; older suites fail tests for
behaviour that has since changed upstream.

`uprobe_bpf.c` is a standalone check for BPF on uprobes. Build it with
`aarch64-linux-gnu-gcc -O2 -static -o uprobe_bpf uprobe_bpf.c`, push it to
`/data/local/tmp` and run it as root. It probes its own function, so it
needs no target binary. For text trace events, note that Android boots with
`tracing_on` set to 0.

`probe_pmu_bpf.c` does the same through the perf `kprobe` and `uprobe`
PMUs (no tracefs) and prints what `BPF_TASK_FD_QUERY` reports for each.
These need `CONFIG_KPROBE_EVENTS`/`CONFIG_UPROBE_EVENTS`, which the kernel
provides as aliases of the 4.9 option names.

## Known limitations

- The stock vendor `wlan.ko` refuses to load into any custom build
  (`disagrees about version of symbol module_layout`).
- Kprobes and CFI: with `CONFIG_CFI_CLANG`, a function body is named
  `<name>.cfi` and the plain name is its jump-table entry, which only
  indirect calls reach. A kprobe on `do_sys_open` therefore attaches but
  never fires; probe `do_sys_open.cfi` (check `/proc/kallsyms`). This is
  how the stock 4.9 CFI kernel behaves too. This tree makes the perf
  kprobe PMU accept `.cfi` names, which used to fail with `EINVAL`.
- No BPF ring buffer (5.8). It would not help bpfloader anyway: bpfloader
  gates maps on kernel version, so uprobestats' 5.8 maps are skipped on any
  4.9 kernel.
- Vendor `wlan.ko` must match the kernel. `CONFIG_QCA_CLD_WLAN=m` is in
  `bonito_defconfig`, so `build.sh modules` produces a matching
  `drivers/staging/qcacld-3.0/wlan.ko`; strip it with `--strip-debug` and
  replace `/vendor/lib/modules/wlan.ko`.
