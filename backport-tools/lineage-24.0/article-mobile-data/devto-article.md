---
title: "Mobile Data on Android 17 and a 4.9 Kernel: Google Fi on the Pixel 3a"
published: false
description: "LineageOS 24.0 on the Pixel 3a gets mobile data from a Google Fi data-only SIM after four fixes: mobile data on by default, a Fi APN, VoLTE in the carrier settings and a close_range() backport for IPv4. Each one traced from the radio log."
tags: android, linux, lineageos, networking
cover_image: https://raw.githubusercontent.com/xbill9/android_kernel_google_msm-4.9/main/backport-tools/lineage-24.0/article-mobile-data/devto-cover.eca0f5c3.jpg
---

This article provides a step by step guide to getting mobile data working on LineageOS 24.0, which is Android 17, on a Google Pixel 3a and its original 4.9 Linux kernel, with a Google Fi data-only SIM. It continues the LineageOS 24.0 port on the same phone. Every patch, script and build is published.

https://github.com/xbill9/android_kernel_google_msm-4.9/releases/tag/lineage-24.0-sargo-20261104-1751

**A Fi data-only SIM gets IPv4 and IPv6 data on the Pixel 3a's LineageOS 24.0 build from a fresh install, with nothing set by hand. Four changes do it: mobile data on by default, a Google Fi APN for 310260, the VoLTE key in Google's carrier settings for Fi, and a `close_range()` backport that lets Android 17 start clatd on a 4.9 kernel. Before them the phone registered on T-Mobile LTE at boot and lost it seconds later.**

---

#### What is this project trying to Do?

The Pixel 3a (codename sargo) runs LineageOS 24.0 on its 4.9 kernel; the previous article in this series covers the boot. That build was tested with no SIM fitted.

The goal here: mobile data from a Google Fi data-only SIM, the same SIM that gets data in an iPhone and on the phone's stock Android 12. The phone's modem build is `MPSS.AT.4.0.2.c4.1-00145`, from 2022.

---

#### At This Point You Should Have…

- A Google Pixel 3a (sargo) running the LineageOS 24.0 port, bootloader unlocked
- The stock factory image, `sargo-sp2a.220505.008`, unpacked with its `flash-all.sh`
- A Google Fi data-only SIM
- `adb` and `fastboot`, with rooted debugging on in Developer options
- A clone: `git clone https://github.com/xbill9/android_kernel_google_msm-4.9`

The port itself: https://dev.to/gde/android-16-on-a-49-kernel-lineageos-232-on-the-pixel-3a-5e79 for 23.2, and the 24.0 article in this series for the boot.

---

#### Step 1 — Measure Stock and Reset the Modem With It

Every test below is the same 90 second check: sample `mDataRegState` from `dumpsys telephony.registry` every 5 s, eighteen times, then ping `8.8.8.8`. Stock Android 12 with the Fi SIM and Wi-Fi off:

```text
stock SP2A, fresh: data in service 18/18 samples, timeline [##################], ping: 0% packet loss
stock baseline: data in service 18/18 samples, timeline [##################], ping: 0% packet loss
stock after airplane toggle 1: data in service 18/18 samples, timeline [##################], ping: 0% packet loss
stock after airplane toggle 2: data in service 18/18 samples, timeline [##################], ping: 0% packet loss
```

The airplane-mode toggles make the modem detach and attach again. Stock comes back every time.

The failure the ROM produces stays in the modem across reboots, ROM reinstalls and modem restarts (`echo restart > /sys/kernel/debug/msm_subsys/modem`). `flash-all.sh` from the stock image clears it, so each ROM test here starts from a stock flash that passed the 90 second check, followed by a fresh install of the ROM.

🔎 **Tip: record the first boot.** The failure happens within 20 seconds of the first boot after stock. Dump `logcat -b all` as soon as `adb` connects; the radio buffer still holds the attach.

---

#### Step 2 — Mobile Data Is Off by Default

The first boot of the ROM after stock registers on 310260 and loses it 11 s later:

```text
11-03 21:31:17.800  [0092]< DATA_REGISTRATION_STATE {.base = {.regState = REG_HOME
11-03 21:31:23.125  [0225]> SETUP_DATA_CALL ... dataProfile=[DataProfile=[ApnSetting] T-Mobile US IMS, 2655, 310260, ims
11-03 21:31:25.751  [0249]> DEACTIVATE_DATA_CALL cid = 0 reason = NORMAL [PHONE0]
11-03 21:31:28.430  [0256]< DATA_REGISTRATION_STATE {.base = {.regState = NOT_REG_MT_SEARCHING_OP_EM
```

