# Sync the build tree and get the ROM

Two jobs: keep the LineageOS tree the same on both build machines, and get a ROM zip to
install. To build, see [BUILD-ROM.md](BUILD-ROM.md). To install on a phone without a build
machine, see [INSTALL.md](INSTALL.md).

| Machine | Address | Tree |
|---|---|---|
| `gpu` | `192.168.0.156` | `/android/lineage-24.0` (`~/android` links to `/android`) |
| `build` | `192.168.50.45` | `/home/xbill/android/lineage-24.0` |

**Run every command here on `gpu`.** It can reach `build` over SSH, and `build` cannot reach
`gpu` (port 22 times out). That holds for both directions of a sync.

## Sync the tree

### 1. Find the newer machine

    ssh xbill@192.168.50.45 'ls -l --time-style=long-iso ~/android/lineage-24.0/out/target/product/sargo/lineage_sargo-ota.zip'
    ls -l --time-style=long-iso /android/lineage-24.0/out/target/product/sargo/lineage_sargo-ota.zip

The newer date is the source. The sync overwrites the other machine's tree, so a sync in the
wrong direction replaces your latest build with the old one.

### 2. Check the machine that will be overwritten

Neither machine may be building or syncing. On the machine being overwritten:

    cd ~/android/lineage-24.0 && repo status

Commit anything listed there first, except the three `PLACEHOLDER` lines under
`prebuilts/clang/host/linux-x86`, which are normal. Copy out any zip you want to keep (see
[Keep a zip](#keep-a-zip)).

### 3. Set the direction

From `gpu` to `build`:

    SRC=/android/lineage-24.0/
    DST=xbill@192.168.50.45:/home/xbill/android/lineage-24.0/

From `build` to `gpu`:

    SRC=xbill@192.168.50.45:/home/xbill/android/lineage-24.0/
    DST=/android/lineage-24.0/

### 4. Dry run

Nothing is copied:

    rsync -aHn --stats --delete --exclude=/.aws-copy "$SRC" "$DST" | grep -E 'Number of|Total'

`Total transferred file size` is how much will be copied. Compare tree sizes with free space:

    du -s -B1G /android/lineage-24.0
    ssh xbill@192.168.50.45 'du -s -B1G ~/android/lineage-24.0'
    df -h /android
    ssh xbill@192.168.50.45 'df -h /'

The destination needs free space for the difference between the two trees, plus a margin.

### 5. Sync

    tmux new -s sync
    rsync -aHz --partial --delete-before --compress-choice=zstd --info=progress2 --exclude=/.aws-copy "$SRC" "$DST"

`tmux` keeps it running if the window closes, and `tmux attach -t sync` gets back to it
(`sudo apt install tmux` if it is missing). If the connection drops, run the same `rsync` again:
it continues from where it stopped.

- `-a` keeps file times, so the next `m bacon` sees the copy as up to date.
- `-H` keeps hard links.
- `--delete-before` removes old files first, so the destination's disk use stays lower.

Leave the source tree alone until it finishes.

### 6. Check it

    sha256sum /android/lineage-24.0/out/target/product/sargo/lineage_sargo-ota.zip
    ssh xbill@192.168.50.45 'sha256sum ~/android/lineage-24.0/out/target/product/sargo/lineage_sargo-ota.zip'

The two hashes must match.

## Get the ROM

### The published release

Promoted releases carry test notes, and `gh` skips prereleases. Download the latest and check it:

    mkdir -p ~/sargo-rom && cd ~/sargo-rom
    gh release download -R xbill9/android_kernel_google_msm-4.9 --pattern 'lineage-24.0-*.zip' --pattern SHA256SUMS --clobber
    sha256sum -c --ignore-missing SHA256SUMS

The last line must end in `OK`. With no tag, `gh` takes the latest release. See what is
published with:

    gh release list -R xbill9/android_kernel_google_msm-4.9

Without `gh`, take the zip link from the
[releases page](https://github.com/xbill9/android_kernel_google_msm-4.9/releases).

### The latest build on `gpu`

The zip is here, and the dated name is the same file:

    /android/lineage-24.0/out/target/product/sargo/lineage_sargo-ota.zip

### The latest build on `build`, without syncing the tree

    mkdir -p ~/sargo-rom
    rsync -aL --partial --info=progress2 xbill@192.168.50.45:/home/xbill/android/lineage-24.0/out/target/product/sargo/lineage_sargo-ota.zip ~/sargo-rom/

### Install it

With a phone already on a 24.0 build (keeps data):

    bash ~/android_kernel_google_msm-4.9/backport-tools/lineage-24.0/phone-install-24.sh <zip>

`adb devices` must show exactly one phone. A phone on anything other than 24.0 needs the
wipe in [INSTALL.md](INSTALL.md), so check first with `adb shell getprop ro.lineage.version`.

### Keep a zip

Every `lineage-24.0-*-UNOFFICIAL-sargo.zip` in `out/target/product/sargo/` is a hard link to
`lineage_sargo-ota.zip`, so each build replaces all of them, whatever date the name shows. A
hard link made from them elsewhere is replaced too. Use `cp`, and name it for the date it was
built:

    mkdir -p ~/sargo-bpf-test/build-24.0-YYYYMMDD
    cp /android/lineage-24.0/out/target/product/sargo/lineage_sargo-ota.zip ~/sargo-bpf-test/build-24.0-YYYYMMDD/lineage-24.0-YYYYMMDD-UNOFFICIAL-sargo.zip
