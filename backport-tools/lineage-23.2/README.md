# LineageOS 23.2 on the Pixel 3a (sargo)

Everything needed to rebuild, install and debug the unofficial LineageOS 23.2
(Android 16) build for sargo on this 4.9 kernel. Release:
[`lineage-23.2-sargo-20261021`](https://github.com/xbill9/android_kernel_google_msm-4.9/releases/tag/lineage-23.2-sargo-20261021),
kernel `50d897667cc3`, built and tested 2026-10-21.

Paths below assume the tree at `~/android/lineage-23.2` and work files in
`~/sargo-bpf-test` (the same `SARGO_ROOT` as `../build.sh`).

## Sources

`local_manifest-sargo.xml` goes in `.repo/local_manifests/sargo.xml` on top of
`repo init -u https://github.com/LineageOS/android.git -b lineage-23.2 --git-lfs`.

| Path | Repository | Revision |
|---|---|---|
| `device/google/bonito` | `xbill9/android_device_google_bonito` | `lineage-23.0` at `f64ad12` |
| `device/google/sargo` | `LineageOS/android_device_google_sargo` | `lineage-23.0` |
| `device/google/gs-common` | `LineageOS/android_device_google_gs-common` | `lineage-23.0`; 23.2 removed thermal from it |
| `packages/apps/ElmyraService` | LineageOS | `lineage-23.2` |
| `kernel/google/msm-4.9` | this repository | `main`; the release used `50d897667cc3` |
| `vendor/google/{sargo,bonito}` | `xbill9/proprietary_vendor_google_*`, forks of TheMuppets | `lineage-22.2` plus the camera fixup: sargo `e599461`, bonito `0934eea` |
| `hardware/qcom/audio` | `xbill9/android_hardware_qcom_audio` | `lineage-23.2` plus `e2cae17`, the patch below |

The newest device trees are `lineage-23.0`; TheMuppets has no 23.x branch for
sargo, and the 22.2 blobs are the final Google firmware.

### Two prebuilt repositories, fetched by hand

`platform/prebuilts/clang/host/linux-x86` and `platform/prebuilts/rust` are
over 110 GB as a full shallow fetch, and googlesource returns 502/503 on long
transfers. The manifest removes both and `fetch-prebuilts.sh` takes a partial
clone at `android-16.0.0_r4` with a sparse checkout of what the build uses:
`clang-r563880c`, `clang-r563880`, `clang-3289846`, `llvm-binutils-stable`,
`clang-stable` and the build-system directories (9.9 GB). One trusty module
group names `clang-r547379`; an empty `clang-r547379/PLACEHOLDER` satisfies it.

Rust: when googlesource kept failing, `prebuilts/rust/linux-x86/1.88.0` came from
rustup instead:

1. `rustup toolchain install 1.88.0` with `rust-src`, `clippy`, `rustfmt` and the
   Android targets, copied to `prebuilts/rust/linux-x86/1.88.0`.
2. `rustc`, `rustdoc` and `clippy-driver` renamed to `*.real` and replaced by
   wrappers, since the build passes `-Z` options that only nightly or
   `RUSTC_BOOTSTRAP` accept:

       #!/bin/sh
       RUSTC_BOOTSTRAP=1 exec "$(dirname "$0")/rustc.real" "$@"

3. `lib/rustlib/src/rust` replaced by Google's patched standard library sources
   from the same AOSP tag.

## Local changes

### Kernel (this repository)

Android 16 needs three things the 4.9 kernel lacked; each stopped init before
Android started. See `../README.md`, section "LineageOS 23.2 (Android 16)".

| Commit | Change |
|---|---|
| `4636e3e38977`, `a613f32fa1bb`, `cf0a02b754fa` | `MADV_WIPEONFORK` (upstream `df3735c5b40f`, `d2cd9ede6e19`, `93949bb21b52`) |
| `bf173c3ec796` | Read and drop netlink xperm rules in the SELinux policy |
| `605b35dc6271`, `7ea255e137dd`, `f649c8dac935`, `416f6a9fd740` | `cpuset_v2_mode` (upstream `e1cba4b85daa`, `b8d1b8ee93df`, `e06b678e3b4e`, `b8eb5fd5bdb4`) |
| `625d64222fae` | `uname` reports 5.4.302 to bpfloader, netbpfload and netd |

### Device tree (`device/google/bonito`)

- `df9d51a` Soong namespaces, soong config flags, VINTF kernel requirements
  off, `ro.bpf.kver_override=5.4.302`.
- `04c08e3` hwcomposer name, `/vendor/firmware_mnt` module, `KERNEL_CC := CC=clang`
  (keeps the 4.9 Makefile's gold LTO link), qtiaudio soong flags, product
  reserve 1000 → 900 MiB (images were 34.9 MB over super), HIDL power.stats
  dropped, AIDL storage health.
- `24c9e5f` camera blobs link against `libtinyxml2-v34`.
- `f64ad12` netd starts on `bpf-progs-loaded`, not `post-fs-data`.

### Vendor blobs

`camera.sdm710.so`, `com.qti.node.eisv2.so` and `com.qti.node.eisv3.so` need
`libtinyxml2-v34.so` in place of `libtinyxml2.so`. With Android 16's
`libtinyxml2` the camera provider crashes in
`CamX::TuningDataManager::GetChromatix()` on every start. The older
`libtinyxml2_1.so` blob lacks `XMLDocument::ErrorIDToName`. The fixup is in
`extract-files.py`, and the vendor forks carry the patched blobs. By hand, on
blobs already extracted:

    patchelf --replace-needed libtinyxml2.so libtinyxml2-v34.so <blob>

and `libtinyxml2` → `libtinyxml2-v34` in the three modules' `shared_libs` in
`vendor/google/{sargo,bonito}/Android.bp`.

### hardware/qcom/audio

`hardware_qcom_audio-cirrus_playback.patch` (`e2cae17` in the fork): the Cirrus
config thread gets the pthread start-routine signature that clang requires. The
manifest swaps the project by path, because the same repository name also backs
`hardware/qcom-caf/msm8953/audio` and `msm8996/audio`.

## Build

    bash build-23.2.sh          # lunch lineage_sargo-bp4a-userdebug; m bacon -j8

On an i7-3770 with 32 GB RAM and 8 threads, incremental builds took 17 to 48
minutes. The kernel is built inside the tree from `kernel/google/msm-4.9`.

## Install and update

- **From 22.2:** `fastboot -w`, then sideload the zip from recovery and accept
  the test-key signature warning. Android 16 upgrades `/data`; 22.2 cannot boot
  it afterwards and stops on recovery's "Can't load Android system".
- **Updates:** `build-and-ota.sh` builds, pushes the zip to
  `/data/ota_package/update.zip` and applies it to the inactive slot with
  `update_engine_client --update --payload=file://... --offset --size --headers`.
  The headers come from `payload_properties.txt`, one per line. Data is kept;
  applying took 4 min 37 s.
- **GApps:** MindTheGapps `16.0.0-arm64` from recovery, then Factory reset.

## Debugging a boot that fails before adb

The test loop runs without touching the phone:

- `fastboot oem off-mode-charge 0`. With USB in, a phone that powers off boots
  again instead of stopping on the charging screen.
- `test-boot.sh <boot.img>` RAM-boots an image with `fastboot boot`, with the
  flashed 23.2 image as the fallback. It compares `/proc/version` to tell the
  tested kernel from the fallback and collects the logs. One cycle is about
  two minutes.
- `kmsglog.c` is a freestanding static `/init` (raw syscalls, no libc). It
  forks a child into a private mount namespace and execs the real init as
  PID 1. The child mounts sysfs, finds the `metadata` partition and appends
  `/dev/kmsg` to `/metadata/boot-kmsg.txt` with synced writes. Build:

      clang --target=aarch64-linux-gnu -O2 -ffreestanding -fno-stack-protector \
            -nostdlib -static -fno-pic -fuse-ld=lld -o kmsglog kmsglog.c

  `add-logger.sh` puts it into a boot image; `KERNEL=` swaps the kernel and
  `EXTRA_CMDLINE=` appends to the command line.
- pstore survives a warm reboot, such as kmsglog's reboot to the bootloader:
  `/sys/fs/pstore/console-ramoops-0` then holds the previous boot's console
  (seen on 24.0, 2026-10-26). A Power + Volume Down reset or a watchdog reset
  wipes it.

Each Android 16 blocker on this kernel showed up as one line in that log:

    e2fsck: libc: arc4random data MADV_WIPEONFORK failed: Invalid argument
    SELinux: avtab: invalid type or class
    libprocessgroup: Failed to mount controller cpuset: No such file or directory

## Gotchas

- Do not boot a 22.2 slot once 23.2 has run: see Install.
- `update_engine_client` without `--update` exits 0 and does nothing.
- `adb shell` refusing every command with `error: closed` after a factory
  reset means the computer is not yet authorized on the phone.
- The kernel's `writecnt` field changes module CRCs; `wlan.ko` must come from
  the same build as the kernel.