LineageOS sets `ro.com.android.mobiledata=false` in `vendor/lineage/config/telephony.mk`, so the only connection after attach is IMS, requested by Qualcomm's `com.qualcomm.qti.cne`. When cne drops the request, Android tears the connection down (`onTearDown: reason=CONNECTIVITY_SERVICE_UNWANTED`), the phone has no connection left on LTE, and it detaches.

The fork `xbill9/android_vendor_lineage` `fe3f792` turns mobile data on. A device tree cannot override this property: the build refuses two different values for it, and the product partition, where LineageOS sets it, is read last.

---

#### Step 3 — A Google Fi APN for 310260

Stock Pixels take APNs from Google's CarrierSettings. The ROM reads the LineageOS APN list, `vendor/apn`. It has `h2g2` only for 310240, so the Fi SIM on 310260 attached with T-Mobile's `fast.t-mobile.com`, as in the log above.

The fork `xbill9/android_vendor_apn` `6acda79` adds the entry, with the values a stock Pixel sends in `SET_INITIAL_ATTACH_APN`:

```xml
<apn mcc="310" mnc="260" carrier="Google Fi" apn="h2g2" authtype="3"
     mmsc="http://mmsc1.g-mms.com/mms/wapenc" modem_cognitive="true"
     mvno_match_data="31026097" mvno_type="imsi" profile_id="0"
     protocol="IPV6" roaming_protocol="IP"
     type="default,ia,mms,supl,hipri,fota,cbs" />
```

`profile_id="0"` with `modem_cognitive` makes qcril reuse the modem's own attach profile, as stock does; without them the framework sends profile id -1 and qcril creates a new modem profile.

---

#### Step 4 — IMS Switched Off Drops LTE

With mobile data on and the Fi APN in place, the phone still lost registration about a second after this:

```text
11-04 12:56:53.115  qcril_qmi_imss_request_set_ims_srv_status_v02: VoLTE setting : 0
11-04 12:56:53.180  qcril_qmi_imss_request_set_ims_registration_v02: ims_service_enabled = FALSE
```

The carrier config for the SIM has `carrier_volte_available_bool=false`, so Android switches IMS off in the modem. A modem set up as voice-centric gives up LTE when it has no voice over LTE, and T-Mobile has no 3G to fall back to. The phone then searches and is denied by 311480 and 310410 (causes 15 and 13), which is the state that survives a modem restart.

Setting the key on the running phone brings service back:

```text
adb shell cmd phone cc set-value -s 0 -p carrier_volte_available_bool true
qcril_qmi_imss_request_set_ims_srv_status_v02: VoLTE setting : 1
qcril_qmi_imss_request_set_ims_registration_v02: ims_service_enabled = TRUE
VoLTE available=true + modem restart: data in service 17/18 samples, longest run 85s, timeline [.#################], ping: Network is unreachable
```

The SIM has no voice plan. IMS tries to register and fails, as it does on stock, and LTE data stays up.

---

#### Step 5 — Where the Carrier Config Comes From

The bonito tree sets `platform_carrier_config_package` to `com.google.android.carrier`, Google's CarrierSettings from the vendor blobs. The stock phone answers the same question in two passes:

```text
Fetch config for default app: com.google.android.carrier carrierid: CarrierIdentifier{mcc=310,mnc=260,...,gid1=4276,...,carrierid=1989,specificCarrierId=10014}
Fetch config for carrier app: com.google.android.apps.tycho carrierid: CarrierIdentifier{mcc=310,mnc=260,spn=Google Fi,...}
            carrier_volte_available_bool = false
            carrier_volte_available_bool = true
```

CarrierSettings maps the Fi SIM to `fi_us.pb`, which carries four keys and no VoLTE key, so its answer is `false`. The `true` comes from the Google Fi app, Tycho, as the SIM's carrier app. A ROM without Tycho keeps `false`.

The fix appends one record to `fi_us.pb` in `xbill9/proprietary_vendor_google_sargo` `84e78b4`. Protobuf merges repeated messages, so the original keys stay as they are. The tree's own `aprotoc` and the CarrierSettings schema from `lineage/scripts/carriersettings-extractor` encode it:

```bash
P=lineage/scripts/carriersettings-extractor
printf 'configs { config { key: "carrier_volte_available_bool" bool_value: true } }' \
  | out/host/linux-x86/bin/aprotoc -I$P --encode=com.google.carrier.CarrierSettings \
    $P/carrier_settings.proto >> fi_us.pb
```

Bind-mounted over `/product/etc/CarrierSettings/fi_us.pb`, with the cached answers in `/data/user_de/0/com.android.phone/files/carrierconfig-*` deleted and the phone process restarted:

```text
carrier_volte_available_bool                                           BOOLEAN         true
fi_us.pb with VoLTE (bind-mounted), modem restart: data in service 17/18 samples, longest run 85s, timeline [.#################], ping: 0% packet loss
```

🔎 **Tip: find out which app answers.** `dumpsys carrier_config | grep 'Fetch config'` names it. An overlay for the AOSP CarrierConfig app's `vendor.xml` has no effect on a device that points `platform_carrier_config_package` at Google's.

---

#### Step 6 — IPv4 Through 464XLAT Needs close_range()

Fi's data network is IPv6-only. IPv4 goes through clatd, and on the 4.9 kernel clatd never starts:

```text
D jniClatCoordinator: clatd process 10827 terminated status=32512
```

32512 is exit code 127. `ClatCoordinator` starts clatd with `posix_spawn(POSIX_SPAWN_USEVFORK | POSIX_SPAWN_CLOEXEC_DEFAULT)`, and bionic handles the second flag in the child:

```c
if ((flags & POSIX_SPAWN_CLOEXEC_DEFAULT) != 0) {
  // mark all open fds except stdin/out/err as close-on-exec
  if (close_range(3, ~0U, CLOSE_RANGE_CLOEXEC)) _exit(127);
}
```

`close_range` arrived in Linux 5.9 and `CLOSE_RANGE_CLOEXEC` in 5.11. Run by hand, the same clatd binary starts and prints its usage line, so the binary, its libraries and its SELinux label are all fine.

Kernel `5b86ef09ed48` backports `close_range()` as syscall 436, wired like the tree's existing `pidfd_open` backport: `fs/file.c`, `include/uapi/linux/close_range.h`, the asm-generic and arm64 compat tables, with `__NR_syscalls` and `__NR_compat_syscalls` raised to 437. With only the new boot image flashed:

```text
v4-rmnet_data2   UNKNOWN        192.0.0.4/32
8.8.8.8 dev v4-rmnet_data2 table 1033 src 192.0.0.4 uid 2000
ROM + close_range kernel (IPv4 ping): data in service 18/18 samples, timeline [##################], ping: 0% packet loss
```

Any caller of `POSIX_SPAWN_CLOEXEC_DEFAULT` on Android 17 needs this on a kernel before 5.11.

---

#### Step 7 — Build From Upstream and Install as an Update

`sargo-build` syncs upstream LineageOS 24.0, checks every fork against upstream, builds and publishes the release. LineageOS's manifest of 2026-11-04 moved twelve projects from AOSP to LineageOS copies, among them `external/aac` and `packages/modules/Profiling`; `repo sync` refuses to replace a project's git metadata without `--force-sync`, so each one, checked clean, gets that once:

```bash
repo sync -c --force-sync external/aac external/hplip ... vendor/apn vendor/lineage
```

The build after that sync re-analyses the whole tree, and it took 03:10:10 on this machine. `update_engine` installs it into the other slot and keeps `/data`:

```text
[INFO:update_engine_client_android.cc(102)] onPayloadApplicationComplete(ErrorCode::kSuccess (0))
release 20261104-1751 after OTA: data in service 18/18 samples, timeline [##################], ping: 0% packet loss
```

And from a fresh install over a stock-reset modem, Wi-Fi off, nothing set by hand:

```text
mi_imss_request_set_ims_registration_v02: ims_service_enabled = TRUE
mi_imss_request_set_ims_srv_status_v02: VoLTE setting : 1
FINAL build 2, fresh install, no overrides: data in service 18/18 samples, timeline [##################], ping: 0% packet loss
3 packets transmitted, 3 received, 0% packet loss, time 2012ms
8.8.8.8 dev v4-rmnet_data3 table 1033 src 192.0.0.4 uid 2000
```

