# Committing changes to the sargo ROM

The ROM tree at `/android/lineage-24.0` holds about a thousand separate git
repositories, one per folder, managed by `repo`. Each folder (`vendor/apn`,
`device/google/bonito`, ...) has its own history and its own GitHub home, so
changes are committed and pushed inside that folder.

---

#### Which folders you can push to

The xbill9 forks, listed as `<project name="xbill9/...">` lines in
`backport-tools/lineage-24.0/local_manifest-sargo.xml`:

| Folder | Fork | Branch |
|---|---|---|
| `device/google/bonito` | `xbill9/android_device_google_bonito` | `lineage-24.0` |
| `vendor/google/sargo` | `xbill9/proprietary_vendor_google_sargo` | `lineage-24.0` |
| `vendor/google/bonito` | `xbill9/proprietary_vendor_google_bonito` | `lineage-24.0` |
| `kernel/google/msm-4.9` | `xbill9/android_kernel_google_msm-4.9` (this repository) | `main` |
| `hardware/qcom/sdm845/display` | `xbill9/android_hardware_qcom_sdm845_display` | `lineage-24.0` |
| `hardware/qcom/audio` | `xbill9/android_hardware_qcom_audio` | `lineage-24.0` |
| `hardware/qcom/sdm845/gps` | `xbill9/android_hardware_qcom_sdm845_gps` | `lineage-24.0` |
| `device/google/sargo` | `xbill9/android_device_google_sargo` | `lineage-24.0` |
| `prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9` | `xbill9/android_prebuilts_gcc_linux-x86_aarch64_aarch64-linux-android-4.9` | `lineage-24.0` |
| `prebuilts/gcc/linux-x86/arm/arm-linux-androideabi-4.9` | `xbill9/android_prebuilts_gcc_linux-x86_arm_arm-linux-androideabi-4.9` | `lineage-24.0` |
| `system/core` | `xbill9/android_system_core` | `lineage-24.0` |
| `system/vold` | `xbill9/android_system_vold` | `lineage-24.0` |
| `system/bpf` | `xbill9/android_system_bpf` | `lineage-24.0` |
| `system/zygote` | `xbill9/android_system_zygote` | `lineage-24.0` |
| `packages/modules/Connectivity` | `xbill9/android_packages_modules_Connectivity` | `lineage-24.0` |
| `vendor/apn` | `xbill9/android_vendor_apn` | `main` |
| `vendor/lineage` | `xbill9/android_vendor_lineage` | `lineage-24.0` |

Every other folder belongs to LineageOS or AOSP. Edits there are lost at the
next `repo sync`; to change one, fork it first (see below).

---

#### Step 1 — Change a file in a fork

`repo sync` leaves each folder on a detached commit, so start a branch at the
fork's tip. The remote is named `github`. Example for `vendor/apn`:

```
cd /android/lineage-24.0/vendor/apn
git remote -v                          # should show github https://github.com/xbill9/...
git fetch github main
git checkout -B main github/main       # branch = the fork's branch from the table
# edit files
git add US.xml
git commit -m "US: what changed and why"
git push github HEAD:main
```

If `git remote -v` shows LineageOS instead of xbill9, the folder still comes
from upstream: the manifest has no fork for it yet, or the tree has not been
synced since the fork was added.

---

#### Step 2 — Fork a folder that has no fork yet

```
gh repo fork LineageOS/android_vendor_apn --clone=false
```

Add a pair of lines to `backport-tools/lineage-24.0/local_manifest-sargo.xml`:

```
<remove-project name="LineageOS/android_vendor_apn" />
<project name="xbill9/android_vendor_apn" path="vendor/apn" remote="github" revision="main" />
```

Use the upstream branch for `revision` (`gh api repos/LineageOS/<name> --jq .default_branch`
or the `revision` in `.repo/manifests/snippets/lineage.xml`). Commit and push the
manifest in this repository (Step 3), then switch the folder over:

```
cp backport-tools/lineage-24.0/local_manifest-sargo.xml \
   /android/lineage-24.0/.repo/local_manifests/sargo.xml
cd /android/lineage-24.0 && repo sync --force-sync vendor/apn
```

The first sync after a folder changes source needs `--force-sync`; repo replaces
the folder's git metadata, so commit or push anything in it beforehand. Then
make the change as in Step 1.

---

#### Step 3 — Commit in this repository

This repository (kernel source plus `backport-tools`) is a single git repo,
checked out at `/android/android_kernel_google_msm-4.9`:

```
cd /android/android_kernel_google_msm-4.9
git add <files>
git commit -m "..."
git pull --rebase origin main
git push origin main
```

The ROM build reads a second copy at `/android/lineage-24.0/kernel/google/msm-4.9`,
which `repo sync` updates from GitHub. Edit and commit in the first checkout and
push; the build copy follows at the next sync.

---

#### Step 4 — Check that everything is pushed

```
cd /android/lineage-24.0 && repo status     # local edits anywhere in the tree
git -C <folder> status -sb                  # one repository: uncommitted work, or ahead of GitHub
```

`repo status` lists three untracked `PLACEHOLDER` files under
`prebuilts/clang/host/linux-x86`; they belong to the hand-fetched clang setup.

---

#### Step 5 — Keep the forks current with upstream

```
bash backport-tools/lineage-24.0/update-forks.sh            # how far each fork is behind
bash backport-tools/lineage-24.0/update-forks.sh --apply    # replay fork commits on upstream, push
```

`sargo-build` runs the report and stops when a fork is behind; run `--apply`,
then `sargo-build` again.

---

#### 🔎 Tip: commit messages

Say what changed and why in the first line, prefixed with the area
(`telephony:`, `US:`, `bonito:`). Fork commits are replayed onto upstream by
`update-forks.sh --apply`, so small single-purpose commits rebase most cleanly.
