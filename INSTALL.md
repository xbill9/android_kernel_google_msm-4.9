# Install LineageOS 24.0 on a Pixel 3a from a laptop

For a computer that only installs: no Android tree, no build. It downloads the ROM from
this repository's GitHub releases and MindTheGapps from its own releases, then one command
installs both and compiles the apps. Build machines use [BUILD-ROM.md](BUILD-ROM.md) instead.

## Once per laptop (Debian or Ubuntu)

    sudo apt install adb git unzip python3 erofs-utils
    git clone --depth 1 --filter=blob:none --sparse https://github.com/xbill9/android_kernel_google_msm-4.9.git ~/sargo
    git -C ~/sargo sparse-checkout set backport-tools/lineage-24.0
    mkdir -p ~/sargo-rom

The clone fetches only the install scripts, not the kernel source.

## Every time

1. Get the newest scripts and the files:

       git -C ~/sargo pull
       cd ~/sargo-rom
       wget -nc https://github.com/xbill9/android_kernel_google_msm-4.9/releases/download/lineage-24.0-sargo-20261027/lineage-24.0-20261027-UNOFFICIAL-sargo.zip
       wget -nc https://github.com/MindTheGapps/17.0.0-arm64/releases/download/MindTheGapps-17.0.0-arm64-20261022_132445/MindTheGapps-17.0.0-arm64-20261022_132445.zip

   For a newer build, take the ROM link from the newest `lineage-24.0-sargo-*` release.

2. Plug in the phone, unlocked and booted, and run:

       bash ~/sargo/backport-tools/lineage-24.0/phone-install-24.sh ~/sargo-rom/lineage-24.0-20261027-UNOFFICIAL-sargo.zip ~/sargo-rom/MindTheGapps-17.0.0-arm64-20261022_132445.zip

   The first run repacks MindTheGapps for this phone (a minute) and keeps the result
   next to it, so later runs reuse it.

3. When the script prints `>>> ON THE PHONE`, the phone is in recovery:
   - It says **Signature verification failed**: tap **Yes** (install anyway).
   - When it finishes: tap **Reboot system now**.

   Leave the phone alone the rest of the time. The whole run takes about 15 minutes.

It ends with `=== DONE: 24.0-...` when finished, or `!!! STOPPED: ...` naming what failed.

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
the bootloader unlocked, also download `boot.img`, `dtbo.img` and `vbmeta.img` from the same
release into `~/sargo-rom`, then:

1. `adb reboot bootloader`, then flash this ROM's recovery and wipe user data:

       cd ~/sargo-rom
       fastboot flash boot boot.img
       fastboot flash dtbo dtbo.img
       fastboot flash vbmeta vbmeta.img
       fastboot -w

2. Boot to recovery. This bootloader refuses `fastboot reboot recovery`, so go through
   fastbootd: `fastboot reboot fastboot`, then `fastboot reboot recovery`.
3. On the phone choose Apply update, Apply from ADB, then run
   `adb sideload lineage-24.0-20261027-UNOFFICIAL-sargo.zip`. The progress stops near 47% and
   adb exits with `Total xfer: 1.00x`: that is a complete transfer. Accept the signature
   warning if asked.
4. Reboot system now. After the first boot, accept "Allow USB debugging?" on the phone, then
   run step 2 above to add Google apps.
