---
title: "Android 17 on a 4.9 Kernel: LineageOS 24.0 on the Pixel 3a"
published: false
description: "The Pixel 3a runs LineageOS 24.0, Android 17, on its original 4.9 kernel with SELinux enforcing and Google apps. One kernel fix, five platform forks and a set of device-tree changes, each traced to a line of a boot log."
tags: android, linux, kernel, lineageos
cover_image: https://raw.githubusercontent.com/xbill9/android_kernel_google_msm-4.9/main/backport-tools/lineage-24.0/article/devto-cover.db4df46f.jpg
---

This article provides a step by step guide to running LineageOS 24.0, which is Android 17, on a Google Pixel 3a and its original 4.9 Linux kernel. It continues the LineageOS 23.2 port on the same phone. Every patch, script and build is published.

https://github.com/xbill9/android_kernel_google_msm-4.9/releases/tag/lineage-24.0-sargo-20261027

**The Pixel 3a boots LineageOS 24.0 on kernel 4.9.337 with SELinux enforcing in 83 s, with Wi-Fi, Bluetooth, the sensors, Chrome and Google Play Services working, and updates that keep user data. Android 17 assumes a kernel of at least 5.10 in more places than Android 16 did. The port needed one kernel fix, for a cgroup BPF reference count the 4.9 backport had missed, five forks of platform code (fscrypt keys, the BPF loader, netd and Connectivity, and the native app zygote), and device-tree changes for `ueventd`, gralloc and the ION memory allocator. The `/init` logger from the 23.2 port found each one.**

---

#### What is this project trying to Do?

The Pixel 3a (codename sargo) has a Snapdragon 670 and a 4.9 kernel. Its last Google build is Android 12. LineageOS carries it officially to 22.2, and the previous article in this series brought it to 23.2, Android 16, by backporting three kernel features.

The goal here: keep the phone current and out of a landfill by running the newest LineageOS release, 24.0, on the kernel it already has.

The kernel carries a BPF backport that reports itself to Android as 5.4. The NetBpfLoad lines from the first 24.0 boot show the version Android reads, `0x504012e`, against the 5.10 it asks for:

```text
NetBpfLoad: skipping map sk_storage which requires kernel version 0x504012e >= 0x50a0000
NetBpfLoad: skipping map packet_trace_ringbuf which requires kernel version 0x504012e >= 0x50a0000
```

---

#### At This Point You Should Have…

- A Google Pixel 3a (sargo), bootloader unlocked
- The LineageOS 24.0 source with the local manifest from the release
- A build machine with room for the tree; the 24.0 tree here is 266 GB, and the home machine is an i7-3770 with 8 threads and 31 GB of RAM
- `adb` and `fastboot`
- A clone: `git clone https://github.com/xbill9/android_kernel_google_msm-4.9`

The first full build ran on a 192-core EC2 instance and the tree then moved to the home machine. That half of the work has its own article: https://dev.to/aws-builders/rent-the-cores-keep-the-phone-building-lineageos-24-on-ec2-and-iterating-at-home-4pmc

---

#### Step 1 — Carry the 23.2 Tree to 24.0

The device tree starts from the 23.2 port, on a new `lineage-24.0` branch of the bonito fork. Android 17 dropped several HIDL HALs and moved a few Pixel components, so the tree changed in the same places the Pixel 4a (sunfish) did:

- vibrator HAL (drv2624) carried in the tree; thermal from `hardware/google/pixel/thermal`
- AIDL lights on `/sys/class/backlight/panel0-backlight`, since Android 17 has no HIDL lights
- power stats AIDL only, HIDL dumpstate dropped
- the fingerprint blob patched to `lineage.frameworks.stats@1.0`, as sunfish does
- `KERNEL_CROSS_COMPILE` with the GCC 4.9 binutils: LineageOS 24 builds kernels with LLVM only, and 4.9 predates `LLVM=1` and links LTO with GNU gold

