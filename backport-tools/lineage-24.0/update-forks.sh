#!/bin/bash
# Report how far each forked project of the 24.0 tree is behind its upstream, and with --apply
# rebase the fork's own commits onto the upstream tip and push the fork branch.
# The forks are the <remove-project> + <project> pairs in .repo/local_manifests/sargo.xml: the
# removed name is the upstream (LineageOS on GitHub, or platform/* on AOSP at the aosp remote's
# revision), the added project is the xbill9 fork. Checkouts are shallow, so the script deepens
# its fetches until the fork and upstream meet.
# Usage: update-forks.sh [--apply] [path...]
set -u
T=${TREE:-$HOME/android/lineage-24.0}
APPLY=0; [ "${1:-}" = --apply ] && { APPLY=1; shift; }
ONLY=" $* "
cd "$T" || exit 1

forks=$(python3 - "$T" <<'EOF'
import sys, xml.etree.ElementTree as ET
t = sys.argv[1]
base = ET.parse(f"{t}/.repo/manifests/default.xml").getroot()
remotes = {r.get("name"): r for r in base.iter("remote")}
default_rev = base.find("default").get("revision").removeprefix("refs/heads/")
up_path = {p.get("name"): p.get("path", p.get("name")) for p in base.iter("project")}
local = ET.parse(f"{t}/.repo/local_manifests/sargo.xml").getroot()
by_path = {p.get("path"): p for p in local.iter("project")}
for e in local.iter("remove-project"):
    up = e.get("name")
    # A removal with no replacement at the same path (prebuilts fetched by hand) is not a fork.
    fork = by_path.get(up_path.get(up))
    if fork is None:
        continue
    if up.startswith("LineageOS/"):
        up_url, up_ref = f"https://github.com/{up}", default_rev
    else:
        aosp = remotes["aosp"]
        up_url, up_ref = f"{aosp.get('fetch')}/{up}", aosp.get("revision")
    print(fork.get("path"), f"https://github.com/{fork.get('name')}", fork.get("revision"), up_url, up_ref)
EOF
) || exit 1

rc=0
while read -r path fork_url fork_br up_url up_ref; do
  [ "$ONLY" != "  " ] && [[ "$ONLY" != *" $path "* ]] && continue
  g() { git -C "$path" "$@"; }
  F=refs/update-forks/fork U=refs/update-forks/upstream
  depth=50 base=
  while [ $depth -le 100000 ]; do
    g fetch -q --depth=$depth "$fork_url" "+refs/heads/$fork_br:$F" &&
      g fetch -q --depth=$depth "$up_url" "+$up_ref:$U" || { echo "$path: fetch failed"; rc=1; continue 2; }
    base=$(g merge-base $F $U 2>/dev/null) && break
    depth=$((depth * 4))
  done
  [ -n "$base" ] || { echo "$path: no common commit within $depth commits"; rc=1; continue; }
  ahead=$(g rev-list --count $base..$F) behind=$(g rev-list --count $base..$U)
  echo "$path: $ahead own, $behind behind ${up_url##*/} ${up_ref##*/} ($(g log -1 --format='%h %cs' $U))"
  [ $APPLY = 1 ] && [ "$behind" -gt 0 ] || continue

  if [ -n "$(g status --porcelain --untracked-files=no)" ]; then
    echo "  skipped: uncommitted changes"; rc=1; continue
  fi
  if [ "$(g rev-parse HEAD)" != "$(g rev-parse $F)" ]; then
    echo "  skipped: checkout is not at the fork tip ($(g log -1 --format=%h HEAD) vs $(g log -1 --format=%h $F))"; rc=1; continue
  fi
  old=$(g rev-parse $F)
  g checkout -q --detach $F
  if ! g rebase -q --onto $U $base; then
    g rebase --abort; g checkout -q --detach "$old"
    echo "  conflict: left at the fork tip; rebase by hand onto $(g rev-parse --short $U)"; rc=1; continue
  fi
  if g push -q --force-with-lease="refs/heads/$fork_br:$old" "$fork_url" "HEAD:refs/heads/$fork_br"; then
    echo "  rebased and pushed: $(g log -1 --format='%h %s' HEAD)"
  else
    echo "  push failed; checkout is at the rebased commit $(g rev-parse --short HEAD)"; rc=1
  fi
done <<< "$forks"
exit $rc
