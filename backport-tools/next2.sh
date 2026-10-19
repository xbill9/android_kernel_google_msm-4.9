#!/bin/bash
# Continue replaying series.txt after the last applied index (state in pos.txt) until a conflict.
export SERIES=series2.txt POS=pos2.txt PULLED=pulled2.txt
cd ~/sargo-bpf-test/ksargo
pos=$(cat ../${POS:-pos.txt})
total=$(wc -l < ../${SERIES:-series.txt})
while [ $pos -lt $total ]; do
  pos=$((pos+1)); c=$(sed -n ${pos}p ../${SERIES:-series.txt})
  if [ -f ../skiplist2.txt ] && grep -qx $pos ../skiplist2.txt; then echo -e "$pos\t$(git rev-parse --short $c)\tskiplist (non-BPF: IPv6 routing, bpfilter, etc)" >> ../skipped2.tsv; echo $pos > ../${POS:-pos.txt}; continue; fi
  if grep -qx $pos ../${PULLED:-pulled_forward.txt}; then echo $pos > ../${POS:-pos.txt}; continue; fi
  if git cherry-pick --allow-empty-message $c >/dev/null 2>../cp.err; then echo $pos > ../${POS:-pos.txt}; continue; fi
  if grep -qE 'now empty|is empty|nothing to commit' ../cp.err; then git cherry-pick --skip >/dev/null 2>&1; echo $pos > ../${POS:-pos.txt}; echo "$pos empty $(git log -1 --format=%s $c)"; continue; fi
  # Auto-drop files absent from our tree whose whole directory is absent too (subsystem not in 4.9 sargo)
  auto=1
  for u in $(git diff --name-only --diff-filter=U); do
    st=$(git status --porcelain -- "$u" | cut -c1-2)
    if [ "$st" = "DU" ] && ! git cat-file -e HEAD:"$(dirname "$u")" 2>/dev/null; then continue; fi
    case "$u" in
      tools/*|samples/*|Documentation/*|MAINTAINERS|CREDITS) continue;;
      net/ipv6/seg6*|include/net/seg6*|include/uapi/linux/seg6*) [ "$st" = "DU" ] && continue;;
      net/tls/*|include/net/tls.h|include/uapi/linux/tls.h) [ "$st" = "DU" ] && continue;;
      arch/arm64/*) ;;
      arch/*) continue;;
      drivers/*.c) [ -f ../kout/"${u%.c}.o" ] || continue;;
      drivers/*) ls ../kout/"$(dirname "$u")"/*.o >/dev/null 2>&1 || continue;;
      security/*|fs/*|sound/*|crypto/*|block/*) ls ../kout/"$(dirname "$u")"/*.o >/dev/null 2>&1 || continue;;
      net/*.c) [ "$st" = "UU" ] && [ ! -f ../kout/"${u%.c}.o" ] && continue;;
    esac
    auto=0
  done
  if [ $auto = 1 ]; then
    for u in $(git diff --name-only --diff-filter=U); do
      st=$(git status --porcelain -- "$u" | cut -c1-2)
      case "$st" in DU|DD) git rm -q -- "$u";; UD) git rm -q -- "$u";; AU|UA) git rm -q -- "$u" 2>/dev/null || true;; *) git checkout -q --ours -- "$u" && git add -- "$u";; esac
      echo -e "$pos\t$(git rev-parse --short $c)\t$st\t$u" >> ../autodropped2.tsv
    done
    if git diff --cached --quiet; then git cherry-pick --skip >/dev/null 2>&1; echo "$pos auto-empty $(git log -1 --format=%s $c)";
    else git -c core.editor=true cherry-pick --continue >/dev/null; fi
    echo $pos > ../${POS:-pos.txt}; continue
  fi
  echo $pos > ../${POS:-pos.txt}
  echo "CONFLICT at $pos: $(git log -1 --format='%h %s' $c)"
  git diff --name-only --diff-filter=U | sed 's/^/  /'
  for f in $(git diff --name-only --diff-filter=U); do echo "  $f: $(grep -c '^<<<<<<<' $f) hunks"; done
  exit 1
done
echo "SERIES DONE at $pos"
