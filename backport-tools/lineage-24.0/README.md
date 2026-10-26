# LineageOS 24.0 on the Pixel 3a (sargo): work in progress

Unofficial LineageOS 24.0 (Android 17) for sargo on this 4.9 kernel, started
2026-10-21 from the 23.2 port in `../lineage-23.2`. The first full build
completed on 2026-10-23 on EC2 (`lineage-24.0-20261023-UNOFFICIAL-sargo.zip`,
1,208,903,032 bytes; see `aws/`). Since 2026-10-26 the full build boots with
SELinux enforcing; boot status is in `boot-test/README.md`.

## Sources

`local_manifest-sargo.xml` goes in `.repo/local_manifests/sargo.xml` on top of
`repo init -u https://github.com/LineageOS/android.git -b lineage-24.0 --git-lfs`.
`sync-24.sh` does both, at depth 1 with `--reference` to the 23.2 tree's git
objects.

| Path | Repository | Branch |
|---|---|---|
| `device/google/bonito` | `xbill9/android_device_google_bonito` | `lineage-24.0` |
| `device/google/sargo` | `LineageOS/android_device_google_sargo` | `lineage-23.0` |
| `vendor/google/{sargo,bonito}` | `xbill9/proprietary_vendor_google_*` | `lineage-24.0` |
| `hardware/qcom/sdm845/display` | `xbill9/android_hardware_qcom_sdm845_display` | `lineage-24.0` |
| `hardware/qcom/sdm845/gps` | LineageOS | `lineage-23.2`; 24.0 dropped it |
| `hardware/qcom/audio` | `xbill9/android_hardware_qcom_audio` | `lineage-23.2` |
| `prebuilts/gcc/linux-x86/{aarch64,arm}/*-4.9` | LineageOS | `lineage-19.1`; 24.0 dropped GCC |
| `kernel/google/msm-4.9` | this repository | `main` |
| `system/core` | `xbill9/android_system_core` | `lineage-24.0` |
| `system/vold` | `xbill9/android_system_vold` | `lineage-24.0` |
| `system/bpf` | `xbill9/android_system_bpf` | `lineage-24.0` |
| `packages/modules/Connectivity` | `xbill9/android_packages_modules_Connectivity` | `lineage-24.0` |

