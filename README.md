# sargo BPF backport tooling

Scripts and bookkeeping used to backport newer eBPF support onto the
LineageOS 4.9 kernel for the Pixel 3a (`sargo`, `bonito_defconfig`).
The kernel itself lives on the other branches of this repo:

- `bpf-backport-s2` — current work: 1642-commit stage-2 series replayed onto
  `lineage-22.2`, plus build fixes and a CFI fix. Boots on a Pixel 3a.
- `bpf-backport` — stage 1 (441-commit series in `series.txt`).

## Build

`build.sh` builds `Image.lz4` the way LineageOS does: clang r416183b with
GNU binutils from the Android 4.9 prebuilts, full LTO, CFI. Expects
`~/sargo-bpf-test/{ksargo,kout,tc}`. `build-base.sh` builds the unmodified
base tree for comparison.

## Replaying a series

`next.sh` (stage 1) and `next2.sh` (stage 2) cherry-pick `series*.txt` one
commit at a time, resuming from the index in `pos*.txt`, and stop at the
first conflict that needs a human. `next2.sh` also skips indices listed in
`skiplist2.txt` and auto-drops conflicts confined to subsystems that are
absent from the 4.9 tree or not built for sargo.

| File | Contents |
| --- | --- |
| `series.txt`, `series2.txt` | commits to replay, in order |
| `stage2-range.txt` | base and tip of the stage-2 source range |
| `skiplist2.txt` | series indices skipped by hand |
| `skipped2.tsv` | every skipped commit, with the reason |
| `autodropped2.tsv` | files dropped from conflicting commits automatically |
| `unbuilt2.tsv` | conflicting files in drivers not built for sargo |
| `groundwork2.txt` | prerequisite commits pulled in ahead of the series |
| `mapper.txt` | BPF helper ID → name table, for checking helper ABI order |

`conflict_test.sh REPO BASE TIP OUT` dry-runs a range and classifies each
commit as clean, already present, or conflicting. `resolve.py FILE
ours|theirs|union` resolves every conflict hunk in a file one way.

## Boot testing

Repack the stock boot image with the new kernel and RAM-boot it; nothing is
flashed, and a reboot returns to stock:

    python3 tools/mkbootimg.py <stock args, --kernel kout/arch/arm64/boot/Image.lz4> --output boot-test.img
    adb reboot bootloader && fastboot boot boot-test.img

If it panics, the phone falls back to the flashed kernel. Run
`adb bugreport` before rebooting again: on a user build, dumpstate copies
`/sys/fs/pstore/console-ramoops-0` into the report as `LAST KMSG`.
