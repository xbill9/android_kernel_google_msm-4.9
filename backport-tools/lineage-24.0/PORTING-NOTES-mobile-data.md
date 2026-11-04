# Mobile data on Android 17 over an old Qualcomm modem: porting notes

Found on 2026-11-03 and 2026-11-04 bringing up a Google Fi data-only SIM on the
Pixel 3a (sargo, SDM670, kernel 4.9) with unofficial LineageOS 24.0. The same
SIM gets data on stock (SP2A.220505.008), on an iPhone, and on a OnePlus N200
running official LineageOS 24. Each item below says what to look for on another
tree, and whether it was confirmed on the phone.

Symptoms on sargo before the fixes: the phone registers on 310260 LTE at boot,
loses registration a few seconds later, then searches and is denied by 311480
and 310410 (causes 15 and 13). The state survives modem restarts and reboots;
flashing stock clears it.

---

#### 1. IMS switched off by carrier config takes LTE down with it

**Confirmed.** With `carrier_volte_available_bool=false`, Android 17 sends the
modem `VoLTE setting : 0` and `ims_service_enabled = FALSE` about one second
before registration is lost:

```
ImsManager: updateVoiceCellFeatureValue: available = false ... voLteFeatureOn = false
qcril_qmi_imss_request_set_ims_srv_status_v02: VoLTE setting : 0
qcril_qmi_imss_request_set_ims_registration_v02: ims_service_enabled = FALSE
```

A voice-centric modem with no voice over LTE gives up LTE; T-Mobile has no 3G
to fall back to, so the phone wanders to other networks and is denied. Stock
gets this key from Google's CarrierSettings, which Lineage does not ship.

Setting it on a running phone brought registration and data back after a modem
restart (17 of 18 five-second samples in service, IPv6 ping 2/2):

```
adb shell cmd phone cc set-value -s 0 -p carrier_volte_available_bool true
adb shell 'echo restart > /sys/kernel/debug/msm_subsys/modem'
```

**Check on another tree:** `cmd phone cc get-value carrier_volte_available_bool`
for the SIM, and `grep -E 'VoLTE setting|ims_service_enabled'` in logcat right
before any loss of registration. The build-time fix (a carrier config overlay
for the carrier) is not written yet.

---

#### 2. clatd exits with status 127: kernels before 5.11 lack close_range

**Confirmed; fixed by a kernel backport.** Fi's data
network is IPv6-only, and IPv4 goes through clatd (464XLAT). On sargo the CLAT
tracker sits in `STARTING`, `v4-rmnet_data0` appears and disappears, and logcat
repeats:

```
jniClatCoordinator: clatd process 10827 terminated status=32512
```

32512 is exit code 127. `ClatCoordinator.cpp` starts clatd with
`posix_spawn(..., POSIX_SPAWN_USEVFORK | POSIX_SPAWN_CLOEXEC_DEFAULT)`, and
bionic (`libc/bionic/spawn.cpp`) handles the second flag in the child with:

```
if (close_range(3, ~0U, CLOSE_RANGE_CLOEXEC)) _exit(127);
```

`close_range` is Linux 5.9 and `CLOSE_RANGE_CLOEXEC` is 5.11, so on 4.9 the
child exits before exec. Run by hand, the same clatd binary starts and prints
its usage line, which rules out the binary, its libraries and its SELinux label.

The fix in this kernel backports `close_range` as syscall 436, wired like the
existing `pidfd_open` backport: `include/uapi/linux/close_range.h`,
`fs/file.c`, `include/linux/syscalls.h`, `include/uapi/asm-generic/unistd.h`
(`__NR_syscalls` 437), `arch/arm64/include/asm/unistd32.h` and
`arch/arm64/include/asm/unistd.h` (`__NR_compat_syscalls` 437).

With that kernel flashed alone (`fastboot flash boot_b`, data kept), clatd
comes up on `v4-rmnet_data2` with `192.0.0.4`, `8.8.8.8` routes through it,
and on mobile data with no Wi-Fi network connected:

```
ROM + close_range kernel (IPv4 ping): data in service 18/18 samples, timeline [##################], ping: 0% packet loss
```

`ping google.com` and an IPv6 ping to `2001:4860:4860::8888` also answered
3 of 3. libsigchain logs one stack trace from `posix_spawn` when clatd starts;
no crash follows.

