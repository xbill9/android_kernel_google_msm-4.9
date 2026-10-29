#!/bin/bash
# LineageOS 24.0 tree beside 23.2, depth 1, sharing its git objects (alternates) so only what
# changed downloads. Installs local_manifest-sargo.xml from this folder. Stops the sync if the
# tree has under 15 GB free or / under 2 GB.
set -u
T=~/android/lineage-24.0
REF=~/android/lineage-23.2/.repo/project-objects
HERE=$(dirname "$(readlink -f "$0")")
log() { echo "$(date +%T) $*"; }
mkdir -p $T && cd $T || exit 1

if [ ! -d .repo/manifests ]; then
  log "repo init lineage-24.0"
  repo init -u https://github.com/LineageOS/android.git -b lineage-24.0 --git-lfs \
    --reference=$REF --depth=1 --no-clone-bundle || exit 1
fi

mkdir -p .repo/local_manifests
cp "$HERE/local_manifest-sargo.xml" .repo/local_manifests/sargo.xml

# Disk guard: stop repo if free space drops under 15 GB.
( while sleep 30; do
    # the tree's filesystem and / (temp files) can be different disks
    free=$(df --output=avail -BG $T/ | tail -1 | tr -dc 0-9)
    root=$(df --output=avail -BG / | tail -1 | tr -dc 0-9)
    if [ "$free" -lt 15 ] || [ "$root" -lt 2 ]; then
      echo "$(date +%T) DISK GUARD: tree ${free}G, / ${root}G free, stopping sync"
      # repo runs as python3 .../main.py; match it and its git children by PID and cwd.
      for p in $(ps -eo pid,args | awk -v t="$T/.repo/repo/main.py" 'index($0,t) && !/awk/ {print $1}'); do kill $p; done
      for p in $(pgrep -x git); do case "$(readlink /proc/$p/cwd)" in $T*) kill $p;; esac; done
      exit
    fi
  done ) &
GUARD=$!

log "repo sync (free: $(df -h --output=avail $T/ | tail -1))"
for attempt in 1 2 3 4 5; do
  repo sync -c -j8 --no-tags --no-clone-bundle --optimized-fetch 2>&1 | tail -40
  rc=${PIPESTATUS[0]}
  log "attempt $attempt exit=$rc free=$(df -h --output=avail $T/ | tail -1)"
  [ $rc -eq 0 ] && break
  sleep 60
done
kill $GUARD 2>/dev/null
du -sh .repo 2>/dev/null; du -sh --exclude=.repo . 2>/dev/null
log "DONE exit=$rc"
