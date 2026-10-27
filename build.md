# Building the sargo BPF backport kernel

For the whole LineageOS 24.0 ROM (sync, build, install with Google apps) see
[BUILD-ROM.md](BUILD-ROM.md); to install a published ROM, [INSTALL.md](INSTALL.md).

Kernel for the Pixel 3a (`sargo`, `bonito_defconfig`) running LineageOS
22.2: the LineageOS 4.9 kernel with eBPF support backported to roughly the
5.4 level, arm64 uprobes, and BPF tracing. Details, test results and the
backport scripts are in [`backport-tools/`](backport-tools/README.md).

## Quick start

**The repo:** https://github.com/xbill9/android_kernel_google_msm-4.9
(branch `main`). Everything is in it: the kernel source (4.9.337 with
the backports), the scripts in `backport-tools/`, and this file.

### On the new box (Debian/Ubuntu)

**1. Install the build tools**

    sudo apt-get install -y git make gcc bc bison flex libssl-dev libelf-dev python3 cpio curl

**2. Get the repo and the toolchains** (about 2 minutes, 4 GB of disk)

    curl -s https://raw.githubusercontent.com/xbill9/android_kernel_google_msm-4.9/main/backport-tools/setup.sh | bash

This creates:

- `~/sargo-bpf-test/ksargo`: the repo
- `~/sargo-bpf-test/tc`: the toolchains (clang and binutils)
- `~/sargo-bpf-test/tools`: mkbootimg

**3. Build**

    ~/sargo-bpf-test/ksargo/backport-tools/build.sh Image.lz4 modules

The last step (the link) needs about 5 GB of RAM and takes 20 minutes or
more.

**4. Strip the Wi-Fi module**

    cd ~/sargo-bpf-test
    tc/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9/bin/aarch64-linux-android-strip \
        --strip-debug -o wlan.ko kout/drivers/staging/qcacld-3.0/wlan.ko

### What you get

- `~/sargo-bpf-test/kout/arch/arm64/boot/Image.lz4`: the kernel
- `~/sargo-bpf-test/wlan.ko`: the Wi-Fi module

Copy both files to the machine the phone is plugged into to flash them.
The flashing steps are in
[`backport-tools/README.md`](backport-tools/README.md#installing-for-daily-use),
under "Installing for daily use".

### Later updates

    git -C ~/sargo-bpf-test/ksargo pull
    ~/sargo-bpf-test/ksargo/backport-tools/build.sh Image.lz4 modules

The sections below explain each step in more detail.

## 1. Set up the build machine

Host packages (Debian/Ubuntu):

    sudo apt-get install git make gcc bc bison flex libssl-dev libelf-dev python3 cpio curl

Then fetch and run the setup script:

    curl -sO https://raw.githubusercontent.com/xbill9/android_kernel_google_msm-4.9/main/backport-tools/setup.sh
    bash setup.sh                      # or: SARGO_ROOT=/path bash setup.sh

This lays out `~/sargo-bpf-test` (or `$SARGO_ROOT`), taking about two
minutes and 4 GB of disk:

| Path | Contents |
| --- | --- |
| `ksargo/` | this repo (`main`) |
| `tc/` | toolchains, pinned to the commits used for every build so far |
| `tools/` | AOSP `mkbootimg` / `unpack_bootimg` |

Pinned toolchains:

| Toolchain | Commit |
| --- | --- |
| LineageOS clang r416183b | `54220fd60105` |
| aarch64-linux-android GCC 4.9 (binutils) | `5e030eafe024` |
| arm-linux-androideabi GCC 4.9 (binutils) | `111258a10e01` |
| lz4 1.9.4 | `5ff839680134` |

## 2. Build

    ~/sargo-bpf-test/ksargo/backport-tools/build.sh Image.lz4 modules

This builds the way LineageOS does: clang with GNU binutils, full LTO,
Clang CFI. Output goes to `~/sargo-bpf-test/kout`. The first run creates
the config from `bonito_defconfig`; later runs are incremental.

- `JOBS=N` sets the parallelism (default `nproc`).
- The full-LTO link is single-threaded and needs about 5 GB of RAM, and
  takes 20 minutes or more on a modest machine. On a machine with little
  RAM, add swap or the link gets killed.
- The kernel version string is `4.9.337-g<commit>`; a `-dirty` suffix means
  there were uncommitted changes.

The two files the phone needs:

| File | Goes to |
| --- | --- |
| `kout/arch/arm64/boot/Image.lz4` | the kernel inside the boot image |
| `kout/drivers/staging/qcacld-3.0/wlan.ko` | `/vendor/lib/modules/wlan.ko` (Wi-Fi) |

The stock vendor `wlan.ko` will not load into a custom kernel, so Wi-Fi
needs this matching module. Strip it before installing (from
`~/sargo-bpf-test`):

    tc/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9/bin/aarch64-linux-android-strip \
        --strip-debug -o wlan.ko kout/drivers/staging/qcacld-3.0/wlan.ko

## 3. Test and install

Copy both files to the machine the phone is plugged into. Build the boot
image from the phone's current boot partition (OTAs change the ramdisk, so
never from an old backup), RAM-boot it first, and flash only once it
works:

    fastboot boot boot-new.img         # RAM only; a reboot returns to the flashed kernel

The exact steps (backing up the boot partition, repacking with
`mkbootimg`, flashing, and writing `wlan.ko` to the full `/vendor`
partition) are in
[`backport-tools/README.md`](backport-tools/README.md#installing-for-daily-use).
Keep the boot partition backup and the stock `wlan.ko` to roll back.

## 4. Check it

On the phone, as root (`adb root`, with "Rooted debugging" enabled):

- `uname -r` shows the commit you built.
- `dmesg | grep -E "WARNING:|Oops|BUG:"` shows only the ION warning that
  the stock kernel logs too.
- `lsmod` lists `wlan`, and Wi-Fi connects.
- `backport-tools/build-selftests.sh` builds and runs the linux-5.4.y
  `test_verifier`. Expected: 1610 passed, 0 failed, 3 skipped.
- `backport-tools/uprobe_bpf.c` and `backport-tools/probe_pmu_bpf.c` test
  BPF on uprobes and on the perf kprobe/uprobe PMUs; each should print PASS.
