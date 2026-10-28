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
  others, checked against the 937AY07L03 logs of the same night:

  - `untrusted_app_34` -> `sysfs_msm_subsys` dir `search`: apps' GPU threads
    (`RenderThread`, Chrome's `CrGpuMain`) look up `5000000.qcom,kgsl-3d0`,
    which bonito labels `sysfs_msm_subsys`
    (`sepolicy/vendor/qcom/common/genfs_contexts:52`). Rendering works.
  - `isolated_app` -> `app_data_file` file `setattr`: Chrome's isolated process
    on its own downloads (`model.crx3`, `manifest.json`,
    `verified_contents.json`). Platform rules forbid isolated apps writing app
    data, so device policy cannot allow it.
  - `artd` -> `app_data_file` lnk_file `read`: the app compiler following an
    app's `lib` link; twice on 937AY07L03.
  - `ueventd` -> `ueventd` capability `mac_admin`: not in the 937AY07L03 logs;
    needs the 935AY06U23 log.

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
| KernelDynamicPartitionsTest | SKIP, first API level 28 is below 29 |
| vts_kernel_checkpoint_test | PASS 3 |
| vts_kernel_fuse_bpf_test | PASS 1 |
| f2fs_test | FAIL 0 pass, 4 fail |
| vts_kernel_toolchain | PASS 5, 1 ignored |
| vts_kernel_isa_test | SKIP (1 ignored) |
| zram_module_test | PASS 1 |
| vts_test_binary_zram_module | FAIL 0 pass, 1 fail |
| vts_virtual_ab_test | SKIP, first API level 28 is below 30 |
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

  The net tests run in their own network namespace (`kernel/tests/net/test/namespace.py:142`).
  69 of the 73 need features that 4.9 lacks:

  | Count | Class | What the kernel is missing |
  |---|---|---|
  | 48 | `XfrmInterfaceMigrateTest` | `xfrm_do_migrate` passes no interface ID to `xfrm_migrate` (`net/xfrm/xfrm_user.c:2406`), so migrating a state on an XFRM interface finds nothing: `ESRCH` |
  | 13 | `RIOTest` | `accept_ra_min_lft` is absent from `net/ipv6/addrconf.c` |
  | 3 | `SysctlsTest` | The congestion control sysctls exist, but in the global `ipv4_table` (`net/ipv4/sysctl_net_ipv4.c:523`, `587`, `593`), which a new network namespace does not see |
  | 2 | `TcpFastOpenTest` | `tcp_fastopen_blackhole_timeout_sec` is absent |
  | 2 | `XfrmFunctionalTest` | IPv6 ESP in UDP: `EINVAL`; `net/ipv6/esp6.c` has no UDP encapsulation |
  | 1 | `BpfTest#testGetNetNsCookie` | `SO_NETNS_COOKIE` is absent: `ENOPROTOOPT` |

  The other 4 are behaviour or configuration that can change:

  | Count | Test | Cause |
  |---|---|---|
  | 2 | `OutgoingTest#testIPv4Remarking`, `testIPv6Remarking` | Setting `SO_MARK` updates `sk_mark` and leaves the socket's cached route (`net/core/sock.c:951`), so a connected UDP socket keeps sending by the old route. A backport candidate |
  | 1 | `KernelFeatureTest#testRemovedAndroidParanoidNetwork` | `CONFIG_ANDROID_PARANOID_NETWORK=y` (`net/Kconfig:95` defaults to y): a process in the `net_raw` group can open an `AF_PACKET` socket, and the test expects `EPERM` |
  | 1 | `TcpRcvWindowTest#testTcpCwndSize` | The test requires `/proc/sys/net/ipv4/tcp_default_init_rwnd` to be gone; this kernel still has Android's old sysctl (`net/ipv4/sysctl_net_ipv4.c:1054`) |
- **f2fs, 4.** Each test's setup mounts a loop image with `compress_mode=user`
  (`F2fsTest.cpp:79`), which returns -1: the 4.9 f2fs has no compression.
- **zram, 1.** `ZramTest.cpp:50: Failed to read /proc/swaps`. The phone has zram
  swap (`/dev/block/zram0`, 2 GB, in use). The module runs as the shell user
  (`RootTargetPreparer` with `force-root` false), and the platform SELinux policy
  lets only `system_server`, `recovery`, `mmd`, `mmd_setup`, `fsck` and `e2fs`
  read `proc_swaps`. As the shell user, `cat /proc/swaps` gives `Permission
  denied`; the same test binary run as root passes.
- **KernelDynamicPartitionsTest, vts_virtual_ab_test.** TradeFed skips both:
  `Skipping module ... because API Level 28 from ro.product.first_api_level is
  less than 29` (30 for virtual A/B). sargo launched on API 28. `vts-kernel-24.sh`
  reports them as SKIP since `85f9252`.

## VTS kernel, kselftest and LTP (937AY07L03)

`vts-kernel-24.sh all` on 937AY07L03, slot a, the earlier `24.0-20261027` build
(kernel `4.9.337-g5a72ebfd04ec`), 2026-10-27 20:23 to 22:01. Its quick set gave
the same results as above, except `drop_caches_test`, which failed once (4 pages
stayed cached; the test allows 2) and passed on an earlier run. Report:
`~/sargo-logs/937AY07L03-vts-kernel-all-20261027-2023/`.

| Module | Result |
|---|---|
| vts_linux_kselftest_arm_64 | FAIL 23 pass, 2 fail, 4 ignored |
| vts_ltp_test_arm_64 | FAIL 1125 pass, 13 fail, 386 ignored |

- **`mm_mremap_test` never ends on 4.9.** Run alone with a terminal on
  2026-10-28, it passes subtests 1 and 2 and then spins in user space in
  subtest 3, "Source Address Misaligned (1KB-aligned)". `get_source_mapping()`
  asks `mmap` for an exact address with `MAP_FIXED_NOREPLACE`, which 4.9 does
  not know: the kernel ignores the flag, treats the address as a hint and
  returns a 4 KB-aligned address, which never passes the test's 1 KB-aligned
  check, so it unmaps and retries forever. The phone stayed responsive for the
  20 minutes it ran (1.43 GB available, swap untouched, nothing in the kernel
  log). The night before, TradeFed logged `Device 937AY07L03 package manager is
  unresponsive` 9 minutes into the test (20:56:07 to 21:05:13) and rebooted the
  phone; why the package manager stopped answering that night is not known.
  The kernel now has `MAP_FIXED_NOREPLACE` (upstream `a4ff8e8620d3` with the
  overlap fix `7aa867dd8952`, value `0x100000` as in 4.17 and bionic); it is
  not yet on a phone.
- **`net_psock_tpacket`.** TPACKET_V1, V2 and V3 receive rings and V1, V2
  transmit rings pass 100 packets each; the V3 transmit ring fails with `EINVAL`,
  which 4.9 returns on purpose (`net/packet/af_packet.c:4332`).
- **LTP, 7 for features 4.9 lacks** (LTP reports TCONF, VTS counts it as a fail):
  `ioctl_ns07` (needs 4.11), `munmap04` (needs 4.17), `unshare03` (`clone3`),
  `userfaultfd03` and `userfaultfd04` (`CONFIG_USERFAULTFD=n`), `statx10` and
  `statx12` (`statx` gives `ENOSYS`).
- **LTP, 5 that behave differently:** `getitimer01` and `setitimer01` (a
  10,000 µs interval reads back as 13,323 and 13,332 µs; `CONFIG_HZ=300`, a
  3,333 µs tick), `readahead01` (`readahead()` on a directory succeeds),
  `inotify07` and `inotify08` (no events from files on overlayfs).
- **LTP, 1 timeout:** `pty09`.
- The LTP module stops the Android framework for its run (`StopServicesSetup`),
  so the screen is black until it ends.

## Next

- Build with `MAP_FIXED_NOREPLACE` and rerun kselftest `mm_mremap_test`.
- Sync and build to pick up bonito `0e07c00`, then repeat the three suites.
- The 4 changeable network failures each need more than one change:
  - `SO_MARK`: upstream `50254256f382` (5.0). Its 4.4 stable backport was
    dropped for breaking Android runtime tests as "an incomplete backport".
  - `CONFIG_ANDROID_PARANOID_NETWORK=n` is safe only while netd's BPF program
    blocks socket creation for apps without INTERNET.
    `prog_netd_cgroupsock_inet_create` is loaded on 937AY07L03; that it blocks
    such an app is untested.
  - `tcp_default_init_rwnd` sets the initial receive window
    (`net/ipv4/tcp_output.c:190`); the test then asks for at least 64,000
    bytes, so removing the sysctl needs the newer window code with it.
- Find the `ueventd` `mac_admin` denial in the 935AY06U23 log.
