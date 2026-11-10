#!/bin/bash
# Make a recovery zip that removes a repack-gapps.sh install (its files and addon.d/30-gapps.sh)
# from the current slot, so the unmodified MindTheGapps 17 zip can be sideloaded after it: the
# unmodified installer leaves PrebuiltGmsCoreVic in place and its space check fails.
# Usage: remove-repacked-gapps.sh MindTheGapps-17.0.0-arm64-<date>-sargo.zip [out.zip]
#   adb reboot sideload && adb sideload remove-repacked-gapps.zip
#   then Apply update > Apply from ADB again, and adb sideload the unmodified zip.
set -eu
IN=$(realpath "$1")
OUT=$(realpath -m "${2:-$(dirname "$IN")/remove-repacked-gapps.zip}")
W=$(mktemp -d); trap 'rm -rf "${W:?}"' EXIT
mkdir -p "$W/META-INF/com/google/android"
unzip -Z1 "$IN" | grep -E '^system/(product|system_ext)/.+[^/]$' | sed 's|^system/||' > "$W/files.txt"
grep -q PrebuiltGmsCoreVic "$W/files.txt" || { echo "$IN is not a repack-gapps.sh zip"; exit 1; }
cat > "$W/META-INF/com/google/android/update-binary" <<'EOF'
#!/sbin/sh
OUTFD="/proc/self/fd/$2"; ZIP=$3
ui_print() { echo "ui_print $1" > "$OUTFD"; echo "ui_print" > "$OUTFD"; }
S=$(getprop ro.boot.slot_suffix)
umount /product 2>/dev/null; umount /system_ext 2>/dev/null; umount /mnt/system 2>/dev/null
n=0
for p in product system_ext; do
  blk=/dev/block/mapper/$p$S
  blockdev --setrw $blk
  mkdir -p /$p
  mount -o rw $blk /$p || { ui_print "cannot mount /$p"; exit 1; }
  for f in $(unzip -p "$ZIP" files.txt | grep "^$p/"); do
    [ -e "/$f" ] && rm -f "/$f" && n=$((n+1))
  done
  find /$p -depth -type d -empty -path "*/priv-app/*" -delete 2>/dev/null
  find /$p -depth -type d -empty -path "*/app/*" -delete 2>/dev/null
done
mkdir -p /mnt/system; blockdev --setrw /dev/block/mapper/system$S
mount -o rw /dev/block/mapper/system$S /mnt/system && rm -f /mnt/system/system/addon.d/30-gapps.sh
ui_print "removed $n repacked GApps files from slot $S"
umount /product /system_ext /mnt/system
exit 0
EOF
echo '# dummy' > "$W/META-INF/com/google/android/updater-script"
rm -f "$OUT"; (cd "$W" && zip -q -r "$OUT" META-INF files.txt)
echo "wrote $OUT ($(wc -l < "$W/files.txt") files to remove; unsigned: recovery asks to install anyway)"