Switching a project in the local manifest from a LineageOS repository to a
fork makes `repo sync` refuse the existing checkout ("--force-sync not
enabled; cannot overwrite a local work tree"). Once, with the checkout clean
and its commits on the fork: `repo sync -c --force-sync <paths>`.

The four `system`/`packages` forks carry the Android 17 changes for a 4.9
kernel: the legacy fscrypt session keyring (core, vold), a bpfloader that
does not fail the boot on the ring buffer test program, and NetBpfLoad,
netd and ConnectivityService changes for a kernel below 5.10.

`manifest-pinned-20261026.xml` pins every project of the tree to the commit
that built `lineage-24.0-20261026-UNOFFICIAL-sargo`, the first build that
boots with SELinux enforcing.

gs-common is not used: thermal comes from `hardware/google/pixel/thermal`.

`fetch-clang-24.sh` takes the clang prebuilts by sparse checkout at
`android-17.0.0_r1` (`clang-r596125`, `clang-r584948` and the tool
directories, 9.1 GB), as 23.2 did. The trusty module group names
`clang-r547379`, `clang-r563880c` and `clang-r584948b`; an empty
`PLACEHOLDER` file in each satisfies it.

## Changes for Android 17

### Device tree (`lineage-24.0` branch)

- Vibrator HAL (drv2624) carried in `vibrator/hal`; PixelLogger dropped.
- Thermal from `hardware/google/pixel/thermal`, with gs-common's
  `thermal_hal` sepolicy copied in.
- Power stats AIDL only and HIDL dumpstate dropped, as sunfish does.
- Lights: `android.hardware.light-service.lineage` on
  `/sys/class/backlight/panel0-backlight`; Android 17 has no HIDL lights.
- Fingerprint HAL blob patched to `lineage.frameworks.stats@1.0`, as sunfish
  does in `761f040735ee`.
- `hardware/google/pixel/power-libperfmgr` added as a Soong namespace (its
  own namespace in 17); `check_dynamic_partitions` carried in-tree (pixel
  dropped it; the retrofit OTA postinstall runs it); configstore package and
  its camera sepolicy rule dropped (17 removed the HAL and the attribute).
- `KERNEL_CROSS_COMPILE` gives the kernel the GCC 4.9 binutils and clang
  r416183b as host compiler. LineageOS 24 builds kernels with LLVM only and
  no longer sets either; 4.9 predates `LLVM=1` and links LTO with GNU gold.

### Device tree fixes found by the first full build (2026-10-23, on EC2)

Each one stopped `m bacon`; all are on bonito `lineage-24.0` up to `873fbf6`.

- `sepolicy/vendor/qcom/common/logger_app.te` removed: `unknown type logger_app`; 17 no longer
  defines it and PixelLogger is gone.
- `hardware/google/pixel-sepolicy/wifi_perf_diag` no longer added by `bonito-sepolicy.mk`:
  `unknown type wifi_logging_data_file`. Pixel adds that policy itself for eng builds only.
- `/sys/class/typec` takes the platform's `sysfs_typec` label: secilc rejects two `genfscon`
  rules for one path. `usbc0` keeps `sysfs_usb_c`; health gets `r_dir_file` on `sysfs_typec`.
- `libgui_shim_secureui` (`libshims/`): `libsecureuisvc_jni.so` needs
  `SurfaceComposerClient::createSurface` with `LayerMetadata` by value; 17 takes a const
  reference. The shim names an ordinary C++ function with the old symbol through `__asm__`
  (an `extern "C"` function returning `sp<>` is a clang error). `extract-files.py` adds it with
  `add_needed`; the vendor forks carry the patched blob (`4a9f4f1`, `8fa4a48`) and
  `proprietary-files.txt` pins both hashes.
- `manifest.xml` `target-level` 5 to 7: 17 ships framework matrices from level 7 only. The
  HIDL `bluetooth.audio@2.0`, `contexthub@1.2` and `health@2.1` are declared optional in
  `device_framework_matrix.xml`; 17's matrices list only the AIDL versions, and the framework
  still has HIDL clients for all three.
- `PRODUCT_ENFORCE_SELINUX_TREBLE_LABELING := false`: 17 enforces Treble app labeling for board
  API 202604 and later, and the vendor partition labels ims, QtiTelephonyService, HardwareInfo
  and GoogleCamera in vendor policy. The check now lists them as warnings.

Untested on the phone: the SecureUI shim, the three HIDL HALs, and the typec labels.

### sdm845 display (`lineage-24.0` branch)

The `hardware.google.light` High Brightness Mode hook is removed; it went with
the HIDL lights HAL.

### Kernel (this repository)

`777de5b0ec6f`: gold gets `LLVMgold.so` by the full path from clang, so the
LTO link does not depend on `LD_LIBRARY_PATH`.

`kernel-24-test.sh` builds the kernel outside the platform build with
LineageOS 24's kernel make flags plus the device's `KERNEL_CROSS_COMPILE`.
With stub `gcc`/`cc`/`g++`/`c++` first on PATH that fail on use, it built
`Image.lz4` (21.0 MB), the dtbs and both modules (`wlan.ko`, `lcd.ko`) in
15 min 54 s, and `headers_install` produced 884 headers.

## Build

    bash build-24-guarded.sh     # build-24.sh in its own process group, stops at 10 GB free disk

`build-24.sh` runs `lunch lineage_sargo cp2a userdebug` (the dashed form
fails on 24) and `m bacon -j8`.

On the i7-3770 with 31 GB RAM, Soong analysis takes about 1 h 45 min and
needs swap: `soong_build` held 31 GB resident with 44 GB of swap in use at the
peak, so the machine has 79 GB of swap (partition plus two swap files).
Compiling runs all 8 threads at 94-95 % user time. One run finished 130,617
of about 233,000 steps in 7 h 49 min.

## Gotchas

- `headers_install` failing does not fail the build: the header step reports
  success with an empty output, and the first sign is a compile error such as
  `unknown type name 'drm_panel_hdr_properties'` in libdrmutils.
- The `aarch64-linux-android-gcc: No such file` tracebacks from
  `gcc-wrapper.py` during header generation are harmless: that step does not
  pass the kernel's `CC`, so 4.9 probes GCC for compiler options and moves on.
- Claude Code stops background shells under memory pressure; Soong analysis
  triggers it. Set `CLAUDE_CODE_DISABLE_BG_SHELL_PRESSURE_REAP=1` or run the
  build in tmux.

## Google apps

MindTheGapps 17.0.0 ships Play Services as an APEX (`com.google.android.gmssystem`)
whose payload is EROFS, which the 4.9 kernel cannot mount (`apexd: Mounting failed ...
No such device`). Play Services then never installs and Play Store crashes looking for
the `com.google.android.gsf.gservices` provider. `repack-gapps.sh` makes a zip that
installs the same APK as `/product/priv-app/PrebuiltGmsCoreVic` with the APEX's
permission files, and removes an earlier install before the installer's space check:

    bash repack-gapps.sh MindTheGapps-17.0.0-arm64-20261022_132445.zip
    adb reboot sideload && adb sideload MindTheGapps-17.0.0-arm64-20261022_132445-sargo.zip

The zip is unsigned, so recovery asks to install anyway. On sargo the installer drops
VelvetTitan (tablets only) and needs 682 MiB of the 900 MiB `/product` reserve.
Play Services installed by hand into `/data` crashes at start (`SingletonComponentRouterProvider
requests FLAG_SINGLE_USER`), so uninstall any such copy first. An OTA installed from a
system without GApps removes them; sideload again afterwards.
