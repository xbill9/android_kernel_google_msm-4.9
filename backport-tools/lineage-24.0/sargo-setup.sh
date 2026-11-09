#!/bin/bash
# One script for a new laptop: installs the tools, gets the install scripts into ~/sargo,
# links ~/bin/sargo-install, then installs the newest ROM on the plugged-in Pixel 3a.
# Run again any time; it updates instead of setting up twice.
set -eu
REPO=https://github.com/xbill9/android_kernel_google_msm-4.9.git
need=""
for c in adb git unzip curl python3 fsck.erofs; do command -v $c >/dev/null || need=1; done
[ -z "$need" ] || sudo apt-get install -y adb git unzip curl python3 erofs-utils
if [ ! -d ~/sargo/.git ]; then
  git clone --depth 1 --filter=blob:none --sparse "$REPO" ~/sargo
  git -C ~/sargo sparse-checkout set backport-tools/lineage-24.0
else
  git -C ~/sargo pull -q --ff-only || echo "(could not update ~/sargo; using it as it is)"
fi
mkdir -p ~/bin
ln -sf ~/sargo/backport-tools/lineage-24.0/phone-install-24.sh ~/bin/sargo-install
echo "set up: ~/bin/sargo-install"
[ -n "${SARGO_SETUP_ONLY:-}" ] || exec ~/bin/sargo-install "$@"
