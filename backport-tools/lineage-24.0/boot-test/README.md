# Boot tests for 24.0 on sargo

The checks `sargo test` runs on a RAM-booted kernel, and tools for boot problems and long log
captures. `sargo update` and `sargo test` (`../sargo-test.sh`) runs them in order and judges the result; each also
runs alone on a booted phone. They expect the build machine's layout: the tree in
`~/android/lineage-24.0`, logs under `~/sargo-bpf-test/logs/` and `~/sargo-logs/`.

| File | Does |
|---|---|
| `kmsglog.c` | 4 KB freestanding `/init` stand-in. Copies the kernel log to `/metadata/boot-kmsg.txt` and logcat to `/metadata/logcat.txt`, then execs the real init. `kmsglog.reboot=<s>` on the cmdline dumps blocked tasks and reboots to the bootloader after that many seconds. The log file is truncated each boot. |
| `add-logger.sh` | Repacks a boot image with kmsglog as `/init`. Env: `NOLOG=1`, `KERNEL=`, `EXTRA_CMDLINE=`. |
| `test-boot-a24.sh` | RAM-boots an image on slot a (24.0 full build). On a hang, boots slot b with the logger-free test image and pulls `/metadata` logs. |
| `post-install-24.sh` | After an OTA and the GApps sideload: grants Play Services its missing runtime permissions and runs `pm bg-dexopt-job`, so apps are compiled instead of left at `verify`. |
| `capture-24.sh` | Streams logcat and `dmesg -w` to the host and records Wi-Fi link, ping times and Bluetooth state every 30 s. Reconnects across adb drops. |
| `hw-check-24.sh` | Checks the hardware of a booted phone over adb in about 2 minutes: Wi-Fi traffic, Bluetooth off and on, NFC, modem and SIM, GNSS HAL, sensors, rear and front camera photos (Aperture), speaker output, vibrator, fingerprint HAL, screen off and on, battery, storage speed, crashes and SELinux denials. Prints PASS, FAIL or SKIP with the evidence and counts them; results and logs go to `~/sargo-logs/<serial>-hwcheck-<time>/`. Not covered: microphone, GPS fix, calls, fingerprint enrolment. Apps the user disabled are enabled for their test only. |
| `sensorread.c`, `sensorread` | Reads one sensor from the adb shell: `sensorread <type> [timeout_s]` prints the first event or exits 1. The smoke test uses it for the barometer (type 6), which nothing else keeps open. `build-test.sh sensorread` rebuilds it. |
| `cts-24.sh` | Runs CTS modules with atest, one at a time, in the report format of `vts-kernel-24.sh`. `core` is 16 modules: kernel config, SELinux, bionic, security, permissions, OS, networking, graphics, media, sensors, hardware, camera, Bluetooth, location and libcore. Each module stops after an hour (libcore three). Telephony is left out: stopped at the limit it left the phone without cellular service. Needs `verifier_verify_adb_installs` set to 0. Results go to `~/sargo-logs/<serial>-cts-<set>-<time>/`. |
| `clone3test.c`, `clone3test`, `clone3test32` | Checks the kernel's `clone3` and capabilities 38-40 from the adb shell, as arm64 and as 32-bit arm: `cap_last_cap`, `PR_CAPBSET_READ`, the size, flag, signal and stack checks, `CLONE_PIDFD` with `CLONE_PARENT_SETTID`, the child's stack pointer and TLS, `CLONE_VM`. PASS or FAIL per case, exit 1 on any failure. `build-test.sh clone3test` rebuilds both binaries. |
| `ringbuftest.c`, `ringbuftest` | Checks the kernel's BPF ring buffer as root: map creation rules, `mmap` protections, `epoll` wakeup, records written by a socket filter run with `BPF_PROG_TEST_RUN` and read back through `mmap` (one straddling the end of the ring), a full ring, `bpf_ringbuf_query`, and eight programs the verifier must reject. `build-test.sh ringbuftest` rebuilds it. |
| `erofstest.sh`, `erofstest-device.sh` | Checks the kernel's EROFS: builds uncompressed, lz4, lz4hc and SELinux-labelled images with the tree's `mkfs.erofs`, mounts each on a loop device and compares every file, symlink, hard link and label; big-pcluster and ztailpacking images must be refused. `GAPPS_ZIP=<MindTheGapps zip>` adds its Play Services APEX payload. |
| `atomictest.c`, `atomictest`, `atomictest-cases.inc` | Checks the kernel's BPF atomic instructions as root: linux v6.1's `verifier/atomic_*.c` cases (in `atomictest-cases.inc`), each loaded as root and as an unprivileged uid and, when accepted, run with `BPF_PROG_TEST_RUN`; precision tracking through `atomic64_fetch_add`; and 10,000-op loops of each operation checked against C, which must be JIT-compiled. `atomictest interp` runs it all with `bpf_jit_enable` 0 and restores it after. The verifier log's `'inv'` (up to 5.17) counts as v6.1's `'scalar'`. `build-test.sh atomictest` rebuilds it. |
| `membarriertest.c`, `membarriertest` | Checks the `membarrier(2)` private expedited commands as ART uses them: `MEMBARRIER_CMD_QUERY` returns 0x79; `EPERM` before registration; registration with another thread running; `PRIVATE_EXPEDITED` and `PRIVATE_EXPEDITED_SYNC_CORE`; `EINVAL` for the global expedited commands, which are not backported; registration kept across `fork()` and cleared by `exec`; and a store-buffering test on three CPUs (200000 rounds each) where both loads seeing 0 is forbidden with either command, after a control run without a barrier that shows the reordering. Run from the adb shell. |
| `mreleasetest.c`, `mreleasetest` | Checks `process_mrelease(2)` as lmkd's reaper uses it: `EBADF` for a bad fd and for an fd that is not a pidfd (lmkd's probe needs anything but `ENOSYS`), `EINVAL` for a flag or a process that is not dying, `ESRCH` once reaped; then `mreleasetest N` (default 200) children touch 16 to 256 MiB and are killed through their pidfd and reaped at once: only 0 or `ESRCH`. |
| `madvisetest.c`, `madvisetest` | Checks `MADV_COLD`, `MADV_PAGEOUT` and `process_madvise(2)` as Android's CachedAppOptimizer uses them, by their effect: `MADV_COLD` moves 64 MiB off the active list (kpageflags), `MADV_PAGEOUT` puts 64 MiB in swap and out of RSS with the data intact; pages shared after fork are left alone, locked memory gives `EINVAL`. `process_madvise`: bytes advised returned (partial count before an unmapped range, `ENOMEM` for only that), `EINVAL` for a flag or another advice, `EBADF` for a non-pidfd, `EPERM`/`EACCES` for an app uid, `ESRCH` once exited. Root, needs zram. |
| `uffdtest.c`, `uffdtest` | Checks userfaultfd and `mremap(MREMAP_DONTUNMAP)` as ART's concurrent mark-compact GC uses them: `MREMAP_DONTUNMAP` on private and shared anonymous memory, with `MREMAP_FIXED` and on part of a locked mapping (`VmLck` unchanged); `UFFD_USER_MODE_ONLY` (allowed for an app uid with `vm.unprivileged_userfaultfd` 0), `UFFD_FEATURE_SIGBUS`, `UFFDIO_COPY` and `UFFDIO_ZEROPAGE` with the `EEXIST` and `ENOENT` errors ART tolerates, `EFAULT` for a kernel access to a missing page, and a registered range moved with `MREMAP_DONTUNMAP` (the old range stays registered, the new one is not). Run as root. |
| `fsveritytest.c`, `fsveritytest` | Checks fs-verity on /data as root: files of 0 B to 16 MB (one to three tree levels, with and without a salt) get `FS_IOC_ENABLE_VERITY`; the measured digest must equal the one it computes (checked against fsverity-utils 1.6 `fsverity digest`); `FS_IOC_GETFLAGS` must show `FS_VERITY_FL`; the file must read back unchanged after dropping the page cache; writing, truncating and enabling again must fail. `fsveritytest list <file>...` lists files with verity; `fsveritytest strip <dir>...` makes them plain files again (sargo-test runs it before leaving a test kernel). |
| `rawtptest.c`, `rawtptest` | Checks BPF raw tracepoints as root: counter programs attached to `sched_switch`, `sys_enter` and `sched_process_fork` count while attached and stop after detach; an unknown name gives ENOENT; reading past a tracepoint's arguments is refused at attach; the attachment is a `bpf_link` that stays attached while pinned in `/sys/fs/bpf` and detaches when the pin is removed. `build-test.sh rawtptest` rebuilds it. |
| `udp0test.c`, `udp0test` | Sends UDP datagrams over IPv4 and IPv6 to the wildcard, loopback and v4-mapped addresses, zero-length and one byte, to a socket bound to the wildcard address; prints each one and `N of 7 received`, exit 1 below 7. `build-test.sh udp0test` rebuilds it. |
| `qmaptest.c`, `qmaptest` | Checks that a qdisc caps an out-of-range `queue_mapping` stored by tc BPF, as root: a cls_bpf direct-action program on the prio root of a 4-queue dummy device stores 7, 64 Ethernet frames sent through AF_PACKET go through it, and the kernel must log `qmt0 selects TX queue 7`. Only for a kernel with the fix: without it the qdisc indexes past the device's tx queues. `build-test.sh qmaptest` rebuilds it. |
| `xdptest.c`, `xdptest` | Checks that generic XDP re-parses the Ethernet header a program rewrote, as root: on a veth pair, a frame for another host's MAC and a frame with EtherType 0x88b5 are dropped under a pass-only program and must reach a UDP socket once the program rewrites the MAC and the EtherType; controls check that a drop program drops and an unchanged frame arrives. On a kernel without the fixes both rewritten frames are dropped (installed build, 2026-11-14). `build-test.sh xdptest` rebuilds it. |
| `results.tsv` | one line per `sargo test` run: date, kernel commit (the last commit that changed kernel source), PASS or FAIL with the failed checks, installed build, report directory. `sargo-build` puts the line for its kernel in the release notes. |
| `build-test.sh` | Builds the test programs above (all, or the names given) with the tree's clang and NDK sysroot, after a full build. `LINEAGE_TREE=` picks another tree. |


