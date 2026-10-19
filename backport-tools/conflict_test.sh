#!/bin/bash
# Replay the msm8953 4.9 BPF backport series onto the sargo kernel, one commit
# at a time, and classify each: clean / already-present (empty) / conflict.
# Conflicting commits are recorded and skipped so the run continues; later
# commits that depend on a skipped one will then also conflict (cascade).
set -u
REPO=$1 BASE=$2 TIP=$3 OUT=$4
cd "$REPO"
git cherry-pick --abort 2>/dev/null
git checkout -q -f -B bpf-conflict-test "${START:-lineage-22.2}"
git config user.email t@t; git config user.name t; git config rerere.enabled false
: > "$OUT/results.tsv"; : > "$OUT/conflict_files.txt"
# Subjects already in sargo history (catches equivalent backports with different SHAs)
git log --format=%s HEAD | sed -E 's/^(BACKPORT|UPSTREAM|FROMGIT|FROMLIST): //' | sort -u > "$OUT/sargo_subjects.txt"
n=0
for c in $(git rev-list --reverse --no-merges "$BASE..$TIP"); do
  n=$((n+1))
  subj=$(git log -1 --format=%s "$c")
  norm=$(sed -E 's/^(BACKPORT|UPSTREAM|FROMGIT|FROMLIST): //' <<<"$subj")
  insubj=no; grep -qxF -- "$norm" "$OUT/sargo_subjects.txt" && insubj=yes
  if git cherry-pick --allow-empty-message "$c" >/dev/null 2>"$OUT/err"; then
    st=clean
  elif grep -qE 'now empty|is empty|nothing to commit' "$OUT/err"; then
    st=empty; git cherry-pick --skip >/dev/null 2>&1 || git cherry-pick --abort
  else
    st=conflict
    files=$(git diff --name-only --diff-filter=U | tr '\n' ' ')
    hunks=$(git diff --diff-filter=U | grep -c '^[+ ]<<<<<<<' || true)
    echo "$n ${c:0:12} hunks=$hunks $files" >> "$OUT/conflict_files.txt"
    git cherry-pick --abort
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$n" "${c:0:12}" "$st" "$insubj" "$subj" >> "$OUT/results.tsv"
done
echo "total=$n"; cut -f3 "$OUT/results.tsv" | sort | uniq -c
