---
title: "Android 16 on a 4.9 Kernel: LineageOS 23.2 on the Pixel 3a"
published: false
description: "LineageOS ships 4.9-kernel phones only up to 22.2. The Pixel 3a runs 23.2 on its 4.9 kernel with three backported kernel features and two device fixes, each found from one line of a boot log."
tags: android, linux, kernel, lineageos
cover_image: https://raw.githubusercontent.com/xbill9/android_kernel_google_msm-4.9/main/backport-tools/lineage-23.2/devto-cover.9f1689a3.jpg
---

This article provides a step by step guide to running LineageOS 23.2, which is Android 16, on a Google Pixel 3a and its original 4.9 Linux kernel. Every patch, script and build is published.

https://github.com/xbill9/android_kernel_google_msm-4.9/releases/tag/lineage-23.2-sargo-20261021

**LineageOS accepts 4.4 and 4.9 kernel phones for official builds only up to 22.2, Android 15. The Pixel 3a boots 23.2 on its 4.9 kernel with SELinux enforcing, Wi-Fi, both cameras and updates that keep user data. Android 16 needed three things the kernel lacked, each one stopping the phone before Android started: `MADV_WIPEONFORK`, a SELinux policy format and a cgroup mount option. Two more fixes in the device tree brought back the camera and netd. A logger that replaces `/init` found each one in a single boot, about two minutes per test, with no hands on the phone.**

---

#### What is this project trying to Do?

The Pixel 3a (codename sargo) has a Snapdragon 670 and a 4.9 kernel. Its last Google build is Android 12, `SP2A.220505.008`. LineageOS carried it to 22.2.

The goal here: keep the phone current and out of a landfill by running the newest LineageOS release that builds for it, on the kernel it already has.

LineageOS states the limit in its 24.0 announcement:

> even though some devices using Linux Kernel versions 4.4 and 4.9 have been ported to LineageOS 23.2 and 24.0, we have decided to only accept LineageOS 22.2 submissions for these devices.

The same post says 23.2 and 24.0 ship only kernels with "1:1 eBPF backports". This kernel already carries a BPF backport at the 5.4 level, from earlier work on this repository.

---

#### At This Point You Should Have…

- A Google Pixel 3a (sargo), bootloader unlocked, running LineageOS 22.2
- A Linux build machine; this one is an i7-3770 with 8 threads, 32 GB of RAM and 98 GB of free disk after the 23.2 tree
- The LineageOS 23.2 source with the local manifest from the release
- `adb` and `fastboot`
- A clone: `git clone https://github.com/xbill9/android_kernel_google_msm-4.9`

---

#### Step 1 — Build 23.2 for a Device With No 23.2 Branch

The newest sargo device trees are `lineage-23.0`, and the vendor blobs are the 22.2 ones. The local manifest pins those and points the kernel at this repository.

The device tree needed Android 16 adaptations, most of them the same ones LineageOS made for the Pixel 4a (sunfish) between 23.0 and 23.2:

- new Soong namespaces and config flags for the split Qualcomm display, GPS and audio repositories
- the HIDL power-stats HAL dropped and storage health moved to the AIDL service, since Android 16's compatibility matrices list neither HIDL HAL
- the product partition reserve cut from 1000 to 900 MiB: Android 16's images were 34.9 MB over the super partition
- `KERNEL_CC := CC=clang`, which keeps the 4.9 Makefile's LTO link with GNU gold

```text
#### build completed successfully (47:58 (mm:ss)) ####
-rw-rw-r-- 1 xbill xbill 1185210476 17:15 lineage-23.2-20261021-UNOFFICIAL-sargo.zip
```

The two largest prebuilt repositories, clang and Rust, are over 110 GB as a full fetch. A partial clone with a sparse checkout of the versions the build uses brings clang to 9.9 GB; the script is in the release.

---

#### Step 2 — Read a Boot That Dies Before adb

The first 23.2 boot shows the Google logo and drops to the bootloader. After three tries the bootloader marks the slot unbootable:

```text
current-slot: b
slot-unbootable:a: yes
slot-retry-count:a: 0
```

A failure this early happens before adb, before logcat and before anything writes to `/data`. The usual crash log, pstore, comes back empty on sargo: Pixel encrypts it with a key kept in `/data`, and a marker written to the kernel log before a plain `adb reboot` is gone after it.

The logger is a 5 KB static `/init`, `kmsglog.c`, written with raw system calls and no C library. It forks, execs the real init as process 1, and the child copies the kernel log to the `metadata` partition with a synced write for every batch. The child runs in its own mount namespace, so its mounts stay out of init's way, and it holds file descriptors instead of paths, because init deletes the ramdisk when it switches root.

`add-logger.sh` puts it into any boot image:

```bash
KERNEL=kout/arch/arm64/boot/Image.lz4 EXTRA_CMDLINE=androidboot.selinux=permissive \
  ./add-logger.sh boot.img boot-log.img
```

A boot of 22.2 with the logger checks it first:

