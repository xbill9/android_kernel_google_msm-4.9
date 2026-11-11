# Building LineageOS 24.0 for sargo from scratch

Everything a build needs is on GitHub: this repository (the kernel, these
scripts and the local manifest), LineageOS `lineage-24.0`, AOSP
`android-17.0.0_r1`, and the xbill9 forks listed in `../../GIT-WORKFLOW.md`.
Every project in `local_manifest-sargo.xml` is on a `lineage-24.0` branch, or
`main` for the kernel and `vendor/apn`. A new machine needs the packages below,
a GitHub login, and the steps in order.

---

#### What the machine needs

| | Measured |
|---|---|
| Memory | RAM plus swap of about 80 GB. Soong analysis held 31 GB resident with 44 GB of swap in use on a 31 GB machine. |
| Disk | 294 GB for the tree after a complete build (`.repo` 60 GB, `out` 140 GB), plus 1.2 GB per published build in `~/sargo-bpf-test`. |
| Time | 9 h 13 min for `sargo-build` from an empty `out/` on `gpu` (12 threads, `-j12`), including the publish: 9 h 11 min in `m bacon`, 231,687 ninja steps (2026-11-09). The 2026-11-06 build took 9 h 27 min. |
| OS | Debian sid (forky) on x86_64 here; Ubuntu 24.04 on EC2 (`aws/ec2-build.sh`). |

Machines this has run on:

| Machine | CPU | RAM | Swap |
|---|---|---|---|
| `build` (192.168.50.45) | i7-3770, 8 threads | 31 GB | 79 GB |
| `gpu` (192.168.50.216) | i7-5820K, 12 threads | 46 GB | 36 GB (two partitions) |
| EC2 c7a.48xlarge | 192 vCPU | 384 GiB | none |

Debian packages, all installed on `gpu`:

```
sudo apt install bc bison build-essential ccache curl flex g++-multilib gcc-multilib git git-lfs \
  gnupg gperf imagemagick lib32readline-dev lib32z1-dev libdw-dev libelf-dev lz4 libsdl1.2-dev \
  libssl-dev libxml2-utils lzop pngcrush python3-protobuf rsync schedtool squashfs-tools xsltproc \
  zip zlib1g-dev repo gh adb fastboot
git lfs install
gh auth login
```

Debian names the lz4 tool `lz4` (Ubuntu: `liblz4-tool`). `gh` must be logged in
as `xbill9`: `sargo-build` publishes each build as a prerelease and
`sargo-install` downloads from the releases.

---

#### Step 1 — Lay out the paths

The scripts use `~/android/lineage-24.0` and `~/android_kernel_google_msm-4.9`.
On the build machine both are symlinks into a separate disk at `/android`:

```
sudo install -d -o $USER /android            # the build disk, mounted at /android
ln -s /android ~/android
gh repo clone xbill9/android_kernel_google_msm-4.9 /android/android_kernel_google_msm-4.9
ln -s /android/android_kernel_google_msm-4.9 ~/android_kernel_google_msm-4.9
mkdir -p ~/bin ~/sargo-bpf-test/logs
ln -s ~/android_kernel_google_msm-4.9/backport-tools/lineage-24.0/sargo-build.sh ~/bin/sargo-build
ln -s ~/android_kernel_google_msm-4.9/backport-tools/lineage-24.0/phone-install-24.sh ~/bin/sargo-install
```

`~/bin` must be on `PATH`. Keep the same user name (`xbill`) and the same
paths on every machine: the build graph records absolute paths and
`BUILD_USERNAME`, and a different value of either rebuilds most of the tree
(`aws/README.md`, "Quirks").

---

#### Step 2 — Sync the tree

```
bash ~/android_kernel_google_msm-4.9/backport-tools/lineage-24.0/setup-24.sh
```

`setup-24.sh` runs `repo init` for LineageOS `lineage-24.0` at depth 1, copies
`local_manifest-sargo.xml` to `.repo/local_manifests/sargo.xml`, syncs with up
to five tries, then runs `fetch-clang-24.sh`. That script trims
`prebuilts/clang/host/linux-x86` by sparse checkout to the versions the build
uses (7.4 GB on disk), one directory at a time with retries.