The full build then stopped on six more device-tree errors, among them `unknown type logger_app`, two `genfscon` rules for `/sys/class/typec`, and a framework compatibility matrix that starts at level 7. A small shim keeps the SecureUI blob linking against Android 17's `SurfaceComposerClient::createSurface`. The list, with commits, is in the 24.0 README.

The lunch form changed too: `lunch lineage_sargo cp2a userdebug`, with spaces. The first complete package:

```text
lineage-24.0-20261023-UNOFFICIAL-sargo.zip  1,208,903,032 bytes
```

---

#### Step 2 — Read the Boot With the /init Logger

The 23.2 port built a 4 KB static `/init`, `kmsglog.c`, that execs the real init as process 1 and copies the kernel log to the `metadata` partition. For 24.0 it gained three things:

- it also appends logcat to `/metadata/logcat.txt`, by entering init's root through `/proc/1/root`
- `kmsglog.reboot=<s>` on the kernel command line dumps every blocked task and reboots to the bootloader after that many seconds, so a hung boot ends by itself
- the log file is truncated each boot and wraps at 2 MB

`add-logger.sh` repacks any boot image with it:

```bash
EXTRA_CMDLINE="androidboot.selinux=permissive kmsglog.reboot=150" \
  ./add-logger.sh boot.img boot-log.img
```

`test-boot-b.sh` RAM-boots the image on slot b, waits, and pulls `/metadata/boot-kmsg.txt` and `/metadata/logcat.txt` from the fallback system. The flashed slot keeps a working build, so a failed test falls back to it with no hands on the phone.

🔎 **Tip: `/metadata` is only 10 MB.** It also holds vold's keys and the feature flags in `/metadata/aconfig`, so the logger caps the kernel log at 2 MB, rotates logcat at 1 MB times two, and the test script deletes the logs after pulling them.

---

#### Step 3 — ueventd

The first permissive boot shows keymaster, gatekeeper, citadeld and surfaceflinger aborting. Their device nodes, `/dev/qseecom`, `/dev/citadel0` and `/dev/kgsl-3d0`, still have root-only permissions.

Android 17's ueventd reads only `/system/etc/ueventd.rc`, which imports `/vendor/etc/ueventd.rc`. The device installed its rules to the old `/vendor/ueventd.rc`, which ueventd no longer parses. Bonito `31b5a72` installs them to `vendor/etc`.

---

#### Step 4 — Gralloc

Next, the composer HAL aborts:

```text
F GraphicBufferMapper: gralloc-mapper is missing
F DEBUG   : Abort message: 'gralloc-mapper is missing'
```

Android 17's `libui` loads only IMapper 4.0 and newer on API 36 and later. The sdm845 display stack provides IMapper 2.1. `libui` still has the older path behind `LEGACY_GRALLOC`, and bonito `433912b` builds it with that flag through Soong config.

---

#### Step 5 — The fscrypt Session Keyring

With a full build flashed, the phone goes to recovery 4.9 s into the boot:

```text
E vold    : Failed to install fscrypt key to /data: Inappropriate ioctl for device
E init: Exec service failed, status 25: Rebooting into recovery, reason: enablefilecrypto_failed
```

The 4.9 kernel has no `FS_IOC_ADD_ENCRYPTION_KEY`, so file encryption keys go into a keyring that every process shares through a session keyring. Android 17 removed that path from both sides: init no longer joins a session keyring, and vold no longer installs keys into it.

Two forks bring it back from 23.2:

- `system/core` `5742820`: init joins a session keyring at second stage, and `mount_all` creates the `fscrypt` keyring on encrypted devices
- `system/vold` `fce7dad`: `isFsKeyringSupported()`, `installKeyLegacy()`, `evictKeyLegacy()` and the cache drop after eviction, with inline `keyctl` wrappers since Android 17 removed `libkeyutils`

🔎 **Tip: do not accept the factory reset.** Recovery offers one after `enablefilecrypto_failed`. The data on `/data` is intact, and the other slot still boots it.

