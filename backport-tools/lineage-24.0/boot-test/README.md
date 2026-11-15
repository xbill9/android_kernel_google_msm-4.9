# Boot tests for 24.0 on sargo

Scripts for unattended boot tests and long log captures. They expect the
layout of the build machine: logs under `~/sargo-bpf-test/logs/`, logger
images under `~/sargo-bpf-test/kmsglog/`, the tree in `~/android/lineage-24.0`.

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
| `rawtptest.c`, `rawtptest` | Checks BPF raw tracepoints as root: counter programs attached to `sched_switch`, `sys_enter` and `sched_process_fork` count while attached and stop after detach; an unknown name gives ENOENT; reading past a tracepoint's arguments is refused at attach; the attachment is a `bpf_link` that stays attached while pinned in `/sys/fs/bpf` and detaches when the pin is removed. `build-test.sh rawtptest` rebuilds it. |
| `udp0test.c`, `udp0test` | Sends UDP datagrams over IPv4 and IPv6 to the wildcard, loopback and v4-mapped addresses, zero-length and one byte, to a socket bound to the wildcard address; prints each one and `N of 7 received`, exit 1 below 7. `build-test.sh udp0test` rebuilds it. |
| `qmaptest.c`, `qmaptest` | Checks that a qdisc caps an out-of-range `queue_mapping` stored by tc BPF, as root: a cls_bpf direct-action program on the prio root of a 4-queue dummy device stores 7, 64 Ethernet frames sent through AF_PACKET go through it, and the kernel must log `qmt0 selects TX queue 7`. Only for a kernel with the fix: without it the qdisc indexes past the device's tx queues. `build-test.sh qmaptest` rebuilds it. |
| `xdptest.c`, `xdptest` | Checks that generic XDP re-parses the Ethernet header a program rewrote, as root: on a veth pair, a frame for another host's MAC and a frame with EtherType 0x88b5 are dropped under a pass-only program and must reach a UDP socket once the program rewrites the MAC and the EtherType; controls check that a drop program drops and an unchanged frame arrives. On a kernel without the fixes both rewritten frames are dropped (installed build, 2026-11-14). `build-test.sh xdptest` rebuilds it. |
| `build-test.sh` | Builds the test programs above (all, or the names given) with the tree's clang and NDK sysroot, after a full build. `LINEAGE_TREE=` picks another tree. |


## Current state, 2026-11-14

Installed on 92FAY00ZMV (slot b, SELinux enforcing): build `24.0-20261113-1707`, kernel
`1d24d73d79e7`. Kernel `main` is ahead of it by the qdisc `queue_mapping` cap, 27 upstream
fixes (`12d8ae72200b`), 39 more upstream base fixes and the DualSense Edge device ID
(`d1d88b8bf32a`) and 2 generic XDP fixes (`b2c760a3f702`), tested by RAM boot; the next ROM
build picks them up.
Dated notes, newest first: `status-history.md`.

| Check | Installed build | `12d8ae72200b` (RAM boot) | `d1d88b8bf32a` (RAM boot) |
|---|---|---|---|
| Hardware check | 21 pass, 0 fail, 1 skip | 21 pass, 0 fail, 1 skip | 21 pass, 0 fail, 1 skip |
| Smoke test | | 18 pass, 1 fail (light sensor: no change at 0 lux), 8 check | 18 pass, 1 fail (light sensor), 8 check |
| VTS net | 647 pass, 0 fail (`092b9889c451`) | 647 pass, 0 fail | 647 pass, 0 fail (twice) |
| `ringbuftest`, `rawtptest`, `clone3test`(32) | 0 failed | 0 failed | 0 failed |
| `atomictest`, `udp0test` | 162 pass, 1 skip; 7 of 7 | 162 pass, 1 skip; 7 of 7 | 162 pass, 1 skip; 7 of 7 |
| `qmaptest` | (do not run: no fix) | PASS, 64 packets capped from queue 7 | PASS |
| `xdptest` | FAIL: rewritten frames dropped (no fix) | | PASS (`b2c760a3f702`) |
| bpfloader errors | `fuseMedia`, `dmabufIter` | the same 2 | the same 2 |
| CTS core | 12 of 16 modules with a result, below | | hardware 365/0, Bluetooth 473/0 |

CTS core on the installed build (2026-11-13 and 11-14): kernel config 5/0, SELinux 10/0,
bionic 3,286/0, graphics 3,774/0, permission 388/1, security host 3,347/150 (147 are neverallow
checks on a userdebug build), OS 861/5, hardware 360/5, Bluetooth 473/0, location 98/1,
libcore 32,251/0.

Known failures and why:

- **`CtsNetTestCases`**: its setup check (`ConnectivityCheckTest#testCheckWifiSetup`) finds no
  reachable global IPv6 on Wi-Fi, though the network has global addresses and an RA default
  route in the `LinkProperties` it logs. The kernel sends `RTM_NEWADDR` on every RA lifetime
  refresh (watched 2026-11-14: RAs every 2 min, lifetimes 600 s), so userspace does not see
  stale deprecated addresses; the cause is above the kernel. Same on `12d8ae72200b` and
  `d1d88b8bf32a`.
- **`CtsSensorTestCases`, `CtsCameraTestCases`**: stop at the 1 h limit; sensor tests pass
  alone (timing).
- **`CtsSecurityTestCases`**: runner error after its multi-user test, which leaves a test user.
- **Hardware, 5** (installed build only): `SonyDualSenseEdge*`: its `hid-playstation` has no
  DualSense Edge device ID (v6.2). `d1d88b8bf32a` adds it: hardware 365/0.
- **OS, 5**: one new on 11-13, `StrictModeTest#testUnsafeIntentLaunch_UnsafeDataCopy_ThrowsViolation`.
- **Location, 1**: `GeofencingTest#testAddProximityAlert`, timed out after 5 min.
- **LTP, 8**: kernel version checks, 4.9 CPU timer rounding, overlayfs inode hashing (see
  `status-history.md`, 2026-11-13).
