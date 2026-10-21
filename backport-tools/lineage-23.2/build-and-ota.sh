#!/bin/bash
# Rebuild LineageOS 23.2, apply it to the inactive slot with update_engine
# (keeps /data), reboot into it and check the basics.
R=~/sargo-bpf-test/logs
P=~/android/lineage-23.2/out/target/product/sargo
Z=$P/lineage-23.2-20261021-UNOFFICIAL-sargo.zip
log() { echo "$(date +%T) $*"; }

log "building"
(time bash $R/build-23.2.sh 2>&1; echo "exit=$?") > $R/build-23.2-ota.log 2>&1
grep -q '^exit=0' $R/build-23.2-ota.log || { log "FAIL: build"; grep -E "FAILED|error:" $R/build-23.2-ota.log | head; exit 1; }
grep '^real' $R/build-23.2-ota.log
Z=$(ls -t $P/lineage-23.2-*-UNOFFICIAL-sargo.zip | head -1); ls -l $Z

read OFF SZ < <(python3 - "$Z" <<'EOF'
import zipfile,sys
z=zipfile.ZipFile(sys.argv[1]); i=z.getinfo('payload.bin'); assert i.compress_type==0
f=open(sys.argv[1],'rb'); f.seek(i.header_offset); h=f.read(30)
print(i.header_offset+30+int.from_bytes(h[26:28],'little')+int.from_bytes(h[28:30],'little'), i.file_size)
EOF
)
HDR=$(unzip -p $Z payload_properties.txt)
T=$(mktemp)
cat > $T <<EOF
H="$HDR"
update_engine_client --update --payload=file:///data/ota_package/update.zip --offset=$OFF --size=$SZ --headers="\$H" > /data/local/tmp/ota.log 2>&1
EOF
timeout 20 adb root >/dev/null; sleep 3; timeout 20 adb wait-for-device
OLD=$(timeout 10 adb shell getprop ro.boot.slot_suffix | tr -d '\r')
timeout 600 adb push $Z /data/ota_package/update.zip | tail -1
timeout 20 adb push $T /data/local/tmp/ota-run.sh >/dev/null; rm -f $T
timeout 30 adb shell 'chown system:cache /data/ota_package/update.zip; chmod 644 /data/ota_package/update.zip; restorecon /data/ota_package/update.zip; sh /data/local/tmp/ota-run.sh; cat /data/local/tmp/ota.log | tail -1'

log "applying"
for i in $(seq 1 120); do
  s=$(timeout 15 adb shell 'timeout 5 update_engine_client --follow 2>&1 | grep -o "UPDATE_STATUS_[A-Z_]*" | tail -1' | tr -d '\r')
  case "$s" in
    UPDATE_STATUS_UPDATED_NEED_REBOOT) log "applied"; break ;;
    UPDATE_STATUS_IDLE) log "FAIL: update_engine idle"; timeout 20 adb shell 'logcat -d -b all | grep " update_engine:" | grep -iE "error|fail" | tail -5'; exit 1 ;;
  esac
  sleep 10
done
[ "$s" = UPDATE_STATUS_UPDATED_NEED_REBOOT ] || { log "FAIL: timeout ($s)"; exit 1; }

adb reboot; sleep 25; timeout 300 adb wait-for-device
for i in $(seq 1 60); do [ "$(timeout 5 adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" = 1 ] && break; sleep 4; done
timeout 20 adb root >/dev/null; sleep 3; timeout 20 adb wait-for-device; sleep 20
timeout 60 adb shell "echo slot: \$(getprop ro.boot.slot_suffix), was $OLD; getprop ro.lineage.version; uname -r; getenforce; echo boot_completed=\$(getprop sys.boot_completed)
dumpsys media.camera | grep 'Number of camera devices'
echo crashes:; logcat -d -b crash | grep 'Cmdline:' | sed 's/.*Cmdline: //' | sort | uniq -c
cmd wifi status | head -1; ping -c 2 -W 3 8.8.8.8 | tail -1
s=\$(getprop ro.boot.slot_suffix); n=0; [ \$s = _b ] && n=1; bootctl is-slot-marked-successful \$n >/dev/null 2>&1; echo slot_marked_successful_exit=\$?"
log "DONE"
