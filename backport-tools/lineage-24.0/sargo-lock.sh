# Sourced by sargo-build, sargo-test and sargo-install: one at a time, since they share the
# build tree and the phone. The lock holds the owner's PID; a lock whose owner is gone is free.
sargo_lock() {
  SARGO_LOCK=$HOME/sargo-bpf-test/.sargo.lock
  mkdir -p "$(dirname "$SARGO_LOCK")"
  if [ -f "$SARGO_LOCK" ] && [ "$(cut -d" " -f1 "$SARGO_LOCK")" != $$ ] && kill -0 "$(cut -d' ' -f1 "$SARGO_LOCK")" 2>/dev/null; then
    echo "busy: $(cut -d' ' -f2- "$SARGO_LOCK") is running (pid $(cut -d' ' -f1 "$SARGO_LOCK"))"; exit 1
  fi
  echo "$$ $1 since $(date '+%F %T')" > "$SARGO_LOCK"
  trap 'rm -f "$SARGO_LOCK"' EXIT
}
