---
title: "Rent the Cores, Keep the Phone: Building LineageOS 24 on EC2 and Iterating at Home"
published: false
description: "A full LineageOS 24.0 build outgrows a 32 GB home machine. A 192-core EC2 instance runs the long build, the tree comes home at the same path, and local builds pick up incrementally next to the phone. Claude Code drives both ends."
tags: aws, android, lineageos, ec2
cover_image: https://raw.githubusercontent.com/xbill9/android_kernel_google_msm-4.9/main/backport-tools/lineage-24.0/aws/article/devto-cover.415eb5a0.jpg
---

This article provides a step by step guide to running the bulk of a LineageOS 24.0 build on an Amazon EC2 instance and finishing it on a home machine, where the phone and its logs are. Claude Code sizes the instance, runs the build, fixes the port errors and moves the tree.

https://github.com/xbill9/android_kernel_google_msm-4.9/tree/main/backport-tools/lineage-24.0/aws

**A from-scratch LineageOS 24.0 build for the Pixel 3a stalled on a 32 GB home machine during Soong analysis. On a c7a.48xlarge the tree synced in 15 minutes and the first build reached 156,615 of about 232,000 steps in 19 min 17 s. Building on EC2 at the home machine's own path, with the home user name, makes the result portable: the tree is copied home and local builds continue incrementally.**

---

#### What is this project trying to Do?

The Pixel 3a (sargo) runs LineageOS 23.2 on its original 4.9 kernel. The next step is 24.0, which is Android 17, and the port is a loop: change the device tree, build, flash, read the boot log, repeat.

That loop has two halves with different needs. The full build wants many cores and a lot of memory once. Every iteration after it wants the phone on a USB cable and a build that redoes only what changed.

The plan: rent the first half, keep the second half at home, and let Claude Code run the hand-off.

---

#### At This Point You Should Have…

- An AWS account with an on-demand vCPU quota of at least 192 for standard instances (this one has 264)
- A home build machine with room for the whole tree; this one has 278 GB free and is an i7-3770 with 8 threads and 31 GB of RAM, running Debian sid
- The LineageOS 24.0 local manifest and device-tree forks from the repository
- Claude Code with the AWS MCP server
- A clone: `git clone https://github.com/xbill9/android_kernel_google_msm-4.9`

---

#### Step 1 — Size the Instance From the Last Local Build

The port notes record what the home machine saw. Soong analysis held 31 GB resident with 44 GB of swap in use at the peak and took about 1 h 45 min. Compiling then ran 8 threads at 94-95 % user time, and one run finished 130,617 of about 233,000 steps in 7 h 49 min.

So the instance needs at least 75 GB of RAM for analysis and as many cores as the compile can use. Claude Code queried prices and quotas through the AWS MCP server:

| Instance | vCPU | RAM (GiB) | On-demand $/h | Spot $/h, cheapest zone |
|---|---|---|---|---|
| m7a.16xlarge | 64 | 256 | 3.71 | 0.86 |
| m7a.24xlarge | 96 | 384 | 5.56 | 1.28 |
| c7a.48xlarge | 192 | 384 | 9.85 | 2.93 |
| m7a.48xlarge | 192 | 768 | 11.13 | 3.19 |

The spot placement score for m7a.48xlarge was 1 out of 10 in every us-east-1 zone, so a 192-core spot request is likely to be refused or reclaimed mid-build. The build runs on-demand on a **c7a.48xlarge**: the same core count as the m7a, and 384 GiB leaves room well beyond the 75 GB analysis peak.

---

#### Step 2 — Launch at the Home Machine's Path

The instance is Ubuntu 24.04 with a 1000 GB gp3 root volume at 16,000 IOPS and 1000 MB/s. SSH is open to the home IP only, with an imported key pair.

Two settings decide whether the finished tree can come home and build incrementally. The tree lives at `/home/xbill/android/lineage-24.0`, the same absolute path as on the home machine, and the build runs with `BUILD_USERNAME=xbill`.

The reason is in the build output. The Soong graph uses relative paths, while the Kati graph names the tree by absolute path:

```
out/soong/build.lineage_sargo.ninja: 0 lines with /home/ubuntu of 3712391
out/build-lineage_sargo.ninja: 124 lines with /home/ubuntu of 5697004
```

And Soong records `BUILD_USERNAME` among the environment values that trigger a fresh analysis:

```
soong.environment.used.lineage_sargo.build BUILD_USERNAME ubuntu
```

A different user name at home means a new analysis there, the step that needed 75 GB.