```text
[    1.222412] kmsglog: started, kmsg fd 0
[    1.225845] kmsglog: metadata minor 34
[    1.230139] kmsglog: mount metadata 0
[    1.230148] kmsglog: log file fd 3
-rw-r--r-- 1 root root 603490 2026-10-21 14:42 /metadata/boot-kmsg.txt
```

---

#### Step 3 — Make Every Test Hands-Free

Three settings take the person out of the loop:

- `fastboot oem off-mode-charge 0`. With the cable in, a phone that powers off starts again instead of waiting on the charging screen.
- Test images boot from RAM with `fastboot boot`, and the flashed slot keeps a working image, so a failed test falls back to it by itself.
- `test-boot.sh` watches the USB ID, compares `/proc/version` to tell the tested kernel from the fallback, and pulls `/metadata/boot-kmsg.txt`.

```text
15:14:35 booting flashed slot a
15:14:56 landed in bootloader; rebooting slot b
15:15:24 adb up: lineage 22.2-20261017-NIGHTLY-sargo, slot _b
/metadata/boot-kmsg.txt: 1 file pulled, 0 skipped.
```

One test takes about two minutes. A kernel build takes 16 to 20 minutes, most of it the single-threaded LTO link.

🔎 **Tip: keep the fallback on the same Android version.** Android 16 upgrades `/data` on its first boot. Booting the 22.2 slot afterwards stops on recovery's "Can't load Android system. Your data may be corrupt" screen. Once 23.2 has run, both slots should hold 23.2, and tests should boot from RAM.

---

#### Step 4 — MADV_WIPEONFORK

The first log:

```text
e2fsck: libc: arc4random data MADV_WIPEONFORK failed: Invalid argument
e2fsck: e2fsck terminated by signal 6
```

Android 16's C library marks its random-number state with `madvise(MADV_WIPEONFORK)`, so a forked child never reuses the parent's random numbers. The flag arrived in Linux 4.14. On 4.9 the call fails, the library aborts the process, and every Android 16 program dies the same way; e2fsck is simply the first to run.

The port is upstream `d2cd9ede6e19` with its prerequisite `df3735c5b40f`, which frees the VMA flag bit, and the later fix `93949bb21b52`. Three hunks needed fitting to 4.9's `dup_mmap()` and `madvise_behavior_valid()`.

---

#### Step 5 — The SELinux Policy

The next log stops at policy load:

```text
SELinux: 16384 avtab hash slots, 56785 rules.
SELinux:  Android master kernel running Android M policy in compatibility mode.
SELinux: avtab: invalid type or class
SELinux: failed to load policy
init: InitFatalReboot: signal 6
```

Android 16's policy has rules on netlink message types, such as `allowxperm netdomain self:netlink_route_socket nlmsg ...`. The compiler writes them as extended permissions of a new type, `0x03`, even at policy version 30. This kernel carries a compatibility path for Android M policies, and it took any extended-permission type other than the two ioctl types for the old M layout. It read the rest of the table at the wrong offsets.

Upstream reads these rules from Linux 6.13 on, under the `netlink_xperm` policy capability. A 4.9 kernel checks netlink messages through the older `nlmsg_read` and `nlmsg_write` permissions, and Android 16's policy still grants those:

```text
allow netdomain self:netlink_route_socket { create read ... nlmsg_read nlmsg };
```

So the fix, `bf173c3ec796`, reads type `0x03` in the normal layout and drops the rule.

---

#### Step 6 — cpuset_v2_mode

The third log reaches init's cgroup setup:

```text
libprocessgroup: Failed to mount controller cpuset: No such file or directory
init: Command 'SetupCgroups' action=SetupCgroups took 2ms and failed
init: Got shutdown_command 'reboot,bootloader,bootstrap-apexd-failed'
```

Android 16 mounts the cpuset controller with `noprefix,cpuset_v2_mode`. The second option arrived in Linux 4.17, and the mount fails without it. With no cgroups, no service can start.

The port is upstream `e1cba4b85daa` for the mount option and `b8d1b8ee93df` for the cpuset behaviour, plus two recent upstream fixes, `e06b678e3b4e` and `b8eb5fd5bdb4`. The fourth boot reached the home screen:

```text
16:04:19 adb up: lineage 23.2-20261021-UNOFFICIAL-sargo, slot _a
23.2-20261021-UNOFFICIAL-sargo
4.9.337-g416f6a9fd740
```

---

#### Step 7 — The Camera

The camera provider crashes on every start, five seconds apart:

```text
#00 pc 000000000033dda8  /vendor/lib64/hw/camera.sdm710.so (CamX::TuningDataManager::GetChromatix()+0)
#01 pc 00000000002a753c  /vendor/lib64/hw/camera.sdm710.so (CamX::ImageSensorUtils::GetTunedColorCorrectionData(...)+48)
```

`camera.sdm710.so` and two image stabilisation modules link against `libtinyxml2.so`. On Android 16 that is a newer tinyxml2 than the blobs were built for, and the provider crashes reading its tuning data. The blobs ship an older `libtinyxml2_1.so`, which lacks a function the camera needs:

```text
dlopen failed: cannot locate symbol "_ZN8tinyxml211XMLDocument13ErrorIDToNameENS_8XMLErrorE"
```

