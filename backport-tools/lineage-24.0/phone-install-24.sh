#!/bin/bash
# Install the newest LineageOS 24.0 build on the attached Pixel 3a, put GApps back and compile apps.
# Installed as ~/bin/sargo-install. Usage:
#   sargo-install                 the newest build from GitHub releases (downloaded once, checked)
#   sargo-install tested          the newest build marked Latest (tested on a phone)
#   sargo-install rom.zip [gapps.zip]
# It updates these scripts first, and downloads MindTheGapps if no copy is found.
# The phone must be booted into one of these 24.0 userdebug builds (adb root works). The OTA goes
# into its inactive slot with update_engine, so each run flips the slot.
# The only manual step is on the phone, in recovery: "Install anyway", then "Reboot system now".
set -u
SELF=$(readlink -f "$0")
B=$(dirname "$SELF")
REPO=xbill9/android_kernel_google_msm-4.9
CACHE=~/sargo-rom
GAPPS_URL=https://github.com/MindTheGapps/17.0.0-arm64/releases/download/MindTheGapps-17.0.0-arm64-20261022_132445/MindTheGapps-17.0.0-arm64-20261022_132445.zip
for d in ~/android/lineage-24.0/out/host/linux-x86/bin ~/platform-tools; do [ -x $d/adb ] && PATH=$d:$PATH; done
log() { echo; echo "=== $(date +%T) $*"; }
die() { echo "!!! STOPPED: $*"; exit 1; }
prop() { timeout 5 adb shell getprop "$1" 2>/dev/null | tr -d '\r'; }
wait_boot() {
  for i in $(seq 1 200); do [ "$(prop sys.boot_completed)" = 1 ] && return 0; sleep 3; done
  return 1
}

# Newest scripts first; restart with them if the pull changed anything.
if [ -z "${SARGO_PULLED:-}" ] && git -C "$B" rev-parse --git-dir >/dev/null 2>&1; then
  before=$(git -C "$B" rev-parse HEAD)
  timeout 60 git -C "$B" pull -q --ff-only 2>/dev/null || echo "(could not update the scripts; using these)"
  [ "$(git -C "$B" rev-parse HEAD)" = "$before" ] || SARGO_PULLED=1 exec "$SELF" "$@"
fi

# Prints: tag zip-name zip-url sums-url, for the newest sargo 24.0 release ("tested": newest non-prerelease).
newest_release() {
  curl -fsSL "https://api.github.com/repos/$REPO/releases?per_page=30" | python3 -c '
import json, sys
tested = sys.argv[1] == "tested"
rs = [r for r in json.load(sys.stdin) if r["tag_name"].startswith("lineage-24.0-sargo-")
      and not r["draft"] and not (tested and r["prerelease"])]
for r in sorted(rs, key=lambda r: r["created_at"], reverse=True):
    a = {x["name"]: x["browser_download_url"] for x in r["assets"]}
    z = [n for n in a if n.endswith("-sargo.zip")]
    if z:
        print(r["tag_name"], z[0], a[z[0]], a.get("SHA256SUMS", "-"), "prerelease" if r["prerelease"] else "tested")
        break' "${1:-}"
}

if [ -z "${1:-}" ] || [ "$1" = tested ]; then
  log "finding the newest build"
  read -r TAG NAME URL SUMS KIND < <(newest_release "${1:-}") || die "cannot read $REPO releases from GitHub"
  echo "build: $TAG ($KIND)"
  Z=""
  for f in ~/sargo-bpf-test/build-24.0-"${TAG#lineage-24.0-sargo-}/$NAME" "$CACHE/$NAME"; do
    [ -f "$f" ] && Z=$f && break
  done
  if [ -z "$Z" ]; then
    log "downloading $NAME (about 1.2 GB)"
    mkdir -p "$CACHE"
    curl -fL --retry 3 -C - -o "$CACHE/$NAME.part" "$URL" || die "download failed; run again to resume"
    mv "$CACHE/$NAME.part" "$CACHE/$NAME"
    find "$CACHE" -maxdepth 1 -name 'lineage-24.0-*-sargo.zip' ! -name "$NAME" -delete   # keep only this build
    Z=$CACHE/$NAME
  fi
  if [ "$SUMS" != - ]; then
    want=$(curl -fsSL "$SUMS" | awk -v n="$NAME" '$2 == n {print $1}')
    [ -n "$want" ] && [ "$(sha256sum "$Z" | cut -d' ' -f1)" = "$want" ] || { rm -f "$CACHE/$NAME"; die "$Z does not match SHA256SUMS of $TAG; run again to download it fresh"; }
    echo "checksum ok"
  fi
  set --
else
  Z=$(realpath "$1")
fi