---

#### Step 6 — BPF on a Kernel That Reports 5.4

Android 17's network stack loads its BPF programs per kernel version, and several now need 5.8 or 5.10. Each missing one stopped the boot in a different place:

```text
init: Reboot start, reason: reboot,bpfloader-failed, reboot_target: bpfloader-failed
E NetdUpdatable: libnetd_updatable_init: Failed: Failed to get program from /sys/fs/bpf/netd_shared/prog_netd_egress_stats: No such file or directory
```

Two forks handle them:

- `system/bpf` `72b37ef`: `bpfRingbufProg` is a test program that needs BPF ring buffers, from 5.8. It is marked critical, so its load failure rebooted userdebug builds. It is now non-critical.
- `packages/modules/Connectivity` `5c298de`: NetBpfLoad warns below 5.10, as it did in 23.2; it hands off to `uprobestatsbpfload` only on 5.8 and later; the 5.4 ingress and egress stats programs in `netd.c` extend to the newest API level, since Android 17 has none below 5.10; and ConnectivityService skips the two ring-buffer listeners when their maps are absent, which aborted system_server.

---

#### Step 7 — netd Stuck in rcu_barrier

With the forks in, about one permissive boot in five reaches the boot animation and stops. The logger's task dump shows netd blocked in the kernel:

```text
[<00000000edff5dad>] _rcu_barrier+0x210/0x2b0
[<000000007d48fdfd>] netdev_run_todo+0xc4/0x9b4
[<00000000610d42d3>] rtnetlink_rcv+0x58/0x78
[<000000004b18ba16>] netlink_unicast+0x2b0/0x468
[<000000004bbdc794>] netlink_sendmsg+0x4bc/0x604
netd            S    0  1595      1 0x00000000
```

system_server waits for netd until its watchdog kills it, and the cycle repeats. The kernel log of these boots carries one warning that names the cause:

```text
percpu ref (cgroup_bpf_release_fn) <= 0 (0) after switching to atomic
```

`cgroup_sk_clone()` reached this tree from 4.9-stable before the tree had a `cgroup_bpf` reference count. The BPF backport added `cgroup_bpf_get()` to `cgroup_sk_alloc()` and `cgroup_bpf_put()` to `cgroup_sk_free()`, and left the clone path out. Every socket made by `accept()` dropped a reference it never took. When a cgroup was removed, its BPF programs were freed while sockets in it could still run them.

Kernel `e7a12e10fc29` is one line: take the reference, as upstream does since `ad0f75e5f57c`. Before the fix 2 of 9 boots hung; after it 8 of 8 booted.

---

#### Step 8 — ION Under SELinux Enforcing

Permissive boots reached the home screen. Enforcing boots stopped at a black screen with no USB. The permissive logs show the reason, from 21 domains:

```text
avc:  denied  { read } for  comm="qseecomd" name="ion" dev="tmpfs" ino=12110 scontext=u:r:tee:s0 tcontext=u:object_r:device:s0 tclass=chr_file permissive=1
```

Android 17 removed `/dev/ion` from the platform `file_contexts` and every `ion_device` rule from the platform policy. This kernel has ION and no DMA-BUF heaps. Three bonito commits put it back in vendor policy:

- `2cbfb4b`: label `/dev/ion` as `ion_device`
- `e2ca4b1`: the Android 16 `ion_device` rules for surfaceflinger, keymaster, gatekeeper, gralloc and qseecomd
- `a6e99a3`: four more accesses an enforcing boot denies, for the sensors, Wi-Fi, USB gadget and power HALs

Three boots from flash with all three took 81, 48 and 80 s, with Wi-Fi up each time.

---

#### Step 9 — The Native App Zygote

The phone booted, but Chrome exited with `Timed out waiting for GPU channel` and then every app launch stalled for minutes. Chrome 156 runs its renderers in a native app zygote, and the zygote died on start:

