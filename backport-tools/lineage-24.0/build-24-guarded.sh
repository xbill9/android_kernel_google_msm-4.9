#!/bin/bash
# Run build-24.sh in its own process group; stop the whole group if free disk drops under 10 GB.
L=~/sargo-bpf-test/logs/build-24.log
setsid bash -c '(time bash ~/sargo-bpf-test/logs/build-24.sh 2>&1; echo "exit=$?") > '"$L"' 2>&1' &
PG=$!
while kill -0 $PG 2>/dev/null; do
  free=$(df --output=avail -BG ~ | tail -1 | tr -dc 0-9)
  if [ "$free" -lt 10 ]; then echo "$(date +%T) DISK GUARD: ${free}G free, stopping build" >> $L; kill -TERM -- -$PG; break; fi
  sleep 30
done
wait $PG 2>/dev/null
