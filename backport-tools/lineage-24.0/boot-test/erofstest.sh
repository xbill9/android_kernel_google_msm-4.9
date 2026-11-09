#!/bin/bash
# Checks the EROFS backport on a booted phone: builds test images with the tree's mkfs.erofs
# (uncompressed, lz4, lz4hc, SELinux-labelled; big pcluster and ztailpacking must be refused),
# plus the Play Services APEX payload if GAPPS_ZIP is set, and runs erofstest-device.sh.
# Usage: erofstest.sh [tree]   Needs adb root.
set -eu
T=${1:-$HOME/android/lineage-24.0}; H=$T/out/host/linux-x86/bin
B=$(dirname "$(readlink -f "$0")"); W=$(mktemp -d); trap 'rm -rf $W' EXIT
mkdir -p $W/src/d1/d2/d3 $W/src/big
( cd $W/src && python3 - <<'PY'
import os, random
random.seed(7)
for sz in (0,1,100,4095,4096,4097,8191,8192,8193,65536,65537,1000000):
    open(f'size_{sz}','wb').write(os.urandom(sz))
words=b'the quick brown fox jumps over the lazy dog erofs lz4 compression test '
open('text_3m.txt','wb').write((words*50000)[:3_000_000])
open('random_3m.bin','wb').write(os.urandom(3_000_000))
mixed=bytearray()
while len(mixed)<2_000_000:
    mixed+= os.urandom(random.randint(1,3000)) + words*random.randint(1,80)
open('mixed_2m.bin','wb').write(bytes(mixed[:2_000_000]))
for i in range(1000): open(f'big/f{i:04d}', 'w').write(f'file {i}\n'*(i%7))
open('d1/d2/d3/deep.txt','w').write('deep\n')
os.symlink('size_100','link_short'); os.symlink('d1/d2/d3/'+'x'*200+'/../deep.txt','link_long')
os.link('text_3m.txt','hardlink_text')
PY
)
( cd $W && find src -type f -o -type l | sort | while read f; do
    [ -L "$f" ] && echo "L $(readlink "$f") ${f#src/}" || echo "$(sha256sum < "$f" | cut -c1-64) ${f#src/}"; done ) > $W/expected.txt
printf '/.* u:object_r:system_file:s0\n/big(/.*)? u:object_r:apex_data_file:s0\n/d1(/.*)? u:object_r:vendor_file:s0\n' > $W/fc.txt
O="-T0"
$H/mkfs.erofs $O $W/plain.img $W/src >/dev/null
$H/mkfs.erofs $O -zlz4 $W/lz4.img $W/src >/dev/null
$H/mkfs.erofs $O -zlz4hc $W/lz4hc.img $W/src >/dev/null
$H/mkfs.erofs $O -zlz4hc --file-contexts=$W/fc.txt $W/labeled.img $W/src >/dev/null
$H/mkfs.erofs $O -zlz4hc -C65536 $W/bigpcl.img $W/src >/dev/null
$H/mkfs.erofs $O -zlz4hc -Eztailpacking $W/ztail.img $W/src >/dev/null
adb shell rm -rf /data/local/tmp/erofstest; adb shell mkdir -p /data/local/tmp/erofstest
adb push $W/*.img $W/expected.txt $B/erofstest-device.sh /data/local/tmp/erofstest/ >/dev/null
if [ -n "${GAPPS_ZIP:-}" ]; then
  unzip -q -o -d $W/g "$GAPPS_ZIP" 'system/product/apex/*.apex'
  unzip -q -o -d $W/a $W/g/system/product/apex/*.apex apex_payload.img
  $H/fsck.erofs --extract=$W/x $W/a/apex_payload.img >/dev/null
  sha256sum < "$(find $W/x -name '*.apk' | head -1)" | cut -c1-64 > $W/gms.sha256
  adb push $W/a/apex_payload.img /data/local/tmp/erofstest/gms.img >/dev/null
  adb push $W/gms.sha256 /data/local/tmp/erofstest/ >/dev/null
fi
adb shell sh /data/local/tmp/erofstest/erofstest-device.sh
