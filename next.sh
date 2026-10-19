#!/bin/bash
# Continue replaying series.txt after the last applied index (state in pos.txt) until a conflict.
cd ~/sargo-bpf-test/ksargo
pos=$(cat ../pos.txt)
total=$(wc -l < ../series.txt)
while [ $pos -lt $total ]; do
  pos=$((pos+1)); c=$(sed -n ${pos}p ../series.txt)
  if grep -qx $pos ../pulled_forward.txt; then echo $pos > ../pos.txt; continue; fi
  if git cherry-pick --allow-empty-message $c >/dev/null 2>../cp.err; then echo $pos > ../pos.txt; continue; fi
  if grep -qE 'now empty|is empty|nothing to commit' ../cp.err; then git cherry-pick --skip >/dev/null 2>&1; echo $pos > ../pos.txt; echo "$pos empty $(git log -1 --format=%s $c)"; continue; fi
  echo $pos > ../pos.txt
  echo "CONFLICT at $pos: $(git log -1 --format='%h %s' $c)"
  git diff --name-only --diff-filter=U | sed 's/^/  /'
  for f in $(git diff --name-only --diff-filter=U); do echo "  $f: $(grep -c '^<<<<<<<' $f) hunks"; done
  exit 1
done
echo "SERIES DONE at $pos"
