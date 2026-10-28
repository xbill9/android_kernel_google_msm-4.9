# Test results: 24.0-20261028

Phone 937AY07L03, slot b, `24.0-20261028-UNOFFICIAL-sargo`, kernel
`4.9.337-g23ec590fc4aa` (with `MAP_FIXED_NOREPLACE`, `193f1371d912`), bonito at
`0e07c00`, SELinux enforcing. Built with `build-24-guarded.sh` after syncing only
the kernel and bonito (28 min 53 s); installed into slot b with
`ota-install-24.sh` (7 min 13 s). Tests run 2026-10-28 12:04 to 12:29.

| Suite | Result | Report |
|---|---|---|
| `smoke-test-24.sh` | 19 pass, 0 fail, 8 check | `~/sargo-logs/937AY07L03-smoke-20261028-1228/` |
| `hw-check-24.sh` | 20 pass, 0 fail, 2 skip | `~/sargo-logs/937AY07L03-hwcheck-20261028-1205/` |
| `vts-kernel-24.sh quick` | 10 modules pass, 3 fail, 4 skip; 637 tests pass, 78 fail | `~/sargo-logs/937AY07L03-vts-kernel-quick-20261028-1204/` |
| kselftest `mremap_test`, run alone | 17 pass, 6 fail, 3 expected fail; 2 s | `~/sargo-logs/937AY07L03-mremap-fixed-20261028-1204/` |

## mremap_test

On the 20261027 builds it looped forever in subtest 3 (see
[test-results-20261027.md](test-results-20261027.md)). With
`MAP_FIXED_NOREPLACE` it finishes in 2 s. The 6 failures need features 4.9
lacks:

| Subtest | Error |
|---|---|
| 16 mremap expand merge | (no message) |
| 22 move multiple vmas, 24 move multiple vmas split | `mremap: Bad address` |
| 23 and 25, the same with `MREMAP_DONTUNMAP` | `mremap: Invalid argument` |
| 26 move multiple invalid vmas | `userfaultfd: Function not implemented` (`CONFIG_USERFAULTFD=n`) |

## Smoke test

The first run, at 1 min of uptime, failed location (no fix yet) and the
barometer (no event since boot). The GPS fix came at 12:28:47, 6 min 30 s after
boot; the rerun at 12:28 passed both: 19 pass, 0 fail.

## Hardware check

- Rear and front camera photos, speaker at 49,440 frames/s, Bluetooth off and on
  (4 s, 1 s), NFC, modem, GNSS HAL, fingerprint HAL, screen off and on, battery
  (charging from a computer port), storage write 158 MB/s and read 314 MB/s. No
  crashes since boot.
- Wi-Fi pings average 120 ms to the router, 102 ms to 1.1.1.1 and 98 ms to
  dns.google.
- 26 SELinux denials in 8 kinds, down from 161 in 18 kinds on 937AY07L03 and 78
  in 13 kinds on 935AY06U23 the day before:

  | Count | Denial |
  |---|---|
  | 12 | `untrusted_app_34` -> `sysfs_msm_subsys` dir `search` |
  | 2 | `vendor_init` -> `default_prop` property_service `set` |
  | 2 | `sensors` -> `diag_device` chr_file `read write` |
  | 2 | `qtelephony` -> `default_prop` file `read` |
  | 2 | `nfc` -> `nfc_vendor_data_file` dir `search` |
  | 2 | `isolated_app` -> `content_capture_service` service_manager `find` |
  | 2 | `init` -> `sysfs_soc_sensitive` file `getattr` |
  | 2 | `hal_camera_default` -> `zygote_config_prop` file `read` |

  The `hal_camera_default` reads of `default_prop` (16) and
  `build_bootimage_prop` (8) are gone with bonito `0e07c00`. `sensors` ->
  `diag_device` is new: `sensors.qti` opening `/dev/diag`, the Qualcomm
  diagnostics device. The sensors work.

## VTS kernel, quick set

The same as on 935AY06U23 with the 14:44 build, module for module, except
`drop_caches_test`, which passed. `KernelDynamicPartitionsTest` and
`vts_virtual_ab_test` now show as SKIP. `vts_kernel_checkpoint_test` reboots the
phone as part of the test (three reboots, 12:20 to 12:23).

## Next

- `sensors` -> `diag_device`: check whether it comes only at boot.
- The 4 changeable network failures (see
  [test-results-20261027.md](test-results-20261027.md)).
- The `ueventd` `mac_admin` denial from 935AY06U23.
