# LineageOS 24.0 on EC2, finished at home

The full build runs on an EC2 instance; the whole tree then comes home to the local build machine,
where the phone is, and local builds continue incrementally. Measured 2026-10-23 on a
c7a.48xlarge (192 vCPU, 384 GiB, us-east-1, on-demand $9.85/h) and the home machine `build`
(192.168.50.45, i7-3770, 31 GB, Debian sid).

| Script | Runs on | Does |
|---|---|---|
| `ec2-build.sh` | instance, in tmux | packages, repo, sync, `m bacon`; rerun resumes |
| `pull-tree.sh <ip>` | home machine | rsync the whole tree to the same path |

## Order of operations

1. Launch: Ubuntu 24.04, 1000 GB gp3 (16000 IOPS, 1000 MB/s), SSH from the home IP only.
2. **Before the first build**, trim the platform clang to the versions the device uses (see
   "Trim before building"). Doing it afterwards forces a near-full rebuild.
3. `ec2-build.sh` until `m bacon` produces the zip. Fix port errors on the instance, carry the
   commits home as patches, push.
4. Point the instance's repos at the pushed commits (`git reset --hard FETCH_HEAD` when the tree
   hashes match), so the copy matches GitHub.
5. `pull-tree.sh` on the home machine. Free the space first.
6. First local `m bacon`: should do little or nothing.

## Quirks

### The tree must live at the home machine's absolute path

The Soong graph uses relative paths; the Kati graph does not.

```
out/soong/build.lineage_sargo.ninja: 0 lines with /home/ubuntu of 3712391
out/build-lineage_sargo.ninja: 124 lines with /home/ubuntu of 5697004
```

`ec2-build.sh` builds in `/home/xbill/android/lineage-24.0` on the instance (`/home/xbill` created
and owned by `ubuntu`). Moving a built tree to a new path rebuilds most of it.

### BUILD_USERNAME must match the home user

Soong records it among the environment values that trigger a new analysis:

```
soong.environment.used.lineage_sargo.build BUILD_USERNAME ubuntu
```

A different value at home reruns Soong analysis, the step that needs about 75 GB of memory there.
`ec2-build.sh` exports `BUILD_USERNAME=xbill`. `BUILD_HOSTNAME` is not in that list and only
changes the build properties.

### Trim before building: git sparse-checkout rewrites the files it keeps

`git sparse-checkout set` on `prebuilts/clang/host/linux-x86` dropped the unused versions (19 GB to
7.4 GB) and also rewrote every kept file, so `clang-r596125/bin/clang` got a time newer than the
finished build:

```
2026-10-23 17:09:39 prebuilts/clang/host/linux-x86/clang-r596125/bin/clang
2026-10-23 17:08:03 out/target/product/sargo/lineage_sargo-ota.zip
```

The next `m bacon` had 186,251 steps to redo. Trim right after the sync. The same applies to any
git operation that rewrites prebuilts after a build.

The kept set matches `../fetch-clang-24.sh`: `soong docs kleaf mlgo-models profiles
embedded-sysroots llvm-binutils-stable clang-stable clang-3289846 clang-r584948 clang-r596125`, plus
an empty `PLACEHOLDER` in `clang-r547379`, `clang-r563880c` and `clang-r584948b` for the trusty
module group.

### Copy with rsync -aH, never a plain copy

`rsync -a` keeps file times, so the build tool sees the copy as up to date; `-H` keeps the hard
links (`lineage-24.0-*.zip` and `lineage_sargo-ota.zip` are one file). Move an old local tree
aside rather than merging into it.

### AOSP rate-limits a 32-job sync

One project answered `HTTP 429` on the first pass. `ec2-build.sh` retries the sync up to five
times; the second pass fetched it in a minute. Whole sync: 15 minutes.

### Ubuntu 24.04 disables the build sandbox

`kernel.apparmor_restrict_unprivileged_userns = 1` stops nsjail, and every build prints
`Build sandboxing disabled due to nsjail error.` The home machine (Debian sid) is unlikely to have
that restriction. Whether the mismatch costs work on the first local build is not yet measured.
`sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0` on the instance matches it.

### Spot is not available at 192 cores

Spot placement score for m7a.48xlarge was 1 of 10 in every us-east-1 zone. The build runs
on-demand. Smaller instances (64-96 vCPU) had spot at $0.86-1.36/h.

### The instance has no GitHub credentials

Commit on the instance, `git format-patch --binary`, copy home, `git am
--committer-date-is-author-date` with the same committer name and email, push. The hashes match
the instance's commits. Amending on the instance after that changes the committer date, and the
hashes no longer match; then `git fetch` GitHub on the instance and `git reset --hard FETCH_HEAD`
once `HEAD^{tree}` equals `FETCH_HEAD^{tree}` (no file changes).

### The kernel's final link is single-threaded

The LTO link (`aarch64-linux-android-ld.gold` with `LLVMgold.so`) ran over 6 minutes on one core
while the instance's load average fell to 1.6. The build sits at about 85 % during it.

### pkill -f over ssh can kill its own shell

`ssh host 'pkill -f soong_ui; ...'` matches the remote `bash -c` whose command line contains
`soong_ui`, and ssh exits 255 with nothing after it run. Stop the build with `tmux kill-session -t
build`, or `pgrep -x`.

### The home machine needs its own key and the allowed IP

The security group allows SSH from the home public IP only; the home machine shares it behind
NAT. It also needs a key in the instance's `authorized_keys` before `pull-tree.sh` can run.

### Disk at home

The tree was 267 GB with the build at 82 % (`.repo` 33 GB, `out` 130 GB, `out/soong/.intermediates`
90 GB). The home machine had 278 GB free once its old 24.0 tree was deleted. `.repo` stays: every
project's `.git` lives in it.

### The public IP changes on stop and start

Look it up again (`DescribeInstances`) after restarting a stopped instance, and pass it to
`pull-tree.sh`.
