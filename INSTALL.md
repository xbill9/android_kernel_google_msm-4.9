# Install LineageOS 24.0 on a Pixel 3a from a laptop

For a computer that only installs: no Android tree, no build. One command downloads the newest
ROM from this repository's GitHub releases and MindTheGapps, installs both and compiles the
apps. Build machines use [BUILD-ROM.md](BUILD-ROM.md) instead.

## Once per laptop (Debian or Ubuntu)

    curl -fsSL https://raw.githubusercontent.com/xbill9/android_kernel_google_msm-4.9/main/backport-tools/lineage-24.0/sargo-setup.sh | bash

It installs the tools (asks for the sudo password if any are missing), fetches only the install
scripts into `~/sargo` (not the kernel source), links `~/bin/sargo-install` and installs on the
plugged-in phone. After that, run `sargo-install` (or `~/bin/sargo-install` until the next login).

## Every time

Plug in the phone, unlocked and booted, and run:

    sargo-install

It updates the scripts, downloads the newest build to `~/sargo-rom` (once; about 1.2 GB) and
checks it, downloads MindTheGapps the first time, and installs. A phone that still has the
repacked GApps from builds before 24.0-20261109 goes to recovery twice: once to remove them,
once for the unmodified MindTheGapps zip. Leave the phone alone except for this: when the
script prints `>>> ON THE PHONE`, the phone is in recovery.

- It says **Signature verification failed**: tap **Yes** (install anyway).
- When it finishes: tap **Reboot system now**.

The whole run takes about 15 minutes, plus the download. It ends with `=== DONE: 24.0-...`, or
`!!! STOPPED: ...` naming what failed. `sargo-install tested` takes the newest build marked
Latest (tested on a phone) instead of the newest build.

## What the phone needs

- **Already running one of these LineageOS 24.0 builds.** The script installs over it with
  the phone's own updater and keeps all data. It stops with `adb root does not work` on
  any other system.
- USB debugging on, and this laptop allowed: `adb devices` shows the phone as `device`.
  The first time, the phone asks "Allow USB debugging?"; tick "Always allow" and tap Allow.
- If the script stops with `adb root does not work`, turn on **Rooted debugging**
  (Settings, System, Developer options) and run it again.

Each run installs into the phone's other slot, so the slot flips every time.

## First install on a phone without this ROM (wipes the phone)

Tested with 24.0 on a phone whose slots held a build that did not boot. Back up first. With
the bootloader unlocked, download the ROM zip, `boot.img`, `dtbo.img` and `vbmeta.img` from the
newest `lineage-24.0-sargo-*` [release](https://github.com/xbill9/android_kernel_google_msm-4.9/releases)
into `~/sargo-rom`, then:

1. `adb reboot bootloader`, then flash this ROM's recovery and wipe user data:

       cd ~/sargo-rom
       fastboot flash boot boot.img
       fastboot flash dtbo dtbo.img
       fastboot flash vbmeta vbmeta.img
       fastboot -w

2. Boot to recovery. This bootloader refuses `fastboot reboot recovery`, so go through
   fastbootd: `fastboot reboot fastboot`, then `fastboot reboot recovery`.
3. On the phone choose Apply update, Apply from ADB, then run
   `adb sideload lineage-24.0-*-UNOFFICIAL-sargo.zip`. The progress stops near 47% and
   adb exits with `Total xfer: 1.00x`: that is a complete transfer. Accept the signature
   warning if asked.
4. Reboot system now. After the first boot, accept "Allow USB debugging?" on the phone, then
   run `sargo-install` to add Google apps.
