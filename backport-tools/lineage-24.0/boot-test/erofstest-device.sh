#!/system/bin/sh
# Runs on the phone as root (pushed by erofstest.sh). Mounts each EROFS image read-only
# on a loop device and compares file contents, symlinks and labels with expected.txt.
D=/data/local/tmp/erofstest; M=$D/mnt; fails=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; fails=$((fails+1)); }
mkdir -p $M
grep -q erofs /proc/filesystems && pass "erofs in /proc/filesystems" || fail "erofs not in /proc/filesystems"
check_tree() { # img
  bad=0; n=0
  while read sum path; do
    n=$((n+1))
    if [ "$sum" = L ]; then
      tgt=${path% *}; p=${path##* }
      [ "$(readlink $M/$p)" = "$tgt" ] || { bad=$((bad+1)); echo "  symlink $p"; }
    else
      [ "$(sha256sum < $M/$path | cut -c1-64)" = "$sum" ] || { bad=$((bad+1)); echo "  content $path"; }
    fi
  done < $D/expected.txt
  [ $bad = 0 ] && pass "$1: $n entries match" || fail "$1: $bad of $n entries differ"
}
mnt() { # img -> loop dev in $LOOP, mount status in $?
  LOOP=$(losetup -f --show -r $D/$1) || return 2
  mount -t erofs -o ro $LOOP $M 2>$D/mount.err
}
umnt() { umount $M 2>/dev/null; losetup -d $LOOP 2>/dev/null; }
for img in plain lz4 lz4hc labeled; do
  if mnt $img.img; then
    check_tree $img
    [ "$(stat -c %h $M/text_3m.txt)" = 2 ] && pass "$img: hard link count 2" || fail "$img: hard link count $(stat -c %h $M/text_3m.txt)"
    [ "$(ls $M/big | wc -l)" = 1000 ] && pass "$img: 1000-entry directory" || fail "$img: directory has $(ls $M/big | wc -l)"
    if [ $img = labeled ]; then
      l1=$(ls -Zd $M/big/f0007 | cut -d' ' -f1); l2=$(ls -Zd $M/d1/d2/d3/deep.txt | cut -d' ' -f1); l3=$(ls -Zd $M/size_100 | cut -d' ' -f1)
      [ "$l1 $l2 $l3" = "u:object_r:apex_data_file:s0 u:object_r:vendor_file:s0 u:object_r:system_file:s0" ] \
        && pass "labeled: SELinux labels from xattrs ($l1 $l2 $l3)" || fail "labeled: labels $l1 $l2 $l3"
    fi
    umnt
  else
    fail "$img: mount failed: $(cat $D/mount.err)"; umnt
  fi
done
for img in bigpcl ztail; do
  if mnt $img.img; then fail "$img: mounted, should be refused"; umnt
  else pass "$img: refused ($(cat $D/mount.err | head -1))"; umnt; fi
done
if [ -f $D/gms.img ]; then
  if mnt gms.img; then
    a=$(find $M -name '*.apk' | head -1)
    s=$(sha256sum < $a | cut -c1-64)
    [ "$s" = "$(cat $D/gms.sha256)" ] && pass "Play Services APEX payload: $(basename $a) matches ($(stat -c %s $a) bytes)" \
      || fail "Play Services APEX payload: apk sha256 $s"
    umnt
  else fail "gms: mount failed: $(cat $D/mount.err)"; umnt; fi
fi
dmesg | grep -i 'erofs' | grep -i -E 'err|corrupt|bug|warn' | tail -5
echo "$fails failed"
[ $fails = 0 ]
