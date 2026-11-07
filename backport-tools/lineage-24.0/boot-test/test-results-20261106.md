# Test results: 24.0-20261106

Phone 92FAY00ZMV (Pixel 3a, Google Fi SIM, Wi-Fi off, NFC off), slot b,
`24.0-20261106-UNOFFICIAL-sargo`, kernel `4.9.337-g610bab6d4d15`, SELinux
enforcing. Built from an empty `out/` with `sargo-build` (9 h 27 min), installed
with `sargo-install` (22:12 to 22:29). Tests run 2026-11-06 22:29 to 2026-11-07
05:50.

| Suite | Result | Report |
|---|---|---|
| `hw-check-24.sh` | 16 pass, 2 fail, 1 skip | `~/sargo-logs/92FAY00ZMV-hwcheck-20261106-2229/` |
| `smoke-test-24.sh` | 18 pass, 1 fail, 8 check | `~/sargo-logs/92FAY00ZMV-smoke-20261106-2228/` |
| `vts-kernel-24.sh all` | 9 modules pass, 6 fail, 4 skip; 1790 tests pass, 90 fail | `~/sargo-logs/92FAY00ZMV-vts-kernel-all-20261106-2228/` |
| `cts-24.sh core` (17 modules) | 4 modules pass, 10 fail, 3 skip; 41,199 tests pass, 249 fail | `~/sargo-logs/92FAY00ZMV-cts-core-20261107-0029/` |

---

#### Hardware check and smoke test

- Hardware check fails Wi-Fi (switched off, no saved network) and NFC
  (`mState=off`); both are settings on this phone. The modem and SIM pass for
  the first time (`LOADED, network Project Fi LTE`). Both cameras, speaker,
  vibrator, fingerprint HAL, display, battery, storage (write 216 MB/s, read
  296 MB/s), no crashes since boot.
- Smoke test fails `chrome: no renderer`; it passed on 10-28. Not looked at yet.

---

#### VTS kernel

Every module gives the same result as on 10-28 (937AY07L03) except
`drop_caches_test`:

| Module | 10-28 | 11-06 |
|---|---|---|
| `drop_caches_test` | 1 pass | 0 pass, 1 fail |
| `vts_kernel_net_tests` | 577 pass, 70 fail | 577 pass, 70 fail |
| `f2fs_test` | 4 fail | 4 fail |
| `vts_test_binary_zram_module` | 1 fail | 1 fail |
| kselftest | 24 pass, 1 fail (`net_psock_tpacket`) | the same |
| LTP | 1125 pass, 13 fail | 1127 pass, 13 fail, the same 13 tests |

`drop_caches_test` measured 124 major faults after dropping the cache against
128 minor faults before, a difference of 4 where the test allows 2
(`drop_caches_test.cpp:139`).

---

#### CTS

| Module | Result |
|---|---|
| `CtsKernelConfigTestCases` | 5 pass |
| `CtsSelinuxTargetSdkCurrentTestCases` | 10 pass |
| `CtsBionicTestCases` | 3281 pass, 4 fail |
| `CtsSecurityTestCases` | no result: the test app was killed after 6 tests |
| `CtsSecurityHostTestCases` | no result: helper APK install did not answer |
| `CtsPermissionTestCases` | 388 pass, 1 fail |
| `CtsOsTestCases` | 859 pass, 7 fail |
| `CtsNetTestCases` | no result: helper APK refused by Play Protect |
| `CtsGraphicsTestCases` | 3774 pass |
| `CtsMediaMiscTestCases` | no result: test APK refused by Play Protect |
| `CtsSensorTestCases` | stopped at 1 h after 270 of 440: 264 pass, 6 fail |
| `CtsHardwareTestCases` | 174 pass, 230 fail; 220 of the failures are `Not connected!`, see below |
| `CtsCameraTestCases` | stopped at 1 h after 669 of 972: 645 pass, 4 fail |
| `CtsBluetoothTestCases` | 473 pass |
| `CtsLocationFineTestCases` | 98 pass, 1 fail |
| `CtsTelephonyTestCases` | stopped at 1 h after 721 of 1513: 649 pass, 19 fail |
| `CtsLibcoreTestCases` | 32,137 pass, 6 fail |

Counts for the modules stopped at the time limit are taken from their logs;
`cts-24.sh` reports those as "no result".

Failures and what they show:

- **Bionic**: the four `sched.clone3*` tests. `clone3` returns `ENOSYS`; the
  call arrived in Linux 5.3.
- **Permission**: `testManageDocuments` finds `com.stevesoltys.seedvault`
  (LineageOS's backup app) holding `MANAGE_DOCUMENTS`.
- **OS**: `testBuildFingerprint` (expected `17`, the build fingerprint says
  `12`); `testGetGlobalSecurityState` (expected `2022-05-05`, got an empty
  string); `testStrongboxConstants` (`Actual: unsupported`); four
  `StrictModeTest` cases (no app handles `image/jpeg`); one work-profile USB
  storage case.
- **Sensors**: six magnetometer (`AK0991X`) batching cases, jitter 869 µs where
  the test allows 800 µs.
- **Camera**: three cases where the rear camera did not settle exposure and
  white balance (`Can not converge AE and AWB within 150 frames`), with the
  phone in a dark room; one focus-distance case.
- **Location**: `testAddProximityAlert` timed out at 300 s.
- **Telephony**: eleven `SmsManagerTest` cases timed out waiting for a text to
  the phone's own number; two found the owner's accounts; one needs a
  voice-capable SIM; five single cases.
- **Libcore**: four network cases fail with `ENETUNREACH` or no DNS, because the
  phone had no cellular service by then (below); two socket cases.

Effects of the test run on the phone:

- **Play Protect** asks for confirmation of some APKs installed over adb
  (`verifier_verify_adb_installs=1`) and refuses them after 10 s. This took out
  the network and media modules and likely the security host module. CTS needs
  `settings put global verifier_verify_adb_installs 0`.
- **A timed-out module keeps running.** `timeout` stops `atest` but not the
  tradefed run under it, which keeps the phone's UI automation connection; the
  next module's tests fail with `UiAutomationService ... already registered` and
  `Not connected!`. This is the 220 hardware failures. `cts-24.sh` now kills the
  leftover run after a time-out.
- **`CtsSecurityTestCases` left a second user** (`testUser`, user 11) running.
- **`CtsTelephonyTestCases`, stopped at the time limit, left the phone with no
  cellular service.** The test binds the phone process to a simulated modem
  (`android.telephony.mockmodem.MockModemService`); afterwards the binding was
  still there, dead, and the time zone was `Asia/Taipei`. After a reboot the
  binding is gone and the radio is on, the SIM is `LOADED`, airplane mode is
  off, mobile data is on and the network mode is `10`, yet the modem reports
  `rssi=-113` and `NOT_REG_SEARCHING`; an airplane-mode toggle did not change
  it. Not resolved. `cts-24.sh core` no longer includes this module.
