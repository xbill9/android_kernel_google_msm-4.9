# Sourced by the sargo commands: one at a time, since they share the build tree and the phone.
# The lock holds the owner's PID; a lock whose owner is gone is free. A command started by the
# owner (sargo update runs sargo-test) sees SARGO_LOCK_OWNER and runs under the owner's lock.
sargo_lock() {
  SARGO_LOCK=$HOME/sargo-bpf-test/.sargo.lock
  mkdir -p "$(dirname "$SARGO_LOCK")"
  local owner; owner=$(cut -d' ' -f1 "$SARGO_LOCK" 2>/dev/null || true)
  [ -n "$owner" ] && [ "$owner" = "${SARGO_LOCK_OWNER:-}" ] && return 0
  if [ -n "$owner" ] && [ "$owner" != $$ ] && kill -0 "$owner" 2>/dev/null; then
    echo "busy: $(cut -d' ' -f2- "$SARGO_LOCK") is running (pid $owner)"; exit 1
  fi
  echo "$$ $1 since $(date '+%F %T')" > "$SARGO_LOCK"
  trap 'rm -f "$SARGO_LOCK"' EXIT
}
