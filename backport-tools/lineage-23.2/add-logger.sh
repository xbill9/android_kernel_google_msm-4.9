#!/bin/bash
# add-logger.sh <in boot.img> <out boot.img>
# Repack a boot image with kmsglog as /init (real init moved to /init.real).
# Env: NOLOG=1 skips the logger, KERNEL=<Image.lz4> swaps the kernel,
#      EXTRA_CMDLINE=<args> appends to the kernel command line.
set -e
IN=$(realpath "$1"); OUTIMG=$(realpath -m "$2")
H=~/android/lineage-23.2/out/host/linux-x86/bin
K=~/sargo-bpf-test/kmsglog
W=$(mktemp -d); trap 'rm -rf $W' EXIT
$H/unpack_bootimg --boot_img "$IN" --out $W/u --format=mkbootimg > $W/args
mkdir $W/rd; cd $W/rd
case "$(file -b $W/u/ramdisk)" in
  gzip*) gzip -dc $W/u/ramdisk ;;
  LZ4*)  lz4 -dc $W/u/ramdisk ;;
  *) echo "unknown ramdisk format"; exit 1 ;;
esac | cpio -idm 2>/dev/null
if [ -z "$NOLOG" ]; then
  [ -e init.real ] || mv init init.real
  cp $K/kmsglog init; chmod 750 init
fi
find . | cpio -o -H newc -R root:root 2>/dev/null | gzip -9 > $W/u/ramdisk
cd $W
# KERNEL replaces the kernel (e.g. kout/arch/arm64/boot/Image.lz4).
if [ -n "$KERNEL" ]; then
  cp "$KERNEL" $W/u/kernel
fi
# EXTRA_CMDLINE is appended to the kernel command line (e.g. androidboot.selinux=permissive).
if [ -n "$EXTRA_CMDLINE" ]; then
  sed -i "s|--cmdline '\([^']*\)'|--cmdline '\1 $EXTRA_CMDLINE'|" $W/args
fi
eval $H/mkbootimg $(cat $W/args) -o "$OUTIMG"
echo "wrote $OUTIMG"