```text
Failed to check capability bound: GetCapBoundFailure(EINVAL)
```

Android 17's `zygote_next` drops the capability bounding set before it forks apps, and it reads every capability up to 40. Linux 5.9 added `CAP_PERFMON`, `CAP_BPF` and `CAP_CHECKPOINT_RESTORE` (38 to 40); 4.9 returns `EINVAL` for them. A capability the kernel does not know cannot be in the bounding set, so the `system/zygote` fork, `d6dadb5`, skips it, as the C++ zygote already does. Chrome opens pages and its sandboxed renderers start.

---

#### Step 10 — CHRE and Active Edge

One service still restarted about every 5 s, 146 times in the first 14 minutes:

```text
dynamic loading failed for chre_slpi on domain 0 (dlerror undefined symbol #25 __sensors_island_start in ./libchre_slpi_skel.so)
```

The sensor DSP firmware on the phone rejects the CHRE program from the vendor blobs. Its only user is Active Edge, the squeeze gesture. Bonito `7f1369f` drops ElmyraService, and `3f29377` stops `vendor.chre` once boot completes. The accelerometer and light sensor keep reporting without it.

---

#### Step 11 — Google Apps

MindTheGapps 17.0.0 ships Play Services as an APEX whose payload is EROFS, a filesystem the 4.9 kernel cannot mount:

```text
apexd: Mounting failed ... No such device
```

`repack-gapps.sh` rebuilds the zip with the same APK installed as `/product/priv-app/PrebuiltGmsCoreVic`, with the APEX's permission files. On sargo it needs 682 MiB of the 900 MiB `/product` reserve.

Flashed onto a phone that is already set up, Google apps never get their default runtime permissions, and Play Services refuses location requests. `post-install-24.sh` grants them, then runs the background compile job. After an update every app sits at the `verify` filter until the phone is idle and charging. Gmail, one run each:

| Gmail | Start | Janky frames scrolling |
| :--- | :--- | :--- |
| Uncompiled (`verify`) | 1,372 ms | 3.37% |
| Compiled (`speed-profile`) | 🥇 1,010 ms | 🥇 0.09% |

The compile job took 2 min 53 s.

---

#### Step 12 — Install and Update Without Wiping

`update_engine` installs a full build into the inactive slot from the running system and keeps `/data`:

```text
10:59:56 update_engine install to the inactive slot
[INFO:update_engine_client_android.cc(102)] onPayloadApplicationComplete(ErrorCode::kSuccess (0))
[INFO:update_engine_client_android.cc(94)] onStatusUpdate(UPDATE_STATUS_UPDATED_NEED_REBOOT (6), 0)
[INFO:update_engine_client_android.cc(367)] Command took 300501 ms
11:04:57 installed; not rebooting
```

On a build machine, `phone-update-24.sh` syncs, builds, installs, sideloads the repacked Google apps and compiles the apps. On a laptop with only `adb`, `phone-install-24.sh` does the same from the release zip; `INSTALL.md` has the steps. The one manual step is in recovery: accept the test-key signature warning, then reboot.

The phone after the update:

```text
932AY05KVV             device usb:5-2 product:sargo model:Pixel_3a device:sargo transport_id:34
24.0-20261027-UNOFFICIAL-sargo
4.9.337-gf002f96f2646
Enforcing
1
_b
17
2026-10-01
Wifi is enabled
```

🔎 **Tip: list every fork in the local manifest.** `repo sync` resets any project the local manifest does not replace to LineageOS upstream, and a build from that tree loses the fork commits. On this phone that build went straight back to `enablefilecrypto_failed`. `update-forks.sh` reports how far each fork is behind upstream and rebases it.

---

#### Compare and Contrast

