#!/bin/bash
# The nightly run, from cron: sargo update --if-changed, logged to ~/sargo-bpf-test/logs/.
# It builds, tests, publishes and installs on every phone only when LineageOS or the kernel
# changed since the last release; otherwise it stops after the pull (about 20 minutes).
# Install: crontab -e, then
#   0 3 * * * bash $HOME/android_kernel_google_msm-4.9/backport-tools/lineage-24.0/sargo-nightly.sh
export PATH=$HOME/bin:/usr/local/bin:/usr/bin:/bin
export HOME USER=${USER:-$(id -un)}
L=~/sargo-bpf-test/logs/nightly-$(date +%Y%m%d-%H%M).log
exec setsid nohup "$HOME/bin/sargo" update --if-changed > "$L" 2>&1 < /dev/null
