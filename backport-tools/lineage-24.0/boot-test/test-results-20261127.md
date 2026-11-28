# Test results: 24.0-20261127

Phone 936AY07BAJ (Pixel 3a on yoga9i, no SIM), `24.0-20261127-UNOFFICIAL-sargo`
(release `lineage-24.0-sargo-20261127-1304`), kernel `4.9.337-ga0f3ef14d1cb`.
CTS modules that touch the kernel, run 2026-11-27 20:21 to 22:07 with
`cts-24.sh` through an SSH tunnel to yoga9i's adb server, Play Protect's adb
install check off. Report: `~/sargo-logs/936AY07BAJ-cts-kernel-20261127-2021/`
on build. CtsNetTestCases runs on every `sargo-test` (on the phone with the SIM):
1010 pass, 1 fail (`ApfApiTest#testReadWriteProgram`, Wi-Fi firmware).

| Module | 11-06 (92FAY00ZMV, SIM) | 11-27 (936AY07BAJ, no SIM) |
|---|---|---|
| `CtsKernelConfigTestCases` | 5 pass | 5 pass |
| `CtsSelinuxTargetSdkCurrentTestCases` | 10 pass | 10 pass |
| `CtsBionicTestCases` | 3281 pass, 4 fail | 3288 pass, 0 fail |
| `CtsOsTestCases` | 859 pass, 7 fail | 859 pass, 7 fail, the same 7 |
| `CtsPermissionTestCases` | 388 pass, 1 fail | 382 pass, 7 fail |
| `CtsSecurityTestCases` | no result after 6 tests | no result after 7 tests |
| `CtsGraphicsTestCases` | 3774 pass | 3774 pass |
| `CtsLibcoreTestCases` | 32,137 pass, 6 fail | 32,251 pass, 0 fail |

---

#### What changed

- **Bionic**: the four `sched.clone3*` cases pass (clone3 backported).
- **Libcore**: no failures. The six on 11-06 were network cases on a phone that
  had lost cellular service, and two socket cases.
- **Permission**: `testManageDocuments` fails as before (Seedvault holds
  `MANAGE_DOCUMENTS`). The other six are `SmsManagerPermissionTest` and
  `NoSystemFunctionPermissionTest#testSendSms`; the run says "Device does not
  have SIM card".
- **OS**: the same seven as 11-06: build fingerprint (says 12), empty global
  security state, no StrongBox, four `StrictModeTest` cases, one work-profile
  USB storage case.
- **Security**: the test app is killed at
  `ActivityManagerTest#testActivityManager_registerUidChangeObserver_onlyNoInteractAcrossPermission`:
  `Killing ...android.security.cts/u0a405 (adj 0): killBackgroundUserProcessesWithAudioRecordPermission`,
  when the run switches to its second user. The run leaves that user
  (`testUser`) running; `pm remove-user` takes it away.

---

#### VTS kernel (`vts-kernel-24.sh all`, the same phone, 22:08 to 23:33)

11 modules pass, 4 fail, 4 skip; 1635 tests pass, 13 fail. Report:
`~/sargo-logs/936AY07BAJ-vts-kernel-all-20261127-2208/`.

| Module | 11-06 | 11-27 |
|---|---|---|
| `drop_caches_test` | 0 pass, 1 fail | 1 pass |
| `vts_kernel_net_tests` | 577 pass, 70 fail | 647 pass, 0 fail |
| `f2fs_test` | 4 fail | 4 fail |
| `vts_test_binary_zram_module` | 1 fail | 1 fail (left out of the script since: see below) |
| kselftest | 24 pass, 1 fail (`net_psock_tpacket`) | 25 pass, 1 fail (`mm_uffd_unit_tests`) |
| LTP | 1127 pass, 13 fail | 900 pass, 7 fail (907 tests in this LTP build) |

Every other module passes or is skipped by the runner as before (dynamic
partitions and virtual A/B need a first API level of 29 and 30; the phone's is 28).

- **`f2fs_test`**: the test formats its image with f2fs compression (5.6) and
  casefolding (5.4); this f2fs has neither, so the mount fails.
- **`vts_test_binary_zram_module`** is `zram_module_test`'s binary without the
  config that roots the phone; as `shell` it may not read `/proc/swaps`
  (`avc: denied { read } ... tcontext=u:object_r:proc_swaps:s0`).
  `zram_module_test` passes.
- **`mm_uffd_unit_tests`**: `zeropage` on shmem fails to register
  (`errno=22`): userfaultfd on shmem arrived in 4.11 and this tree has the
  anonymous-memory part only.
- **LTP**: `getitimer01` and `setitimer01` read a 10000 µs interval back as
  13323 and 13332 µs, rounded to the 3333 µs tick (CPU-time timers in
  nanoseconds came with 4.11); `inotify07` and `inotify08` miss events on
  overlayfs after copy-up (4.13 overlayfs); `ioctl_ns07` and `munmap04` end
  in `TCONF` ("requires kernel 4.11" and "4.17"), which VTS counts as a
  failure; `mkdir09` passes on ext2, ext3 and ext4 and exits 255 while
  formatting ntfs (ntfs-3g over FUSE).
