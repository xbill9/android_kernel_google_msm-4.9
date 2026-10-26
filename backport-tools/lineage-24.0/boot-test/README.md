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

## Status, 2026-10-26

- **Slot a, full build** (`24.0-20261026-UNOFFICIAL-sargo`, OTA installed
  2026-10-26 14:19, kernel `4.9.337-gb80340096fda`): boots with SELinux
  enforcing in 82 s; surfaceflinger, system_server, netd and Wi-Fi run.
  With bonito `a6e99a3` (sensors ION, Wi-Fi version props, USB gadget
  config prop, power HAL as thermal client) three boots from flash took 81,
  48 and 80 s, with no kernel errors and Wi-Fi up each time. Left per boot:
  8 `qtidataservices_app` service lookups and one each of `nfc` on
  `nfc_vendor_data_file`, `hal_camera_default` on `zygote_config_prop`,
  `qtelephony` and `vendor_init` on `default_prop`, `init-edge_sense-sh` on
  the SoC serial number. `org.protonaosp.elmyra`
  (Active Edge) restarts in a loop with `IndexOutOfBoundsException: Index 0
  out of bounds for length 0`, in permissive boots too. The cause is CHRE:
  `vendor.chre` exits every 5 s because the sensor DSP rejects its program
  (`dynamic loading failed for chre_slpi on domain 0 (dlerror undefined
  symbol #25 __sensors_island_start in ./libchre_slpi_skel.so)`), so the
  vendor blob and the DSP firmware on the phone do not match.
- Camera: Aperture opens camera 0 and the preview runs at 15 fps for 45 s
  with the provider up; capture not yet tried. `dumpsys media.camera` piped
  into `grep -m` kills the camera provider with SIGPIPE when grep exits.
- The two failures below had separate causes:
  - **Permissive hang** (netd in `_rcu_barrier`, about 1 boot in 5): kernel
    `e7a12e10fc29`. `cgroup_sk_clone()` took no `cgroup_bpf` reference, so
    each `accept()`ed socket dropped one it did not hold, and removing a
    cgroup freed its BPF programs while sockets still used them. The sign
    was `percpu ref (cgroup_bpf_release_fn) <= 0` in the kernel log. Before
    the fix 2 of 9 boots hung; after it 8 of 8 booted.
  - **Enforcing black screen**: Android 17 removed the `/dev/ion` label and
    every `ion_device` rule from the platform policy. bonito `2cbfb4b` and
    `e2ca4b1` put back the label and the Android 16 rules.

Method notes:

- pstore survives the logger's reboot to the bootloader (a warm reboot), and
  is wiped by a Power + Volume Down reset. An enforcing boot resets itself
  at about 82 s, so use `EXTRA_CMDLINE=kmsglog.reboot=60` to reboot first.
- With SELinux enforcing the logger loses its kmsg node once the policy
  loads and stops; the permissive boots' `permissive=1` denials show what
  enforcing would block.

## Status, 2026-10-24 (superseded)

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
