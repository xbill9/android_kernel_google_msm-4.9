#!/bin/bash
# Fetch mainline, list the upstream fixes this tree lacks, and compare with
# the committed missing-fixes.tsv.
# Usage: check-upstream.sh [--write]   (--write replaces missing-fixes.tsv)
# UPSTREAM_GIT (default ~/sargo-bpf-test/linux-upstream.git) is the mainline
# clone from the README; KOUT is passed on to missing_fixes.py.
set -e
UP=${UPSTREAM_GIT:-$HOME/sargo-bpf-test/linux-upstream.git}
BT=$(cd "$(dirname "$0")" && pwd)
TREE=$(dirname "$BT")
NEW=$(mktemp); trap 'rm -f $NEW' EXIT

git -C "$UP" fetch -q origin '+refs/heads/master:refs/heads/master'
echo "mainline $(git -C "$UP" log -1 --format='%h %cd' --date=short master), tree $(git -C "$TREE" rev-parse --short=12 HEAD)"
APPLY=backport python3 "$BT/missing_fixes.py" "$UP" "$TREE" ${KOUT:+"$KOUT"} > "$NEW"

python3 - "$BT/missing-fixes.tsv" "$NEW" "$BT/verifier-fixes.tsv" <<'EOF'
import csv, sys
def rows(p):
    with open(p) as f:
        return {r["fix"]: r for r in csv.DictReader(f, delimiter="\t")}
old, new = rows(sys.argv[1]), rows(sys.argv[2])
# Fixes already decided on (ported, n/a, skipped, ...) stay quiet.
done = {}
with open(sys.argv[3]) as f:
    for r in csv.DictReader(f, delimiter="\t"):
        done[r["upstream"]] = r["result"]
added = [r for k, r in new.items() if k not in old]
gone = [r for k, r in old.items() if k not in new]
act = [r for r in added if r["class"] in ("built", "header")
       and r["target_origin"].startswith("backport") and r["fix"] not in done]
print(f"{len(old)} rows before, {len(new)} now: {len(added)} new, {len(gone)} gone; "
      f"{len(act)} new in built backport code")
key = lambda r: (r["class"] != "built", not r["target_origin"].startswith("backport"), r["fix"])
for title, rs in (("NEW", added), ("GONE", gone)):
    if rs:
        print(f"\n{title}:")
    for r in sorted(rs, key=key):
        rec = f"  [{done[r['fix']]}]" if r["fix"] in done else ""
        print(f"  {r['class']:10} {r['target_origin']:20} {r['apply'] or '-':8} "
              f"{r['fix']} {r['fix_subject'][:64]}{rec}")
EOF

if [ "$1" = --write ]; then
	cp "$NEW" "$BT/missing-fixes.tsv"
	echo "wrote $BT/missing-fixes.tsv"
fi