| | LineageOS 23.2 on this kernel | LineageOS 24.0 on this kernel |
| :--- | :--- | :--- |
| Android | 16 | 🥇 17 |
| Kernel changes | three backports from 4.14 and 4.17 | 23.2's plus one cgroup BPF fix |
| Platform forks | none | five: core, vold, bpf, Connectivity, zygote |
| SELinux | 🟢 enforcing | 🟢 enforcing, with ION rules in vendor policy |
| Google apps | 🟢 MindTheGapps 16 | ⚠️ MindTheGapps 17, repacked |
| Active Edge | ⚠️ not tested | ❌ off |
| Signing | ⚠️ test keys | ⚠️ test keys |

---

#### So, Which One?

For a spare Pixel 3a, 24.0 is the current one: Android 17, the October 2026 security patch level, and the same update path that keeps your data. Back up before moving from 23.2: the update tools here install over 24.0 builds only, and a first install wipes the phone.

---

#### Summary

The goal of this project was to run Android 17 on the Pixel 3a's original 4.9 kernel. The key to the solution was the same `/init` logger as the 23.2 port, now with logcat and a task dump on a timed reboot, which turned each stopped boot into a log line. The results were:

- 🟢 LineageOS 24.0 boots on kernel 4.9.337 with SELinux enforcing in 83 s and installs over the previous build, keeping user data.
- 🟢 One kernel fix: `cgroup_sk_clone()` takes its `cgroup_bpf` reference. 2 of 9 boots hung before it; 8 of 8 booted after.
- 🟢 Five platform forks cover a kernel below 5.10: the fscrypt session keyring, the BPF loader, netd and Connectivity, and the native app zygote.
- 🟢 Wi-Fi connects, Bluetooth turns on, the sensors report, Chrome renders pages.
- 🟢 Play Services runs as a system app from repacked MindTheGapps 17, and network location gives a fix.
- 🟢 Compiling apps after an update brings Gmail from 1,372 ms to 1,010 ms to start.
- ⚠️ Signed with test keys; recovery shows a signature warning on every install.
- ⚠️ Calls, SMS, mobile data, audio by ear, taking photos, fingerprint enrolment, NFC payments and satellite GPS are untested.
- ❌ Active Edge is off: the sensor DSP firmware rejects the CHRE program.

Scope: one Pixel 3a (sargo, 932AY05KVV) with no SIM fitted, LineageOS 24.0 built 2026-10-27 from bonito `lineage-24.0` at `3f29377` and the five `lineage-24.0` forks, kernel 4.9.337 from this repository. The hang counts come from 9 and 8 boots of one phone; the Gmail figures are one run each. Parts of the analysis and writing were done with AI assistance (Claude); every log line comes from the phone or the build machine.

The strategy for running Android 17 on a 4.9 kernel was validated with an incremental step by step approach.

---

#### References

- Release, build and kernel source: https://github.com/xbill9/android_kernel_google_msm-4.9/releases/tag/lineage-24.0-sargo-20261027
- Build, install and boot-test notes: https://github.com/xbill9/android_kernel_google_msm-4.9/tree/main/backport-tools/lineage-24.0
- Install from a laptop: https://github.com/xbill9/android_kernel_google_msm-4.9/blob/main/INSTALL.md
- Device tree: https://github.com/xbill9/android_device_google_bonito/tree/lineage-24.0
- Fork, system/core: https://github.com/xbill9/android_system_core
- Fork, system/vold: https://github.com/xbill9/android_system_vold
- Fork, system/bpf: https://github.com/xbill9/android_system_bpf
- Fork, Connectivity: https://github.com/xbill9/android_packages_modules_Connectivity
- Fork, system/zygote: https://github.com/xbill9/android_system_zygote
- The 23.2 port: https://dev.to/gde/android-16-on-a-49-kernel-lineageos-232-on-the-pixel-3a-5e79
- The EC2 build: https://dev.to/aws-builders/rent-the-cores-keep-the-phone-building-lineageos-24-on-ec2-and-iterating-at-home-4pmc
- MindTheGapps 17.0.0 arm64: https://github.com/MindTheGapps/17.0.0-arm64