## Current state

Installed on 92FAY00ZMV (slot a, SELinux enforcing): build `24.0-20261115-1255` (marked Latest),
kernel `f3ef238ee578`. Kernel test results, one line per `sargo test` run: `results.tsv`
(commit, PASS/FAIL, report directory under `~/sargo-logs`). Dated notes, newest first:
`status-history.md`.

What a passing `sargo test` shows, and the known differences it does not count:

| Check | Result on a good kernel | Not counted |
|---|---|---|
| unit tests (`qmaptest` … `udp0test`) | all exit 0; `atomictest` 162 pass, 1 skip; `udp0test` 7 of 7; `fsveritytest` 148 pass; `uffdtest` 41 pass; `mreleasetest` 10 pass; `madvisetest` 20 pass | |
| smoke test | 18 pass, 1 fail, 8 check | the light sensor shows no change at 0 lux |
| hardware check | 21 pass, 0 fail, 1 skip | the microphone (needs a person) |
| VTS net | 647 pass, 0 fail | |
| CTS net | 1005 pass, 6 fail | the 6 below |
| bpfloader | 2 errors: `fuseMedia`, `dmabufIter` | both |
| kernel log | no oops, BUG or WARNING | kswapd allocation failures when the camera starts |

CTS core on the installed build (2026-11-13 and 11-14): kernel config 5/0, SELinux 10/0,
bionic 3,286/0, graphics 3,774/0, permission 388/1, security host 3,347/150 (147 are neverallow
checks on a userdebug build), OS 861/5, hardware 360/5, Bluetooth 473/0, location 98/1,
libcore 32,251/0.

