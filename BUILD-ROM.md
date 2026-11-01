# Build and install LineageOS 24.0 on the Pixel 3a

Two commands. Both run on either build machine.

## Build a new ROM

    tmux new -s rom
    sargo-build

It pulls upstream, builds, and publishes the ROM to
[GitHub releases](https://github.com/xbill9/android_kernel_google_msm-4.9/releases) as a
prerelease. It takes about 30 minutes to 2 hours. `tmux attach -t rom` gets back to it.

## Install it on the phone

    sargo-install

The phone must be plugged in with USB debugging on. When it prints `>>> ON THE PHONE`, tap
**Yes** at "Signature verification failed", then **Reboot system now** when it finishes.
Each install goes into the phone's other slot, so the slot flips every time.

The GApps zip goes in `~/sargo-bpf-test/gapps/`. See [INSTALL.md](INSTALL.md) for a first
install or a computer that does not build.

## Promote a tested build

Builds stay prereleases, and installers keep getting the release marked Latest. After testing
on the phone, add the test notes and promote it:

    gh release edit lineage-24.0-sargo-YYYYMMDD -R xbill9/android_kernel_google_msm-4.9 --prerelease=false --latest

## If it stops

- **Forks are behind upstream:** run this, then `sargo-build` again. It rebases your fork
  commits onto upstream and force-pushes the forks.

      bash ~/android_kernel_google_msm-4.9/backport-tools/lineage-24.0/update-forks.sh --apply

  A conflict leaves that fork unchanged, and you rebase it by hand.
- **Build failed:** the log is `~/sargo-bpf-test/logs/build-24.log`.

## New machine

    gh repo clone xbill9/android_kernel_google_msm-4.9 ~/android_kernel_google_msm-4.9
    cd ~/android_kernel_google_msm-4.9/backport-tools/lineage-24.0
    ln -s "$PWD/sargo-build.sh" ~/bin/sargo-build
    ln -s "$PWD/phone-install-24.sh" ~/bin/sargo-install
    bash setup-24.sh

To copy the tree between build machines, see [SYNC-AND-PULL.md](SYNC-AND-PULL.md).