LineageOS already ships `libtinyxml2-v34`, a copy from the VNDK v34 libraries, in `hardware/lineage/compat`. Pointing the three blobs at it fixes the camera:

```bash
patchelf --replace-needed libtinyxml2.so libtinyxml2-v34.so camera.sdm710.so
```

```text
CameraProviderManager: Camera provider legacy/0 ready with 2 camera devices
```

The device tree carries the same change as a blob fixup in `extract-files.py`.

---

#### Step 8 — netd

netd aborts once on every boot and runs after init restarts it. The kernel log shows the order:

```text
17:58:38.143 init: starting service 'netd'...
17:58:38.191 init: starting service 'bpfloader'...
17:58:38.281 F libc: Fatal signal 6 (SIGABRT) in tid 1171 (netd)
17:58:38.869 init: processing action (bpf-progs-loaded)
17:58:38.869 init: starting service 'netd'...
```

netd starts 48 ms before the BPF loader, and its updatable network library aborts without the BPF maps. The device's own `init.hardware.rc` starts netd early, in `post-fs-data`. Android 16 starts it on `bpf-progs-loaded`. With the early start removed, netd starts once and stays up.

---

#### Step 9 — Update Without Wiping

`update_engine` installs a full build into the inactive slot from a file on the phone and keeps `/data`:

```bash
update_engine_client --update --payload=file:///data/ota_package/update.zip \
  --offset=4042 --size=1185202940 --headers="$H"
```

The offset and size locate `payload.bin` inside the zip, and the headers come from `payload_properties.txt`, one per line. `build-and-ota.sh` builds, installs and checks:

```text
18:22:43 applying
18:27:20 applied
slot=_a
23.2-20261021-UNOFFICIAL-sargo
4.9.337-g50d897667cc3
Enforcing
boot_completed=1
Number of camera devices: 2
crashes:
Wifi is enabled
slot_a_successful_exit=0
```

🔎 **Tip: `update_engine_client` needs `--update`.** Without it the command exits 0 and installs nothing.

---

#### Compare and Contrast

| | LineageOS 22.2 | LineageOS 23.2 on this kernel |
| :--- | :--- | :--- |
| Android | 15 | 🥇 16 |
| Official builds for sargo | 🟢 yes | ❌ no, unofficial |
| Signing | 🟢 LineageOS keys | ⚠️ test keys |
| Kernel | 4.9 | 4.9 with three Android 16 backports |
| Wi-Fi, camera, recovery | 🟢 tested by LineageOS | 🟢 tested here |
| Calls, Bluetooth, audio, GPS | 🟢 tested by LineageOS | ⚠️ not tested |

---

#### So, Which One?

For a phone that has to work every day, stay on 22.2: it is official and signed, and LineageOS merges security patches into it monthly until Google stops publishing Android 15 bulletins. For a spare Pixel 3a, 23.2 runs on the phone's own kernel today, with the core of the phone working and updates that keep your data.

---

#### Summary

The goal of this project was to run Android 16 on the Pixel 3a's original 4.9 kernel. The key to the solution was a logger that replaces `/init`, which turned each silent boot failure into one line of kernel log. The results were:

- 🟢 LineageOS 23.2 boots on kernel 4.9.337 with SELinux enforcing, and the slot is marked successful.
- 🟢 Three kernel features made the difference: `MADV_WIPEONFORK` from 4.14, `cpuset_v2_mode` from 4.17, and reading Android 16's netlink policy rules.
- 🟢 Wi-Fi connects and validates; both cameras work after the blobs move to `libtinyxml2-v34`.
- 🟢 netd starts once, after the BPF loader, with no crash.
- 🟢 Updates install over the running system in under 5 minutes and keep user data.
- 🟢 One boot test runs hands-free in about two minutes.
- ⚠️ Signed with test keys; recovery shows a signature warning on every install.
- ⚠️ Calls, mobile data, Bluetooth, audio, GPS, NFC and the fingerprint reader are untested.
- ❌ Once 23.2 has run, a 22.2 slot cannot boot the upgraded `/data`.

Scope: one Pixel 3a (sargo) with no SIM fitted, LineageOS 23.2 built 2026-10-21 from `lineage-23.0` device trees and `lineage-22.2` vendor blobs, kernel `4.9.337-g50d897667cc3`. Parts of the analysis and writing were done with AI assistance (Claude); every log line comes from the phone or the build machine.

The strategy for running Android 16 on a 4.9 kernel was validated with an incremental step by step approach.

---

#### References

- Release, build and kernel source: https://github.com/xbill9/android_kernel_google_msm-4.9/releases/tag/lineage-23.2-sargo-20261021
- Build, install and boot-debug notes: https://github.com/xbill9/android_kernel_google_msm-4.9/tree/main/backport-tools/lineage-23.2
- Device tree: https://github.com/xbill9/android_device_google_bonito
- LineageOS Changelog 32: https://lineageos.org/Changelog-32/
- MindTheGapps 16.0.0 arm64: https://github.com/MindTheGapps/16.0.0-arm64