---

#### Step 3 — Sync From Scratch

`ec2-build.sh` installs the build packages, runs `repo init` at depth 1, installs the local manifest and syncs with 32 jobs. On EC2 the platform clang is synced by repo in full, where the home tree uses a sparse checkout.

```
14:39:26 repo sync
14:53:00 sync attempt 1 exit=1 free= 828G
14:54:09 sync attempt 2 exit=0 free= 828G
```

Sync took 15 minutes. One repository answered HTTP 429, the AOSP server's rate limit, and the retry loop fetched it a minute later.

---

#### Step 4 — Build

`lunch lineage_sargo cp2a userdebug`, then `m bacon` at the default parallelism, which is 194 jobs on this instance.

```
14m50.05s Build Failure: 156615 done, 1 failed, 75511 remaining - 175.96/s
real	19m16.886s
user	2259m47.302s
```

Analysis and two thirds of the compile took 19 min 17 s, against 1 h 45 min for analysis alone at home. The failure is a port error, covered next.

---

#### Step 5 — Fix Port Errors at Cloud Speed

Each fix goes to the fork on GitHub, the instance syncs that one project, and the build resumes from where it stopped. Four Android 17 changes surfaced in order:

| Error | Fix | Rebuild |
|---|---|---|
| `unknown type logger_app` | drop the device tree's `logger_app.te`; Android 17 no longer defines the type | 5 min 49 s |
| `unknown type wifi_logging_data_file` | stop adding `wifi_perf_diag` policy; Pixel adds it for eng builds only | 9 min 51 s |
| `Found conflicting genfscon rules` for `/class/typec` | use the platform's `sysfs_typec` label; keep `sysfs_usb_c` on `usbc0` | 16 min 36 s |
| `Unresolved symbol ... createSurface ... LayerMetadata` in `libsecureuisvc_jni.so` | shim library exporting the Android 12 symbol | PENDING |

The fourth is a vendor blob compiled when `SurfaceComposerClient::createSurface` took `LayerMetadata` by value. Android 17 takes it by const reference, so the mangled name changed. A shim exports the old name and forwards:

```cpp
sp<SurfaceControl> SurfaceComposerClient_createSurface_metadataByValue(
        SurfaceComposerClient* thiz, const String8& name, uint32_t w, uint32_t h, PixelFormat format,
        int32_t flags, const sp<IBinder>& parentHandle, gui::LayerMetadata metadata,
        uint32_t* outTransformHint)
        __asm__("_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");
```

The `__asm__` label gives an ordinary C++ function the blob's symbol name, and a free function with the object as its first argument has the member function's calling convention. The body calls `thiz->createSurface(...)` with the same arguments.


`extract-files.py` gets a `blob_fixup().add_needed('libgui_shim_secureui.so')` for the blob, so a fresh extraction reproduces the patch, and `proprietary-files.txt` pins both the original and the patched SHA-1.

---

#### 🔎 Tip: Same Commit Hash on Both Machines

The instance has no GitHub credentials. Commits made there travel as `git format-patch --binary` files and are applied at home with `git am --committer-date-is-author-date` under the same committer identity. The result has the same hash as the instance's commit, so after the push the instance tree and GitHub agree, and `repo sync` on the instance changes no file.

---

#### Step 6 — Trim the Tree to Fit at Home

PENDING: final sizes after the clean build.

Sizes on the instance with the build at 85 %:

| Part | Size | Needed at home |
|---|---|---|
| `out/soong/.intermediates` | 90 GB | yes, it is the build state |
| `.repo/project-objects` | 32 GB | yes, every project's git data |
| `prebuilts/clang/host/linux-x86` | 19 GB | two of five versions |
| `prebuilts/rust-toolchain` | 13 GB | yes |
| `out/target/product/sargo` | 14 GB | yes; packaging is rebuilt in minutes |

The home machine has 278 GB free once its old 24.0 tree is gone. Three clang versions (`r547379`, `r563880c`, `r584948b`) are 3.5-3.7 GB each and unused by this device, so the instance trims them with the same sparse checkout the home tree uses, then runs `m bacon` once more so the analysis there sees the trimmed tree.

---

#### Step 7 — Pull the Tree Home

`pull-tree.sh` runs on the home machine and copies the whole tree with `rsync -aHz --partial` and zstd compression, keeping file times and hard links.

PENDING: transfer size, time and AWS data-transfer charge.

---

#### Step 8 — Iterate Locally