log "checking"
command -v adb >/dev/null || die "adb not found; install Android platform-tools"
[ -f "$Z" ] && unzip -l "$Z" payload.bin >/dev/null 2>&1 || die "$Z is not an OTA zip with payload.bin"
for i in $(seq 1 60); do
  n=$(adb devices | grep -c 'device$')
  [ "$n" = 1 ] && break
  [ "$n" -gt 1 ] && die "more than one phone is connected; unplug the others"
  if [ "$i" = 1 ]; then
    if adb devices | grep -q unauthorized; then echo ">>> ON THE PHONE: tap Allow at \"Allow USB debugging?\" (tick Always allow)"
    else echo ">>> Plug in the phone, unlocked, with USB debugging on. Waiting up to 3 minutes..."; fi
  fi
  sleep 3
done
[ "$(adb devices | grep -c 'device$')" = 1 ] || die "no phone found (adb devices)"
[ "$(prop ro.product.device)" = sargo ] || die "the connected phone is not a Pixel 3a (sargo)"
echo "phone: slot $(prop ro.boot.slot_suffix), $(prop ro.lineage.version)"
timeout 20 adb root >/dev/null 2>&1; sleep 3; timeout 60 adb wait-for-device
[ "$(timeout 5 adb shell id -u | tr -d '\r')" = 0 ] || die "adb root does not work: the phone must run a userdebug build of this ROM (see INSTALL.md for a first install)"

G=${2:-}
if [ -z "$G" ]; then
  GD="$HOME/sargo-bpf-test/gapps $CACHE $(dirname "$Z")"
  G=$(for d in $GD; do ls "$d"/MindTheGapps-17*-sargo.zip; done 2>/dev/null | tail -1)
  [ -n "$G" ] || G=$(for d in $GD; do ls "$d"/MindTheGapps-17*-arm64-*.zip; done 2>/dev/null | grep -v -- -sargo | tail -1)
  if [ -z "$G" ]; then
    log "downloading MindTheGapps"
    mkdir -p "$CACHE"
    G=$CACHE/$(basename "$GAPPS_URL")
    curl -fL --retry 3 -o "$G.part" "$GAPPS_URL" && mv "$G.part" "$G" || die "MindTheGapps download failed"
  fi
fi
[ -f "$G" ] || die "$G not found"
G=$(realpath "$G")
if [[ "$G" != *-sargo.zip ]]; then
  log "repacking $G for this phone"
  bash "$B/repack-gapps.sh" "$G" || die "repack-gapps.sh failed"
  G=${G%.zip}-sargo.zip
fi
echo "rom:   $Z"
echo "gapps: $G"

log "installing the update into the other slot (about 7 minutes; do not use the phone)"
OLD=$(prop ro.boot.slot_suffix)
M=$(unzip -p "$Z" META-INF/com/android/metadata)
OFF=$(echo "$M" | sed -n 's/.*[=,]payload.bin:\([0-9]*\):\([0-9]*\).*/\1/p' | head -1)
SIZE=$(echo "$M" | sed -n 's/.*[=,]payload.bin:\([0-9]*\):\([0-9]*\).*/\2/p' | head -1)
HDR=$(unzip -p "$Z" payload_properties.txt)
adb shell 'rm -f /data/ota_package/update.zip'
adb push "$Z" /data/ota_package/update.zip | tail -1
adb shell 'chown system:cache /data/ota_package/update.zip; chmod 644 /data/ota_package/update.zip; restorecon /data/ota_package/update.zip'
R=$(adb shell "update_engine_client --update --follow --payload=file:///data/ota_package/update.zip --offset=$OFF --size=$SIZE --headers=\"$HDR\"" 2>&1 | grep -vE "UPDATE_STATUS_DOWNLOADING|progress" | tail -4)
adb shell rm -f /data/ota_package/update.zip
echo "$R"
echo "$R" | grep -q 'kSuccess' || die "the install did not report kSuccess"

log "rebooting into the new build"
timeout 20 adb reboot; sleep 15
wait_boot || die "the phone did not finish booting; it may be in the bootloader"
NEW=$(prop ro.boot.slot_suffix)
echo "phone: slot $NEW, $(prop ro.lineage.version), $(timeout 5 adb shell getenforce | tr -d '\r')"
[ "$NEW" != "$OLD" ] || die "still on slot $OLD; the new slot did not boot"

log "GApps: the phone goes to recovery now"
timeout 20 adb reboot sideload
for i in $(seq 1 90); do adb devices | grep -q 'sideload$' && break; sleep 2; done
adb devices | grep -q 'sideload$' || die "the phone did not reach sideload mode"
echo
echo ">>> ON THE PHONE: when it says 'Signature verification failed', tap YES (install anyway)."
echo ">>> When it finishes, tap 'Reboot system now'. This script waits for that."
adb sideload "$G" >/dev/null 2>&1   # ends at about 47%; that is normal
sleep 20
wait_boot || die "the phone did not boot after GApps; tap 'Reboot system now', then run: bash $B/boot-test/post-install-24.sh"

log "permissions and app compile (about 3 minutes)"
bash "$B/boot-test/post-install-24.sh" || die "post-install check failed (see above)"

log "DONE: $(prop ro.lineage.version) on slot $(prop ro.boot.slot_suffix)"
