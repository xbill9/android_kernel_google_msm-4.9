#!/bin/bash
# Unofficial LineageOS 23.2 for sargo. lunch directly: breakfast's roomservice would look for
# lineage-23.2 device branches that do not exist (the trees are lineage-23.0, from the local manifest).
cd ~/android/lineage-23.2 || exit 1
source build/envsetup.sh
lunch lineage_sargo-bp4a-userdebug || exit 1
m bacon -j8
