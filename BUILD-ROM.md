# Build and install LineageOS 24.0 on the Pixel 3a

One command does everything: sync, build, install on the phone, put Google apps back,
and compile the apps. It runs on either build machine with its own phone plugged in.
To install a published build from a computer that does not build, see [INSTALL.md](INSTALL.md).

## Every time

    git -C ~/android_kernel_google_msm-4.9 pull
    tmux new -s phone
    bash ~/android_kernel_google_msm-4.9/backport-tools/lineage-24.0/phone-update-24.sh

`tmux` keeps it running if the window closes; `tmux attach -t phone` gets back to it.
It takes about 30 minutes to 2 hours, mostly the build.

## The one thing you do

When the script prints `>>> ON THE PHONE`, the phone is in recovery:

1. It says **Signature verification failed**: tap **Yes** (install anyway).
2. When it finishes: tap **Reboot system now**.

Leave the phone alone the rest of the time.

## How it ends

- `=== DONE: 24.0-...-UNOFFICIAL-sargo on slot _a` (or `_b`): finished.
- `!!! STOPPED: ...`: something failed. The line says what; send it and the log it names.

## Shortcuts

    SKIP_SYNC=1 bash .../phone-update-24.sh               # build without syncing
    SKIP_SYNC=1 SKIP_BUILD=1 bash .../phone-update-24.sh  # install the last build again

## Before the first run on a machine

- `sudo apt install tmux` if `tmux` is missing.
- The repacked GApps zip `MindTheGapps-17.0.0-arm64-20261022_132445-sargo.zip` goes in
  `~/sargo-bpf-test/gapps/`. With only the original MindTheGapps 17 zip there, the script
  repacks it first.
- The phone needs USB debugging on and this computer allowed (`adb devices` shows `device`).
- Each run installs into the phone's other slot, so the slot flips every time.

Logs are in `~/sargo-bpf-test/logs/`: `sync-24-*.log`, `build-24.log`, `ota-install-last.log`.
The details behind each step are in `backport-tools/lineage-24.0/README.md`.
