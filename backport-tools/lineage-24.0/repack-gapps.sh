#!/bin/bash
# Repack a MindTheGapps 17 zip for sargo. Its Play Services comes as an EROFS APEX
# (com.google.android.gmssystem), which the 4.9 kernel cannot mount, so Play Services never
# installs. This puts the APK and its permission files under /product as a priv-app, as the
# Android 16 package did, and makes the installer remove its own files and the APEX before its
# space check so the zip can be flashed over an earlier install.
# Usage: repack-gapps.sh MindTheGapps-17.0.0-arm64-<date>.zip [out.zip]
set -eu
IN=$(realpath "$1")
OUT=$(realpath -m "${2:-${IN%.zip}-sargo.zip}")
H=${HOST_BIN:-$HOME/android/lineage-24.0/out/host/linux-x86/bin}
APEX=system/product/apex/com.google.android.gmssystem.prodvic.apex
ALLOW=system/product/etc/sysconfig/allowed_apex_com.google.android.gmssystem.xml
UB=META-INF/com/google/android/update-binary
W=$(mktemp -d); trap 'rm -rf "${W:?}"' EXIT
cd "$W"

unzip -q "$IN" "$APEX" "$UB"
unzip -q -d apex "$APEX" apex_payload.img
"$H/fsck.erofs" --extract=payload apex/apex_payload.img >/dev/null
GMS=$(cd payload && ls priv-app/*/*.apk)
mkdir -p add/system/product/priv-app/PrebuiltGmsCoreVic add/system/product/etc
cp "payload/$GMS" add/system/product/priv-app/PrebuiltGmsCoreVic/PrebuiltGmsCoreVic.apk
cp -r payload/etc/permissions payload/etc/sysconfig add/system/product/etc/

python3 - "$UB" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
mark = "# Compute storage requirements\n"
add = r'''# sargo: remove the unmountable GMS APEX and any earlier install of this package, so the
# space check below counts the space they free.
rm -f /product/apex/com.google.android.gmssystem.prodvic.apex
rm -f /product/etc/sysconfig/allowed_apex_com.google.android.gmssystem.xml
for p in product system_ext; do
  [ -d "$TMP/system/$p" ] && [ -d "/$p/etc" ] || continue
  (cd "$TMP/system/$p" && find . -type f) | while read f; do rm -f "/$p/$f"; done
done

'''
assert s.count(mark) == 1
open(p, "w").write(s.replace(mark, add + mark))
EOF

python3 - "$IN" "$OUT" "$APEX" "$ALLOW" "$UB" <<'EOF'
import os, sys, zipfile
src, dst, apex, allow, ub = sys.argv[1:]
with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zo:
    for i in zi.infolist():
        if i.filename in (apex, allow):
            continue
        data = open(ub, "rb").read() if i.filename == ub else zi.read(i)
        zo.writestr(i, data, compress_type=i.compress_type)
    for root, _, files in os.walk("add"):
        for f in files:
            path = os.path.join(root, f)
            name = os.path.relpath(path, "add")
            zo.write(path, name, zipfile.ZIP_STORED if f.endswith(".apk") else zipfile.ZIP_DEFLATED)
            print("added", name, os.path.getsize(path))
EOF
echo "wrote $OUT (unsigned: recovery asks to install anyway)"