---

#### 🔎 Tip: What the Radio Logs Show and Hide

- **qcril's detailed log** turns on with `setprop persist.vendor.radio.adb_log_on 1`, then `stop vendor.qcrild; start vendor.qcrild`. It shows the attach profile qcril matches and whether it re-attaches.
- **The modem's own messages** stay out of reach on this phone. Diag over USB and `diag_mdlog`, with `/vendor/etc/mdlog/default.cfg`, return Android-side debug lines and no LTE signalling packets, so the network's reject causes come only from what qcril reports.
- **Dumping the carrier config** with `cmd phone cc get-value -s 0 <key>` reads the value Android applies, including any `-p` override.

---

#### Compare and Contrast

| | Stock Android 12 | LineageOS 24.0, port build | LineageOS 24.0, this build |
| :--- | :--- | :--- | :--- |
| Data with the Fi SIM | 🟢 18/18 | ❌ lost seconds after attach | 🥇 🟢 18/18 |
| IPv4 on IPv6-only data | 🟢 | ❌ clatd exits 127 | 🟢 through clatd |
| Fi APN | CarrierSettings | ❌ none for 310260 | 🟢 `h2g2` |
| VoLTE key for Fi | 🟢 from Tycho | ❌ `false` | 🟢 in `fi_us.pb` |
| Mobile data default | 🟢 on | ❌ off | 🟢 on |

---

#### So, Which One?

For a Pixel 3a with a Fi data-only SIM, this build is the one: data from a fresh install, IPv4 and IPv6, and the same update path that keeps your data. Re-extracting the vendor blobs from a stock image reverts `fi_us.pb`, so keep the fork's copy.

---

#### Summary

The goal of this project was mobile data from a Google Fi data-only SIM on the Pixel 3a's LineageOS 24.0 port. The key to the solution was a stock flash before every test, which resets the modem, and the first boot's radio log, which shows each change the ROM makes to the modem. The results were:

- 🟢 Data in service for 18 of 18 samples from a fresh install, Wi-Fi off, nothing set by hand.
- 🟢 IPv4 through clatd after a `close_range()` backport; `ping` to `google.com` answers 3 of 3.
- 🟢 Calls over LTE marked available for Fi in `fi_us.pb`, so Android leaves IMS on in the modem.
- 🟢 A Google Fi `h2g2` APN for 310260, and mobile data on by default.
- ⚠️ Re-extracting the vendor blobs from stock reverts the `fi_us.pb` change.
- ⚠️ Calls, SMS, roaming and SIMs from other carriers are untested.

Scope: one Pixel 3a (sargo) with one Google Fi data-only SIM in one location, modem build `MPSS.AT.4.0.2.c4.1-00145`, LineageOS 24.0 built 2026-11-04 from the trees in the references. Each data figure is one 90 second check of eighteen samples. Parts of the analysis and writing were done with AI assistance (Claude); every log line comes from the phone or the build machine.

The strategy for getting mobile data on Android 17 and a 4.9 kernel was validated with an incremental step by step approach.

---

#### References

- Release: https://github.com/xbill9/android_kernel_google_msm-4.9/releases/tag/lineage-24.0-sargo-20261104-1751
- Porting notes for mobile data: https://github.com/xbill9/android_kernel_google_msm-4.9/blob/main/backport-tools/lineage-24.0/PORTING-NOTES-mobile-data.md
- Kernel `close_range()` backport: https://github.com/xbill9/android_kernel_google_msm-4.9/commit/5b86ef09ed48
- Fork, vendor/apn: https://github.com/xbill9/android_vendor_apn
- Fork, vendor/lineage: https://github.com/xbill9/android_vendor_lineage
- Vendor blobs with `fi_us.pb`: https://github.com/xbill9/proprietary_vendor_google_sargo/tree/lineage-24.0
- Device tree: https://github.com/xbill9/android_device_google_bonito/tree/lineage-24.0
- How to commit to the tree and its forks: https://github.com/xbill9/android_kernel_google_msm-4.9/blob/main/GIT-WORKFLOW.md
- The 23.2 port: https://dev.to/gde/android-16-on-a-49-kernel-lineageos-232-on-the-pixel-3a-5e79
- Google Fi APN settings: https://support.google.com/fi/answer/6330195