**Check on another tree:** any kernel older than 5.11 under Android 17 will hit
this for every caller of `POSIX_SPAWN_CLOEXEC_DEFAULT`, not only clatd. Grep
logcat for `status=32512`; on the device, `ping 8.8.8.8` fails with
`Network is unreachable` while IPv6 works.

---

#### 3. Lineage turns mobile data off by default

**Confirmed.** `vendor/lineage/config/telephony.mk` sets
`ro.com.android.mobiledata=false`; stock ships it on. With data off, the only
connection after attach is IMS, requested by `com.qualcomm.qti.cne`. When cne
dropped the request about one second later, Android tore the connection down
(`onTearDown: reason=CONNECTIVITY_SERVICE_UNWANTED`), the last connection on
LTE was gone, and registration was lost three seconds later. Changed to `true`
in this tree.

---

#### 4. The Lineage APN list has no Fi entry for 310260

**Confirmed.** Stock Pixels take APNs from CarrierSettings (protobuf under
`/product/etc/CarrierSettings`), so the Lineage APN list
(`vendor/apn/upstream/apns-conf.xml`) is all a ROM has. It has `h2g2` only for
310240 with a GID match, so a Fi SIM on 310260 fell back to T-Mobile's
`fast.t-mobile.com`. Added to `vendor/apn/US.xml`:

```
<apn mcc="310" mnc="260" carrier="Google Fi" apn="h2g2" authtype="3"
     mmsc="http://mmsc1.g-mms.com/mms/wapenc" modem_cognitive="true"
     mvno_match_data="31026097" mvno_type="imsi" profile_id="0"
     protocol="IPV6" roaming_protocol="IP"
     type="default,ia,mms,supl,hipri,fota,cbs" />
```

Values match what stock sends in `SET_INITIAL_ATTACH_APN` (profile 0, IPv6 at
home, IPv4 roaming, PAP or CHAP). With `modem_cognitive` unset the framework
sends profile id -1 and qcril creates a new modem profile for the attach APN;
with it set, qcril reuses the modem's own profile. The modem's attach profile
persists in the modem itself across flashes.

---

#### 5. Android 17 drops the CDMA bits from the modem's capabilities

**Confirmed as a difference from stock; not the cause of the outage.**
`RILUtils.convertHalNetworkTypeBitMask` no longer maps the HAL's IS95, 1xRTT,
EVDO and EHRPD bits, so the framework sends `SET_PREFERRED_NETWORK_TYPE 9`
(stock sends 10) and the modem stores mode preference 28 (GSM, UMTS, LTE). A
build that restores the mapping sends 10 and still lost registration until
item 1 was fixed.

---

#### 6. Vendor blobs present on stock, missing from the ROM

`/vendor/bin/qti`, `/vendor/bin/modem_svc`,
`vendor.google.radioext@1.0-service` with `hardware.google.bluetooth.sar@1.0.so`,
and `diag_mdlog` are on stock and absent from this ROM's vendor image. Copying
the first three in on a running ROM did not restore data. `init` logs
`Cannot find '/vendor/bin/modem_svc'` at every boot.

---

#### 🔎 Tip: how to test radio changes on this phone

- **Reset the modem with stock.** The failure leaves state in the modem that a
  ROM reinstall or modem restart does not clear; `flash-all.sh` from the stock
  factory image does. Confirm stock has data before installing the ROM.
- **Record the first boot.** Install the ROM over a freshly reset modem and dump
  `logcat -b all` as soon as adb connects; the outage happens in the first
  20 seconds.
- **Re-attach test.** Toggle airplane mode with
  `cmd connectivity airplane-mode enable|disable`. Stock re-attaches every time.
- **Verbose qcril.** `setprop persist.vendor.radio.adb_log_on 1`, then
  `stop vendor.qcrild; start vendor.qcrild`. A value baked into `vendor/build.prop`
  is overwritten with 0 during first boot.
- **Modem logs.** On this modem, diag over USB and `diag_mdlog` (with
  `/vendor/etc/mdlog/default.cfg` or a scat-generated mask) returned only
  Android-side debug messages and no LTE signalling log packets, so the
  network's reasons were never visible directly.

---

#### Scope

Pixel 3a (sargo), modem build `MPSS.AT.4.0.2.c4.1-00145`, one Google Fi
data-only SIM in one location, LineageOS 24.0 built from this tree on
2026-11-04. Calls, SMS, roaming and other carriers were not tested.