Known failures and why:

- **`CtsNetTestCases`**: 1005 passed, 6 failed (installed build, 2026-11-14). Its setup check
  used to fail, for two reasons above the kernel, both fixed in the Connectivity fork
  (`lineage-24.0`):
  - `LinkProperties.equals` compares addresses only, so Android drops the lifetime refreshes the
    kernel sends on each RA and the address counts as deprecated 10 min after Wi-Fi connects
    (lifetimes 600 s here). Fix: `89283a3828`, tested on `24.0-20261115-1255`. On older builds,
    reconnect Wi-Fi (`svc wifi disable; svc wifi enable`) before a Net module.
  - The setup app's QUIC check read a hidden `framework.jar` copy of `NetworkStackConstants`
    (`NoSuchFieldError`). Fix: `3324d2f8c9`, tested.

  The 6 failures, not looked into yet: `ApfApiTest#testReadWriteProgram` (Wi-Fi firmware APF
  memory), `ConnectivityManagerTest#testRestrictedNetworkPermission`,
  `EthernetManagerTest#testUpdateConfiguration_forAllowedUids`, and three
  `NetworkNdkTest#testBlockedReason_*` (blocked reason -92, `ENOPROTOOPT`).
- **`CtsSensorTestCases`, `CtsCameraTestCases`**: stop at the 1 h limit; sensor tests pass
  alone (timing).
- **`CtsSecurityTestCases`**: runner error after its multi-user test, which leaves a test user.
- **Hardware, 5** (installed build only): `SonyDualSenseEdge*`: its `hid-playstation` has no
  DualSense Edge device ID (v6.2). `d1d88b8bf32a` adds it: hardware 365/0.
- **OS, 5**: one new on 11-13, `StrictModeTest#testUnsafeIntentLaunch_UnsafeDataCopy_ThrowsViolation`.
- **Location, 1**: `GeofencingTest#testAddProximityAlert`, timed out after 5 min.
- **LTP, 8**: kernel version checks, 4.9 CPU timer rounding, overlayfs inode hashing (see
  `status-history.md`, 2026-11-13).
