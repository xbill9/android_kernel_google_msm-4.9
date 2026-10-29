#!/bin/bash
# Unofficial LineageOS 24.0 for sargo: lunch directly (no lineage-24.0 device branches).
cd ~/android/lineage-24.0 || exit 1
source build/envsetup.sh
lunch lineage_sargo cp2a userdebug || exit 1
m bacon -j${JOBS:-$(nproc)}
