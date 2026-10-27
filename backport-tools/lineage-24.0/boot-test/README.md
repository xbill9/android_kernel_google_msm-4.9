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
| `post-install-24.sh` | After an OTA and the GApps sideload: grants Play Services its missing runtime permissions and runs `pm bg-dexopt-job`, so apps are compiled instead of left at `verify`. |
| `capture-24.sh` | Streams logcat and `dmesg -w` to the host and records Wi-Fi link, ping times and Bluetooth state every 30 s. Reconnects across adb drops. |
| `hw-check-24.sh` | Checks the hardware of a booted phone over adb in about 2 minutes: Wi-Fi traffic, Bluetooth off and on, NFC, modem and SIM, GNSS HAL, sensors, rear and front camera photos (Aperture), speaker output, vibrator, fingerprint HAL, screen off and on, battery, storage speed, crashes and SELinux denials. Prints PASS, FAIL or SKIP with the evidence and counts them; results and logs go to `~/sargo-logs/<serial>-hwcheck-<time>/`. Not covered: microphone, GPS fix, calls, fingerprint enrolment. Apps the user disabled are enabled for their test only. |

## Status, 2026-10-27

- **Hardware check** (`hw-check-24.sh`, 24.0-20261027 with GApps): 20 pass,
  0 fail, 2 skipped (no SIM, microphone) on both 937AY07L03 (slot a) and
  935AY06U23 (slot a, 14:02 build). Rear and front camera photos, speaker
  output at about 54,000 frames/s, Bluetooth off and on in 3 s, storage
  write 164 MB/s and read 319 MB/s. No crashes since boot.
- **qtidataservices_app** crashed at each start (`NullPointerException` in
  `ActivityThread.attach`) because it was denied the `activity` service
  lookup. bonito `3dbaf3e` grants `app_api_service`; with the 14:15 build
  the denials and the crashes are gone.
- **Camera denials**: `hal_camera_default` reads `ro.camera.req.fmq.size`
  and `ro.camera.res.fmq.size` (unlabelled, so `default_prop`) and
  `ro.bootimage.build.fingerprint` (`build_bootimage_prop`). bonito
  `0e07c00` labels the first two `vendor_default_prop` and grants the
  third. Not built yet. Photos work with or without it.
- **Wi-Fi latency**: pings over Wi-Fi take 100-250 ms in steps of about
  102 ms (the beacon interval) on both phones and on two networks; on
  935AY06U23 also from the laptop to the phone. The same router answers a wired host in 2.3 ms. tcpdump on
  the phone shows the replies reaching the kernel as late as `ping`
  reports, so the delay is in the driver, the chip firmware or the air.
  Measured on 935AY06U23, 12 or 15 pings at 1 s each:

  | Change | Phone to router | Laptop to phone |
  |---|---|---|
  | none (screen off) | 16-231 ms | 129-4,221 ms, 6 of 15 lost |
  | screen on | 23-484 ms | 4.7-2,307 ms |
  | screen on, after a reboot | 2.6-632 ms | 92-3,700 ms |
  | screen on, `iw dev wlan0 set power_save off` | 5.7-249 ms | 9.7-248 ms |
  | `cmd wifi force-low-latency-mode enabled` | 5.7-211 ms | not run |
  | all 8 CPUs busy | 14-226 ms | not run |
  | `gIPAConfig=0x3d` (no IPA STA offload) | 2.5-257 ms | 463-4,948 ms, 1 of 15 lost |
  | `gEnableBmps=0`, `gEnableImps=0`, `gEnableModulatedDTIM=0` | 3.5-210 ms | 9.5-527 ms |
  | Bluetooth and BLE scanning off | 3.6-210 ms | not run |

  The chip's power save (the stock `WCNSS_qcom_cfg.ini` settings) explains
  the multi-second delays to the phone; nothing tried removes the ~100 ms
  steps. `drivers/staging/qcacld-3.0` and `drivers/platform/msm/ipa` are
  unchanged in this tree apart from build fixes. No 23.2 or stock
  measurement exists to compare with. To test ini changes without a
  reboot: `svc wifi disable`, `rmmod wlan`, bind-mount an edited copy
  (label `vendor_firmware_file`) over
  `/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini`,
  `insmod /vendor/lib/modules/wlan.ko`, `svc wifi enable`. The
  `svc wifi disable` setting survives a reboot.

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
  vendor blob and the DSP firmware on the phone do not match. Active Edge is
  not used: it is disabled on the phone (`pm disable-user`) and bonito
  `7f1369f` drops ElmyraService from the build. bonito `3f29377` stops
  `vendor.chre` once boot completes (146 restarts in the first 14 minutes
  before); the sensors keep reporting without it.
- Play Store (`com.android.vending` 53.6.15-34) is a user app in `/data`,
  with no system Play Services or GSF. It crashed at start with
  `ForegroundServiceStartNotAllowedException` from
  `SafeSelfUpdateService` (its recovery mode); `pm clear com.android.vending`
  stopped that. A privileged system install of GApps would avoid it.
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