PENDING: first local `m bacon` after the copy; expected to do little or no work.

PENDING: one device-tree edit, rebuild time at home.

---

#### Step 9 — Orchestrate With Claude Code

Claude Code ran every step from one session on a laptop:

- **Sizing:** quotas, on-demand prices, spot prices and spot placement scores through the AWS MCP server.
- **Launch:** key pair import, a security group for the home IP, and `RunInstances` with the volume settings.
- **Watching:** each long step runs in a `tmux` session on the instance; Claude Code waits on the log in a background job and is notified when the build exits.
- **Fixing:** reads the failed command from `out/siso_output`, traces the type or symbol through the tree, commits to the forks and resumes the build.
- **Memory:** the instance id, cost per hour and teardown rule go into Claude Code's project memory, so a later session finds a running instance.

The home machine is reached on the local network with its own credentials, used to stop the slow local build and free disk.

---

#### And What Does It Cost?

More than a home build. Prices from the AWS Pricing API, us-east-1:

| Item | Rate | One round trip |
|---|---|---|
| c7a.48xlarge on-demand | $9.85/h | PENDING |
| Data transfer out to home | $0.09/GB | PENDING |
| gp3 1000 GB at 16,000 IOPS and 1000 MB/s | $180/month, running or stopped | PENDING |
| m7a.xlarge for the copy home | $0.23/h | PENDING |

The copy home runs on an m7a.xlarge: stop the instance, change its type, start it. The volume and the tree stay, and two hours of copying cost $0.46 against $19.71 on the build instance. While the instance is stopped, the volume drops back to its baseline performance, which leaves $80 a month for the storage.

---

#### Compare and Contrast

| | i7-3770, home | c7a.48xlarge, EC2 |
|---|---|---|
| Threads | 8 | 192 |
| RAM | 31 GB + 79 GB swap | 384 GiB |
| Soong analysis | about 1 h 45 min, swapping | 🥇 included in the 19 min 17 s below |
| Build steps completed | 130,617 in 7 h 49 min | 🥇 156,615 in 19 min 17 s, analysis included |
| Phone on USB | 🥇 yes | no |
| Cost | electricity | $9.85/h on-demand |

---

#### So, Which One?

Both, in sequence. EC2 for the from-scratch build and for any change that re-runs Soong analysis; the home machine for edits that keep the build graph, flashing, and boot logs.

---

#### Teardown

PENDING: stop the instance once the tree is home; a stopped instance keeps the 1000 GB volume for the next full rebuild.

---

#### Summary

The goal of this project was to move the long part of a LineageOS 24.0 build off a 32 GB home machine without losing incremental builds next to the phone. The key to the solution was building on EC2 at the home machine's absolute path with the home user name, so the finished tree is portable. The results were:

- 🟢 Full sync of the LineageOS 24.0 tree in 15 minutes on a c7a.48xlarge.
- 🟢 Soong analysis plus 156,615 build steps in 19 min 17 s.
- 🟢 Four Android 17 port errors fixed between builds of 6 to 17 minutes each.
- 🟢 Fork commits carry the same hash on the instance and on GitHub.
- ⚠️ PENDING: clean build, transfer, first local build.
- ⚠️ The EC2 build ran without Soong's sandbox; Ubuntu 24.04 restricts the unprivileged user namespaces it needs.
- ❌ A `.bp` or `.mk` change still re-runs Soong analysis at home.

Scope: one c7a.48xlarge on-demand instance in us-east-1a running Ubuntu 24.04, one home machine (i7-3770, 31 GB, Debian sid), LineageOS 24.0 for sargo built 2026-10-23. Home figures come from the 24.0 port notes in the repository; EC2 figures from the build log on the instance. Parts of the analysis and writing were done with AI assistance (Claude).

The strategy for splitting a LineageOS build between EC2 and a home machine was validated with an incremental step by step approach.

---

#### References

- Build and pull scripts: https://github.com/xbill9/android_kernel_google_msm-4.9/tree/main/backport-tools/lineage-24.0/aws
- LineageOS 24.0 port notes: https://github.com/xbill9/android_kernel_google_msm-4.9/tree/main/backport-tools/lineage-24.0
- Device tree: https://github.com/xbill9/android_device_google_bonito/tree/lineage-24.0
- LineageOS 23.2 on the Pixel 3a: https://github.com/xbill9/android_kernel_google_msm-4.9/tree/main/backport-tools/lineage-23.2
- Amazon EC2 C7a instances: https://aws.amazon.com/ec2/instance-types/c7a/
