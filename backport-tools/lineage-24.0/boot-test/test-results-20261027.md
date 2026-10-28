# Test results: 24.0-20261027, 14:44 build

Phone 935AY06U23 on `build`, slot b, `24.0-20261027-UNOFFICIAL-sargo` (build date
Tue Oct 27 14:44:42 EDT 2026), kernel `4.9.337-g25a78bbc4088`, SELinux enforcing,
MindTheGapps 17 20261022. Installed with `phone-install-24.sh`; bonito at `3dbaf3e`
(without `0e07c00`). Tests run 2026-10-27 21:39 to 22:18.

| Suite | Result | Report |
|---|---|---|
| `smoke-test-24.sh` | 18 pass, 1 fail, 8 check | `~/sargo-bpf-test/logs/smoke-1027-2139/` |
| `hw-check-24.sh` | 20 pass, 0 fail, 2 skip | `~/sargo-logs/935AY06U23-hwcheck-20261027-2140/` |
| `vts-kernel-24.sh quick` | 10 modules pass, 5 fail, 2 skip; 637 tests pass, 78 fail | `~/sargo-logs/935AY06U23-vts-kernel-quick-20261027-2141/` |

## Smoke test

- The one FAIL was the barometer: `no event in the last minute`. The barometer
  works; it read 1021.4 hPa at 21:40:03, when Play Services sampled it, three
  seconds after the check. Nothing keeps it open, so the check now accepts any
  event since boot (`dc02ae4`).
- CHECK items (need a person or a SIM): 15 SELinux denials, modem without SIM,
  NFC tag, GPS fix, front camera, fingerprint enrolment, audio by ear, sensor list
  (39 sensors).
- Passes include Wi-Fi ping and DNS, Bluetooth, network location, rear camera
  photo, vibrator, Titan M services, Play Services, Chrome, `chre` stopped,
  `qtidataservices` running, no crashes since boot.

## Hardware check

- Rear and front camera photos, speaker output at 51,600 frames/s, Bluetooth off
  and on, NFC, modem, GNSS HAL, fingerprint HAL, screen off and on, battery,
  storage write 159 MB/s and read 310 MB/s. No crashes since boot.
- Skipped: SIM (absent) and microphone.
- Wi-Fi pings average 154 ms to the router, 168 ms to 1.1.1.1 and 163 ms to
  dns.google, matching the ~100 ms steps in [README.md](README.md).
- 78 SELinux denials in 13 kinds:

  | Count | Denial |
  |---|---|
  | 18 | `isolated_app` -> `app_data_file` file `setattr` |
  | 16 | `hal_camera_default` -> `default_prop` file `read` |
  | 14 | `untrusted_app_34` -> `sysfs_msm_subsys` dir `search` |
  | 8 | `hal_camera_default` -> `build_bootimage_prop` file `read` |
  | 6 | `priv_app_36` -> `sysfs_msm_subsys` file `read` |
  | 2 | `vendor_init` -> `default_prop` property_service `set` |
  | 2 | `ueventd` -> `ueventd` capability `mac_admin` |
  | 2 | `qtelephony` -> `default_prop` file `read` |
  | 2 | `nfc` -> `nfc_vendor_data_file` dir `search` |
  | 2 | `isolated_app` -> `content_capture_service` service_manager `find` |
  | 2 | `init` -> `sysfs_soc_sensitive` file `getattr` |
  | 2 | `hal_camera_default` -> `zygote_config_prop` file `read` |
  | 2 | `artd` -> `app_data_file` lnk_file `read` |

  The two `hal_camera_default` property reads at the top (24 of the 78) are the
  ones bonito `0e07c00` fixes; it is on the fork and not in this build. The
  `isolated_app`, `sysfs_msm_subsys`, `ueventd` and `artd` denials are new in
  this report and not yet looked into.

## VTS kernel, quick set

| Module | Result |
|---|---|
| drop_caches_test | PASS 1 |
| vts_kernel_net_tests | FAIL 574 pass, 73 fail, 6 ignored |
| vts_kernel_encryption_test | PASS 26 |
| vts_kernel_loopconfig_test | PASS 2 |
| vts_memfd_class_selinux_test | SKIP (1 ignored) |
| KernelApiSysfsTest | PASS 10, 2 skipped |
| KernelAbilistTest | PASS 1 |
| KernelDynamicPartitionsTest | FAIL, no results from TradeFed |
| vts_kernel_checkpoint_test | PASS 3 |
| vts_kernel_fuse_bpf_test | PASS 1 |
| f2fs_test | FAIL 0 pass, 4 fail |
| vts_kernel_toolchain | PASS 5, 1 ignored |
| vts_kernel_isa_test | SKIP (1 ignored) |
| zram_module_test | PASS 1 |
| vts_test_binary_zram_module | FAIL 0 pass, 1 fail |
| vts_virtual_ab_test | FAIL, no results from TradeFed |
| vts_test_binary_bow_module | PASS 13 |

Failures:

- **Networking, 73.** By test class:

  | Count | Class |
  |---|---|
  | 48 | `xfrm_tunnel_test.XfrmInterfaceMigrateTest` |
  | 13 | `multinetwork_test.RIOTest` (`accept_ra_rt_info_*`, `accept_ra_min_lft`) |
  | 3 | `sysctls_test.SysctlsTest` (congestion control) |
  | 2 | `multinetwork_test.OutgoingTest` (`testIPv4Remarking`, `testIPv6Remarking`) |
  | 2 | `tcp_fastopen_test.TcpFastOpenTest` (`testConnectOption*`) |
  | 2 | `xfrm_test.XfrmFunctionalTest` (`testIPv6UDPEncapRecv*`) |
  | 1 | `sock_diag_test.TcpRcvWindowTest#testTcpCwndSize` |
  | 1 | `kernel_feature_test.KernelFeatureTest#testRemovedAndroidParanoidNetwork` |
  | 1 | `bpf_test.BpfTest#testGetNetNsCookie` |

  Not yet sorted into features missing from 4.9 and bugs.
- **f2fs, 4.** Each test's setup mounts a loop image with `compress_mode=user`
  (`F2fsTest.cpp:79`), which returns -1: the 4.9 f2fs has no compression.
- **zram, 1.** `ZramTest.cpp:50: Failed to read /proc/swaps`. The phone has zram
  swap (`/dev/block/zram0`, 2 GB, in use), so the test process cannot read the
  file; cause not found.
- **KernelDynamicPartitionsTest, vts_virtual_ab_test.** Both modules built;
  TradeFed reported "No test results available". No test ran.

## Next

- Sync and build to pick up bonito `0e07c00`, then repeat the three suites.
- Sort the 73 network failures into missing features and kernel bugs.
- Find what blocks the zram test's read of `/proc/swaps`.
- Look into the new `isolated_app`, `sysfs_msm_subsys`, `ueventd` and `artd` denials.
