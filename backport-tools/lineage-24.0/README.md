# LineageOS 24.0 on the Pixel 3a (sargo)

Unofficial LineageOS 24.0 (Android 17) for sargo on this repository's 4.9 kernel. Builds are
published as [GitHub releases](https://github.com/xbill9/android_kernel_google_msm-4.9/releases);
the one marked Latest has passed the hardware check on a phone. Current test results:
`boot-test/README.md`.

## One command: `sargo`

| Command | Does | Time |
|---|---|---|
| `sargo update` | pulls LineageOS (rebasing the forks), commits the upstream kernel fixes that apply as is, builds the ROM with this kernel tree, RAM-boots the ROM's kernel on the plugged-in phone and runs every test (nothing flashed). On PASS it pushes the kernel and publishes the ROM as a prerelease; on FAIL nothing is pushed or published | 1 to 3 h |
| `sargo install` | installs the newest release on the phone (data kept), puts Google apps back, compiles apps, runs the hardware check and marks the release Latest if it passes; `sargo install tested` takes the Latest one | 15 min, no taps (builds with Google apps built in); `sargo install --all` does every phone in `phones.conf` |
| `sargo test` | tests a kernel change in progress without building the ROM: builds the kernel at HEAD and runs the same tests; `--push` pushes on PASS | 15 min build + 22 min test |

A kernel or Android change: commit it (Android changes to the fork, pushed), then `sargo update`,
then `sargo install`. Upstream fixes that need a hand merge are listed by `sargo update` (from
`backport-tools/check-upstream.sh`); port them or record them in `backport-tools/fixes-done.tsv`.

`~/bin/sargo` links to `sargo.sh` here. Run `sargo update` in tmux or with `setsid nohup`: a
Claude Code background shell is stopped under memory pressure during Soong analysis. Logs:
`~/sargo-bpf-test/logs/build-24.log`; test reports `~/sargo-logs/<serial>-test-*/summary.txt`;
one line per test in `boot-test/results.tsv`.

New build machine: `BUILD.md`. A laptop that only installs, or a first install: `INSTALL.md`
at the top of the repository (it sets up `sargo-install`, the same as `sargo install`).

## Files

| File | Does |
|---|---|
| `sargo.sh` | the `sargo` command; `sargo install` runs `phone-install-24.sh`, `sargo test` runs `sargo-test.sh` |
| `phones.conf` | the phones the test runs on at once, and which checks each runs (one on this machine, the others on SSH hosts) |
| `sargo-lock.sh` | lets one `sargo` command run at a time (they share the tree and the phone) |
| `kbuild.sh` | builds the kernel outside the platform build, with the tree's prebuilts and the ROM's kernel config (used by `sargo test`) |
| `update-forks.sh` | reports how far each fork is behind upstream; `--apply` rebases and pushes (used by `sargo update`) |
| `local_manifest-sargo.xml` | the local manifest (below) |
| `setup-24.sh`, `fetch-clang-24.sh` | first-time tree setup on a build machine |
| `sargo-setup.sh` | first-time setup of a laptop that only installs |
| `repack-gapps.sh`, `remove-repacked-gapps.sh` | GApps for builds before 2026-11-09 (below) |
| `boot-test/` | the tests `sargo update` and `sargo test` run, and the test history |
| `aws/` | building on EC2 |
| `PORTING-NOTES-mobile-data.md` | how mobile data was brought up on the Fi SIM |

## Sources

`local_manifest-sargo.xml` goes in `.repo/local_manifests/sargo.xml` on top of
`repo init -u https://github.com/LineageOS/android.git -b lineage-24.0 --git-lfs`.
`setup-24.sh` does both, at depth 1, and fetches the clang prebuilts.

| Path | Repository | Branch |
|---|---|---|
| `device/google/bonito` | `xbill9/android_device_google_bonito` | `lineage-24.0` |
| `device/google/sargo` | `xbill9/android_device_google_sargo` | `lineage-24.0` |
| `vendor/google/{sargo,bonito}` | `xbill9/proprietary_vendor_google_*` | `lineage-24.0` |
| `hardware/qcom/sdm845/display` | `xbill9/android_hardware_qcom_sdm845_display` | `lineage-24.0` |
| `hardware/qcom/sdm845/gps` | `xbill9/android_hardware_qcom_sdm845_gps` | `lineage-24.0` |
| `hardware/qcom/audio` | `xbill9/android_hardware_qcom_audio` | `lineage-24.0` |
| `prebuilts/gcc/linux-x86/{aarch64,arm}/*-4.9` | `xbill9/android_prebuilts_gcc_linux-x86_*-4.9` | `lineage-24.0` |
| `kernel/google/msm-4.9` | this repository | `main` |
| `system/core` | `xbill9/android_system_core` | `lineage-24.0` |
| `system/vold` | `xbill9/android_system_vold` | `lineage-24.0` |
| `packages/modules/Connectivity` | `xbill9/android_packages_modules_Connectivity` | `lineage-24.0` |

Every project in the local manifest is on a `lineage-24.0` branch, the kernel
on `main`. LineageOS has no `lineage-24.0` branch for the sargo device tree,
the sdm845 GPS HAL, the audio HAL or the GCC 4.9 prebuilts (24.0 has no
sdm845-family device and no GCC), so each is an xbill9 fork whose
`lineage-24.0` branch starts at the LineageOS commit the build used before.

Switching a project in the local manifest from a LineageOS repository to a
fork makes `repo sync` refuse the existing checkout ("--force-sync not
enabled; cannot overwrite a local work tree"). Once, with the checkout clean
and its commits on the fork: `repo sync -c --force-sync <paths>`.

`repo sync` brings upstream changes to every project except the forks; `sargo update` rebases
the forks' own commits onto their upstream (LineageOS `lineage-24.0`, or the AOSP tag of the
`aosp` remote) with `update-forks.sh --apply`, which force-pushes each fork with a lease on its
old tip. A conflict stops the build and leaves the fork as it was.

The `system`/`packages` forks carry the Android 17 changes for a 4.9
kernel: the legacy fscrypt session keyring (core, vold), and NetBpfLoad,
netd and ConnectivityService changes for a kernel below 5.10.

`system/bpf` and `system/zygote` are LineageOS and AOSP again from
`24.0-20261109` (tested 2026-11-10: bpfloader exits 0, `zygote_next` starts
apps, smoke test 19 pass, 0 fail): the kernel now has BPF ring buffers (bpfloader
creates its `test_ringbuf` map) and capabilities 38-40 (PERFMON, BPF,
CHECKPOINT_RESTORE), which `zygote_next` drops from every app's bounding
set. Switching back needs, once, with clean checkouts:
`repo sync -c --force-sync system/bpf system/zygote`.

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

`777de5b0ec6f`: gold gets `LLVMgold.so` by the full path from clang, so the LTO link does not
depend on `LD_LIBRARY_PATH`. `kbuild.sh` builds outside the platform build with the same
toolchain and stub `gcc`/`cc`/`g++`/`c++` first on PATH, which fail if anything falls back to
the host compiler.

## Build

`sargo.sh` runs `lunch lineage_sargo cp2a userdebug` (the dashed form fails on 24) and
`m bacon -j$(nproc)`. Soong analysis needs a lot of memory: on the i7-3770 with 31 GB RAM,
`soong_build` held 31 GB resident with 44 GB of swap in use at the peak, so that machine has
79 GB of swap. An incremental build after a sync took 43 min on 2026-11-15.

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

Builds from 2026-11-18 on have them built in: the local manifest adds MindTheGapps' Android 17
branch (`vendor/gapps`, `cinnamonbun`) and `lineage_sargo.mk` in the bonito fork includes it, so
`sargo install` skips recovery. For older builds `sargo install` handles them: it sideloads the unmodified MindTheGapps 17 zip (it needs a
build from `24.0-20261109` on, whose kernel has EROFS for the Play Services APEX), then
`boot-test/post-install-24.sh` grants Play Services its runtime permissions (a phone flashed
after setup never gets the default grants, and location fails) and runs the background compile
job (after an OTA every app is left at `verify`).

Builds before 2026-11-09 cannot mount the EROFS APEX; for those, `repack-gapps.sh` makes a zip
that installs the same APK as `/product/priv-app/PrebuiltGmsCoreVic`. A phone with that
repacked install gets it removed first (`remove-repacked-gapps.sh`, one more trip to recovery);
`sargo install` does this when it finds `PrebuiltGmsCoreVic`. Both zips are unsigned, so
recovery asks to install anyway. An OTA from a system without GApps removes them.
