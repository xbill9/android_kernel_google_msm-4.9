# Test results: 24.0-20261127

Phone 936AY07BAJ (Pixel 3a on yoga9i, no SIM), `24.0-20261127-UNOFFICIAL-sargo`
(release `lineage-24.0-sargo-20261127-1304`), kernel `4.9.337-ga0f3ef14d1cb`.
CTS modules that touch the kernel, run 2026-11-27 20:21 to 22:58 with
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
