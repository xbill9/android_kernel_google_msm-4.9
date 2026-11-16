#!/bin/bash
# Fetch mainline and list the upstream fixes this tree lacks, for code the phone builds.
# Usage: check-upstream.sh [--apply] [--write]
#   (none)    list the fixes new since missing-fixes.tsv, in built code or headers, not decided
#             in fixes-done.tsv
#   --apply   also commit each listed fix that applies as is (UPSTREAM: ..., cherry picked from);
#             the rest are listed for a hand merge (sargo update runs this, then tests).
#   --write   also replace missing-fixes.tsv with this run's full table: run it after deciding
#             on the listed fixes, so the next run lists only newer ones
# Record each decision (ported, n/a, skipped) in fixes-done.tsv, so it is not listed again.
# UPSTREAM_GIT (default ~/sargo-bpf-test/linux-upstream.git) is the mainline clone; KOUT (default
# the `sargo test` build, /android/sargo-bpf-test/kout) tells built code from unbuilt.
set -e
UP=${UPSTREAM_GIT:-$HOME/sargo-bpf-test/linux-upstream.git}
BT=$(cd "$(dirname "$0")" && pwd)
TREE=$(dirname "$BT")
KOUT=${KOUT:-/android/sargo-bpf-test/kout}
APPLY=; WRITE=
for a in "$@"; do case $a in --apply) APPLY=1 ;; --write) WRITE=1 ;; *) sed -n '2,10p' "$0"; exit 1 ;; esac; done
NEW=$(mktemp); TODO=$(mktemp); trap 'rm -f $NEW $TODO' EXIT

git -C "$UP" fetch -q origin '+refs/heads/master:refs/heads/master'
echo "mainline $(git -C "$UP" log -1 --format='%h %cd' --date=short master), tree $(git -C "$TREE" rev-parse --short=12 HEAD)"
APPLY=backport python3 "$BT/missing_fixes.py" "$UP" "$TREE" "$KOUT" > "$NEW"

python3 - "$BT/missing-fixes.tsv" "$NEW" "$BT/fixes-done.tsv" "$TODO" <<'EOF'
import csv, sys
def rows(p):
    with open(p) as f:
        return {r["fix"]: r for r in csv.DictReader(f, delimiter="\t")}
old, new = rows(sys.argv[1]), rows(sys.argv[2])
with open(sys.argv[3]) as f:
    done = {r["upstream"]: r["result"] for r in csv.DictReader(f, delimiter="\t")}
rel = lambda r: r["class"] in ("built", "header") and r["fix"] not in done
backlog = sum(1 for r in new.values() if rel(r))
todo = [r for k, r in new.items() if k not in old and rel(r)]
print(f"{len(new)} missing fixes in all, {backlog} in built code or headers and not decided "
      f"(the backlog: missing-fixes.tsv); new since the table: {len(todo)}:")
todo.sort(key=lambda r: (r["class"] != "built", r["fix"]))
for r in todo:
    print(f"  {r['class']:7} {r['target_origin']:20} {r['apply'] or '-':8} {r['fix']} {r['fix_subject'][:60]}")
with open(sys.argv[4], "w") as f:
    f.write("".join(r["fix"] + "\n" for r in todo))
EOF

if [ -n "$APPLY" ]; then
  [ -z "$(git -C "$TREE" status --porcelain --untracked-files=no)" ] || { echo "--apply needs a clean tree"; exit 1; }
  echo; echo "applying:"
  hand=""
  while read -r h; do
    full=$(git -C "$UP" rev-parse "$h")
    if git -C "$UP" format-patch -1 --stdout "$full" | git -C "$TREE" am -q 2>/dev/null; then
      { git -C "$TREE" log -1 --format=%B | sed '1s/^/UPSTREAM: /'; echo "(cherry picked from commit $full)"; } \
        | git -C "$TREE" commit -q --amend -F -
      echo "  committed $(git -C "$TREE" log -1 --format='%h %s' | cut -c1-80)"
    else
      git -C "$TREE" am --abort; hand="$hand $h"
    fi
  done < "$TODO"
  [ -z "$hand" ] || echo "  need a hand merge (BACKPORT: with a [ Context: ] line):$hand"
fi

if [ -n "$WRITE" ]; then
  cp "$NEW" "$BT/missing-fixes.tsv"
  echo "wrote $BT/missing-fixes.tsv"
fi
