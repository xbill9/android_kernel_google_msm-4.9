#!/bin/bash
# Run build-24.sh in its own process group; stop the whole group if the tree has under 10 GB free or / under 2 GB.
L=~/sargo-bpf-test/logs/build-24.log
setsid bash -c '(time bash ~/sargo-bpf-test/logs/build-24.sh 2>&1; echo "exit=$?") > '"$L"' 2>&1' &
PG=$!
while kill -0 $PG 2>/dev/null; do
  # the tree's filesystem (out/) and / (temp files) can be different disks
  free=$(df --output=avail -BG ~/android/lineage-24.0/ | tail -1 | tr -dc 0-9)
  root=$(df --output=avail -BG / | tail -1 | tr -dc 0-9)
  if [ "$free" -lt 10 ] || [ "$root" -lt 2 ]; then echo "$(date +%T) DISK GUARD: tree ${free}G, / ${root}G free, stopping build" >> $L; kill -TERM -- -$PG; break; fi
  sleep 30
done
wait $PG 2>/dev/null
