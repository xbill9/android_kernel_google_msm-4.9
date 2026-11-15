# sargo kernel: backports and upstream fixes

The LineageOS 4.9 kernel for the Pixel 3a (`sargo`, `bonito_defconfig`), on `main`, with newer
eBPF and Android support backported onto LineageOS `lineage-22.2`. It runs LineageOS 24.0
(Android 17) daily on a Pixel 3a. The ROM, its build and its tests are in `lineage-24.0/`.

On top of the LineageOS base, in order: two replayed upstream BPF series (441 and 1642
commits; linux-5.4.y's `test_verifier` passes on it), arm64 uprobes, the kprobe/uprobe event options, the
arm64 JIT branch-offset fix, the verifier and JIT fixes `missing_fixes.py` finds for that code
(`fixes-done.tsv`), what Android 16 and 17 need (below), and upstream fixes since,
committed one by one. The series scripts and their lists were retired on 2026-11-15; they are
in git history before that date.

## What this directory holds

| File | Does |
|---|---|
| `check-upstream.sh` | fetches mainline, runs `missing_fixes.py`, lists the fixes new since `missing-fixes.tsv` in code the phone builds and not yet decided; `--apply` commits the ones that apply as is, `--write` refreshes the table |
| `missing_fixes.py` | lists upstream commits whose `Fixes:` tag names a commit in this tree, when the fix is absent; repeats for fixes of fixes |
| `missing-fixes.tsv` | the last full table (also the backlog of older fixes to the LineageOS base) |
| `fixes-done.tsv` | each fix decided on and what was done with it: ported (with the commit), n/a or already in, and why |
| `lineage-24.0/` | the ROM: build, install, test (`lineage-24.0/README.md`) |
| `lineage-23.2/` | the 23.2 article's cover image; the 23.2 build is retired |

## Taking upstream fixes

    backport-tools/check-upstream.sh --apply   # commit the new fixes that apply as is
    # hand-merge the rest, or record them in fixes-done.tsv as n/a with the reason
    sargo-test --push                          # RAM-boot test on the phone; pushes on PASS
    backport-tools/check-upstream.sh --write   # refresh the table, so the next run lists only newer fixes

`check-upstream.sh` needs the mainline clone (once, no file contents):

    git clone --bare --filter=blob:none https://github.com/torvalds/linux.git ~/sargo-bpf-test/linux-upstream.git

Columns of its rows:

- `class`: what the fix touches. `built` is a `.c` file with an object in the test build
  (`/android/sargo-bpf-test/kout`); then `header`, `unbuilt`, `absent`, `tools`, `other-arch`.
  Only `built` and `header` rows matter for this phone.
- `target_origin`: `backport` if the fixed commit came in with the backport, `base` if it was in
  LineageOS already. `-via-missing`: the row fixes another missing fix.
- `apply`: for backport targets, whether the patch applies as is (`clean`), only in reverse
  (`already`, so it is in), or not (`conflict`, hand merge).

Commit messages: `UPSTREAM: <subject>` for a cherry-pick, `BACKPORT: <subject>` for a hand merge
with a `[ Context: what differs on 4.9 ]` line, then `(cherry picked from commit <full hash>)`.
A fix whose code is not built for sargo, or whose bug cannot happen on 4.9, is not taken; it
goes in `fixes-done.tsv` as `n/a` with the reason.

## What Android 16 and 17 need from a 4.9 kernel

LineageOS lists 4.14 as the minimum for 23.x and 24.0. Each of these stopped init on 4.9:

- `MADV_WIPEONFORK` (4.14, `d2cd9ede6e19`): bionic's arc4random aborts on `EINVAL`.
- Netlink extended permissions in the SELinux policy (xperm type `0x03`): `bf173c3ec796` reads
  and drops them; 4.9 keeps checking the `nlmsg_*` permissions the policy also grants.
- The `cpuset_v2_mode` cgroup mount option (4.17, `e1cba4b85daa`, `b8d1b8ee93df`).
- For 24.0 also: BPF ring buffers (bpfloader), capabilities 38-40 (`zygote_next`), EROFS (the
  MindTheGapps installer), and the userspace forks in `lineage-24.0/README.md`.

## Known limitations

- Kprobes and CFI: a function body is named `<name>.cfi`; a kprobe on the plain name attaches
  but never fires. Probe `<name>.cfi` (see `/proc/kallsyms`). The perf kprobe PMU accepts
  `.cfi` names.
- `wlan.ko` must come from the same build: struct changes alter the symbol CRCs it checks
  (`disagrees about version of symbol`). The ROM build makes a matching one.
- `missing_fixes.py` finds missing upstream fixes only. Two local commits that do not fit
  together show up on the phone instead, as a kernel warning; `sargo-test` fails on those.
