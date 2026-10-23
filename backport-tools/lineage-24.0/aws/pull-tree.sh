#!/bin/bash
# Run on the local build machine once the AWS build has finished: copy the whole tree (.repo,
# sources, out) to the same absolute path, keeping file times and hard links so the next local
# `m bacon` sees everything up to date. Moves an existing local tree aside rather than merging.
# rsync resumes where it stopped; rerun it after a dropped connection.
# Usage: pull-tree.sh <instance public ip>
set -eu
H=${1:?usage: pull-tree.sh <instance public ip>}
T=/home/xbill/android/lineage-24.0

if [ -d $T ] && [ ! -e $T/.aws-copy ]; then
  mv $T $T.local-$(date +%Y%m%d)
  echo "moved the local tree to $T.local-$(date +%Y%m%d)"
fi
mkdir -p $T
touch $T/.aws-copy
df -h --output=avail $T | tail -1

rsync -aHz --partial --info=progress2 --compress-choice=zstd --exclude=/.aws-copy \
  ubuntu@$H:$T/ $T/
echo "done: $(du -sh $T | cut -f1)"