Trim clang before the first build, never after: the sparse checkout rewrites
the files it keeps, and a build that already finished then redoes 186,251 steps.

---

#### Step 3 — Build

```
setsid nohup sargo-build > ~/sargo-bpf-test/logs/sargo-build-$(date +%Y%m%d).log 2>&1 < /dev/null &
```

`sargo-build` does three things and stops at the first failure:

1. Pulls this repository, copies the local manifest into the tree, runs
   `repo sync`, and stops if `update-forks.sh` reports a fork behind upstream
   (then run `update-forks.sh --apply` and start again).
2. `lunch lineage_sargo cp2a userdebug` and `m bacon -j$(nproc)`, logged to
   `~/sargo-bpf-test/logs/build-24.log`.
3. Copies the OTA zip, `boot.img`, `dtbo.img`, `vbmeta.img` and the manifest to
   `~/sargo-bpf-test/build-24.0-<date>/` and publishes them as the prerelease
   `lineage-24.0-sargo-<date>`, "Not tested on a phone".

Start it with `setsid nohup` or in tmux, never as a Claude Code background
shell: Claude Code stops its own background shells under memory pressure, and
Soong analysis is exactly that.

For a build that reuses nothing, delete `out/` first:

```
rm -rf ~/android/lineage-24.0/out
```

---

#### Step 4 — Install on the phone

```
sargo-install            # newest prerelease
sargo-install tested     # newest release marked Latest
```

The phone must already run one of these 24.0 userdebug builds. The script
installs the OTA into the inactive slot, puts MindTheGapps back (downloaded if
no copy is in `~/sargo-bpf-test/gapps`), and compiles apps. The one manual step
is on the phone, in recovery: "Install anyway", then "Reboot system now". A
first install from another ROM: `article/evidence/INSTALL.md`.

When a build passes `boot-test/hw-check-24.sh` on the phone, mark it tested so
`sargo-install tested` picks it up:

```
gh release edit lineage-24.0-sargo-<date> -R xbill9/android_kernel_google_msm-4.9 --prerelease=false --latest
```

---

#### Switching machines

Two ways. A fresh sync (Steps 1 to 3) needs nothing from the old machine. A
copy of the built tree saves the build time.

Before leaving the old machine, check that nothing exists only there:

```
cd ~/android/lineage-24.0 && repo status                        # local edits anywhere in the tree
bash ~/android_kernel_google_msm-4.9/backport-tools/lineage-24.0/update-forks.sh
git -C ~/android_kernel_google_msm-4.9 status -sb                # this repository: committed and pushed
```

`repo status` should list only the three `PLACEHOLDER` files under
`prebuilts/clang/host/linux-x86`. Each fork must show its own commits pushed
(`GIT-WORKFLOW.md`, Step 4).

What lives outside GitHub, in `~/sargo-bpf-test`:

| Folder | Size | Needed for |
|---|---|---|
| `gapps/` | 1.6 GB | MindTheGapps zips; `sargo-install` downloads them again if missing |
| `kmsglog/` | 903 MB | boot-log images for `boot-test/` |
| `build-24.0-*/` | 1.2 GB each | the same files as the GitHub releases |
| `logs/` | 703 MB | build and boot logs |

Copy what you want with `rsync -aH` (sizes measured 2026-11-06).

To move the built tree instead, copy it to the same path under the same user,
with the old tree moved aside rather than merged into:

```
rsync -aH --info=progress2 old-machine:/android/lineage-24.0/ /android/lineage-24.0/
```

`-a` keeps file times, so the build sees the copy as up to date; `-H` keeps
the hard links between the OTA zips. `aws/pull-tree.sh` does this for the EC2
instance. The first `m bacon` afterwards should do little or nothing.

---

#### 🔎 Tip: one manifest, two copies

The build reads `.repo/local_manifests/sargo.xml`; the source of truth is
`local_manifest-sargo.xml` in this repository. `sargo-build` copies one over the
other on every run, so edit the one in this repository, commit and push. When a
project changes source (LineageOS to a fork), the next sync needs
`repo sync -c --force-sync <path>` once.
