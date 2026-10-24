# Boot tests for 24.0 on sargo

Scripts for unattended boot tests and long log captures. They expect the
layout of the build machine: logs under `~/sargo-bpf-test/logs/`, logger
images under `~/sargo-bpf-test/kmsglog/`, the tree in `~/android/lineage-24.0`.

| File | Does |
|---|---|
| `kmsglog.c` | 4 KB freestanding `/init` stand-in. Copies the kernel log to `/metadata/boot-kmsg.txt` and logcat to `/metadata/logcat.txt`, then execs the real init. `kmsglog.reboot=<s>` on the cmdline dumps blocked tasks and reboots to the bootloader after that many seconds. The log file is truncated each boot. |
| `add-logger.sh` | Repacks a boot image with kmsglog as `/init`. Env: `NOLOG=1`, `KERNEL=`, `EXTRA_CMDLINE=`. |
| `test-boot.sh` | RAM-boots an image on slot a and pulls the logger output (written for 23.2 on slot a). |
| `test-boot-b.sh` | Same on slot b, returning to slot a afterwards. |
| `test-boot-a24.sh` | RAM-boots an image on slot a (24.0 full build). On a hang, boots slot b with the logger-free test image and pulls `/metadata` logs. |
| `ota-install-24.sh` | Waits for a build, then installs its OTA into the inactive slot with `update_engine_client` from the running system. |
| `capture-24.sh` | Streams logcat and `dmesg -w` to the host and records Wi-Fi link, ping times and Bluetooth state every 30 s. Reconnects across adb drops. |

## Status, 2026-10-24

- **Slot b, RAM-booted test image** (`boot-24-perm-nolog.img`: debugfs-patched
  system and vendor, SELinux permissive): reaches `boot_completed`; Wi-Fi and
  Settings work.
- **Slot a, full build with all fixes** (OTA installed 2026-10-24 01:59):
  does not boot.
  - Enforcing: stays on the Google logo, never enumerates on USB.
  - Permissive: reaches the boot animation, then netd blocks in the kernel
    (`_rcu_barrier` <- `netdev_run_todo` <- `rtnetlink_rcv`), system_server
    waits for it (`returning null INetd instance`) until the watchdog kills it,
    and the cycle repeats. `crtc_commit` is also blocked on a display
    interrupt. No RCU stall is reported.
  - Kernel, dtb and command line are byte-identical to the working slot b
    image, so the trigger is in the full build's system/vendor images or
    ramdisk.
