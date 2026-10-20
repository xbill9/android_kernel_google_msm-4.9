#!/usr/bin/env python3
"""List upstream fixes for backported commits that this tree lacks.

Usage: [APPLY=backport] missing_fixes.py UPSTREAM_GIT [TREE] [KOUT] > missing-fixes.tsv

UPSTREAM_GIT is a mainline clone (--filter=blob:none is enough until the
apply check, which fetches the blobs of candidate fixes). An upstream
commit after v4.9 counts as present in TREE when a TREE commit has the
same subject (ignoring UPSTREAM:/BACKPORT:/... prefixes) or cites its
hash. A fix is reported when its Fixes: tag names a present commit, or a
reported fix, and the fix itself is not present.
"""
import bisect
import collections
import os
import re
import subprocess
import sys

UP = sys.argv[1]
TREE = sys.argv[2] if len(sys.argv) > 2 else "."
KOUT = sys.argv[3] if len(sys.argv) > 3 else os.path.expanduser("~/sargo-bpf-test/kout")
BASE = "69973b830859bc6529a7a0468ba0d80ee5117826"  # v4.9
LINEAGE = "df5cccf77f2a78959014d9b9206241a128691764"  # lineage-22.2 the backport sits on

PREFIX = re.compile(r"^((UPSTREAM|BACKPORT|FROMLIST|FROMGIT|ANDROID|CHROMIUM|"
                    r"Revert \"UPSTREAM)[^:]*:\s*)+", re.I)
CITE = re.compile(r"\b(?:commit|cherry picked from commit|upstream commit)\s+"
                  r"([0-9a-f]{12,40})\b", re.I)
FIXES = re.compile(r"^\s*Fixes:\s*([0-9a-f]{8,40})\b", re.I | re.M)
REVERTS = re.compile(r"This reverts commit ([0-9a-f]{12,40})")


def git(repo, *args, inp=None):
    return subprocess.run(["git", "-C", repo, *args], input=inp, capture_output=True,
                          check=True).stdout.decode("utf-8", "replace")


def norm(s):
    return PREFIX.sub("", s).strip().lower()


def log(repo, rng, fmt, *extra):
    out = git(repo, "log", "--no-merges", f"--format={fmt}%x1e", *extra, rng)
    return [r.strip("\n") for r in out.split("\x1e") if r.strip("\n")]


# Upstream commits after the base: hash -> subject, and a prefix index.
up_subj = {}
for rec in log(UP, f"{BASE}..HEAD", "%H %s"):
    h, _, s = rec.partition(" ")
    up_subj[h] = s
up_sorted = sorted(up_subj)


def resolve(prefix):
    i = bisect.bisect_left(up_sorted, prefix)
    hits = []
    while i < len(up_sorted) and up_sorted[i].startswith(prefix):
        hits.append(up_sorted[i])
        i += 1
    return hits[0] if len(hits) == 1 else None


# Tree commits not in upstream v4.9, split at the LineageOS tip into the
# base (LineageOS, Android common, 4.9 stable) and the backport on top.
origin = {}   # present upstream commit -> "backport" | "base"
for label, rng in (("backport", f"{LINEAGE}..HEAD"), ("base", f"{BASE}..{LINEAGE}")):
    subjects = set()
    for rec in log(TREE, rng, "%s%x00%b"):
        s, _, b = rec.partition("\x00")
        subjects.add(norm(s))
        for c in CITE.findall(b):
            r = resolve(c.lower())
            if r:
                origin.setdefault(r, label)
    for h, s in up_subj.items():
        if norm(s) in subjects:
            origin.setdefault(h, label)
present = set(origin)

# Fix commits and their targets; reverts.
fixes = collections.defaultdict(set)   # fix -> targets
for rec in log(UP, f"{BASE}..HEAD", "%H%x00%B", "-E", "--grep=^[[:space:]]*Fixes:", "-i"):
    h, _, b = rec.partition("\x00")
    for t in FIXES.findall(b):
        r = resolve(t.lower())
        if r:
            fixes[h].add(r)
reverted = {}
for rec in log(UP, f"{BASE}..HEAD", "%H%x00%B", "--grep=This reverts commit"):
    h, _, b = rec.partition("\x00")
    for t in REVERTS.findall(b):
        r = resolve(t.lower())
        if r:
            reverted[r] = h

# Missing fixes, closed over fixes-of-missing-fixes.
missing = {}   # fix -> the present or missing commit it fixes
frontier = set(present)
while frontier:
    new = set()
    for f, ts in fixes.items():
        if f in present or f in missing:
            continue
        hit = sorted(ts & frontier, key=lambda t: (origin.get(t) != "backport", t))
        if hit:
            missing[f] = hit[0]
            new.add(f)
    frontier = new


def origin_of(t):
    """backport/base for a present commit; for a missing fix, its root's, marked."""
    if t not in missing:
        return origin[t]
    while t in missing:
        t = missing[t]
    return origin[t] + "-via-missing"


def classify(path):
    if path.startswith(("tools/", "Documentation/", "samples/", "scripts/")):
        return "tools"
    if path.startswith("arch/") and not path.startswith("arch/arm64/"):
        return "other-arch"
    if not os.path.exists(os.path.join(TREE, path)):
        return "absent"
    if path.endswith(".c"):
        return "built" if os.path.exists(os.path.join(KOUT, path[:-2] + ".o")) else "unbuilt"
    if path.endswith(".h"):
        return "header"
    return "other"


RANK = ["built", "header", "other", "unbuilt", "absent", "tools", "other-arch"]

print("\t".join(["class", "apply", "fix", "fix_subject", "fixes", "fixes_subject",
                 "target_origin", "reverted_by", "files"]))
rows = []
for f, t in missing.items():
    files = [p for p in git(UP, "show", "--no-renames", "--name-only", "--format=", f).split("\n") if p]
    classes = {classify(p) for p in files} or {"other"}
    cls = min(classes, key=RANK.index)
    rows.append((RANK.index(cls), cls, f, t, files))
rows.sort(key=lambda r: (r[0], origin_of(r[3]), r[2]))
for _, cls, f, t, files in rows:
    apply = "-"
    # APPLY=backport limits the check (it fetches blobs) to backport targets.
    if cls in ("built", "header", "other") and origin_of(t).startswith(os.environ.get("APPLY", "")):
        patch = git(UP, "format-patch", "-1", "--stdout", f).encode()
        ok = lambda *a: subprocess.run(["git", "-C", TREE, "apply", "--check", *a],
                                       input=patch, capture_output=True).returncode == 0
        apply = "already" if ok("-R") else "clean" if ok() else "conflict"
    print("\t".join([cls, apply, f[:12], up_subj[f], t[:12], up_subj[t], origin_of(t),
                     reverted.get(f, "")[:12], " ".join(files)]))
