Built from upstream LineageOS 24.0 on 2026-11-04 with the sargo tree in this repository.

#### Tested on a phone

Pixel 3a (sargo) with a Google Fi data-only SIM, installed as an OTA update over the previous build, Wi-Fi off:

- 🟢 Mobile data on T-Mobile LTE: in service for 18 of 18 five-second samples over 90 seconds
- 🟢 IPv4: `ping google.com` 3/3, through 464XLAT (`v4-rmnet_data2`, `192.0.0.4`)
- 🟢 IPv6: `ping6 google.com` 3/3
- ⚠️ Not tested: calls, SMS, roaming, other carriers

#### Mobile data fixes in this build

- Kernel: `close_range()` backported (syscall 436). Android 17 starts clatd with it; without it IPv4 fails on IPv6-only mobile networks.
- `fi_us.pb` (CarrierSettings): `carrier_volte_available_bool=true` for Google Fi. Without it Android switches IMS off in the modem and the phone drops off LTE seconds after attach.
- APN list: Google Fi `h2g2` for 310260 (`xbill9/android_vendor_apn`).
- Mobile data on by default (`xbill9/android_vendor_lineage`).

Details: `backport-tools/lineage-24.0/PORTING-NOTES-mobile-data.md`.
