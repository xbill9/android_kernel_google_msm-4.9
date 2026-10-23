#!/bin/bash
# LineageOS 24.0 for sargo on a fresh Ubuntu 24.04 EC2 instance: build packages, repo, a depth-1
# sync with local_manifest-sargo.xml, then `m bacon` at full parallelism. Run it in tmux; it logs
# to ~/build.log. Rerunning it skips finished steps (repo init, packages) and resumes the build.
# Unlike sync-24.sh there is no 23.2 tree to reference, and repo fetches the platform clang itself
# instead of fetch-clang-24.sh's sparse checkout, so the clang remove-project line is dropped.
set -u
# Same absolute path as the local tree, so out/ can be copied home and built on incrementally.
T=/home/xbill/android/lineage-24.0
log() { echo "$(date +%T) $*"; }

if ! command -v repo >/dev/null; then
  log "packages"
  sudo apt-get update -q
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -yq bc bison build-essential ccache curl flex \
    g++-multilib gcc-multilib git git-lfs gnupg gperf imagemagick lib32readline-dev lib32z1-dev \
    libdw-dev libelf-dev liblz4-tool libsdl1.2-dev libssl-dev libxml2 libxml2-utils lzop pngcrush \
    protobuf-compiler python3-protobuf python-is-python3 rsync schedtool squashfs-tools xsltproc \
    zip zlib1g-dev tmux
  sudo curl -s https://storage.googleapis.com/git-repo-downloads/repo -o /usr/local/bin/repo
  sudo chmod a+x /usr/local/bin/repo
  git lfs install
  git config --global user.name xbill9
  git config --global user.email xbill9@gmail.com
  git config --global color.ui false
fi

[ -d /home/xbill ] || sudo install -d -o "$USER" /home/xbill
mkdir -p $T && cd $T || exit 1
if [ ! -d .repo/manifests ]; then
  log "repo init lineage-24.0"
  repo init -u https://github.com/LineageOS/android.git -b lineage-24.0 --git-lfs --depth=1 \
    --no-clone-bundle || exit 1
fi
mkdir -p .repo/local_manifests
grep -v 'platform/prebuilts/clang/host/linux-x86' ~/local_manifest-sargo.xml > .repo/local_manifests/sargo.xml

if [ ! -f .sync-done ]; then
  log "repo sync"
  for attempt in 1 2 3 4 5; do
    repo sync -c -j32 --no-tags --no-clone-bundle --optimized-fetch --force-sync 2>&1 | tail -40
    rc=${PIPESTATUS[0]}
    log "sync attempt $attempt exit=$rc free=$(df -h --output=avail ~ | tail -1)"
    [ $rc -eq 0 ] && break
    sleep 60
  done
  [ $rc -eq 0 ] || exit 1
  touch .sync-done
  du -sh .repo; du -sh --exclude=.repo .
fi

# Soong records BUILD_USERNAME; a different value at home would rerun the analysis there.
export BUILD_USERNAME=xbill
log "build"
source build/envsetup.sh
lunch lineage_sargo cp2a userdebug || exit 1
time m bacon
rc=$?
log "build exit=$rc"
ls -la out/target/product/sargo/*.zip out/target/product/sargo/boot.img 2>/dev/null
exit $rc
