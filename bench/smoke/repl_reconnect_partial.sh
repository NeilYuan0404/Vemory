#!/usr/bin/env bash
# Partial resync smoke: master stays up; a Python mini-slave does FULLRESYNC,
# disconnects, master writes, reconnects with replid+offset → expect CONTINUE.
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
MASTER_PORT="${MASTER_PORT:-16379}"
SYNC_TIMEOUT_S="${SYNC_TIMEOUT_S:-30}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VEMORY_BIN="${VEMORY_BIN:-${ROOT}/bin/vemory}"
MASTER_LOG="${MASTER_LOG:-/tmp/vemory_repl_partial_master.log}"
HELPER="${ROOT}/bench/smoke/repl_partial_psync.py"

if ! command -v redis-cli >/dev/null 2>&1; then
  echo "error: redis-cli not found (install redis-tools)" >&2
  exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 not found" >&2
  exit 1
fi
if [[ ! -x "${VEMORY_BIN}" && ! -f "${VEMORY_BIN}" ]]; then
  echo "error: VEMORY_BIN not found: ${VEMORY_BIN}" >&2
  exit 1
fi
if [[ ! -f "${HELPER}" ]]; then
  echo "error: helper missing: ${HELPER}" >&2
  exit 1
fi

cli() {
  redis-cli -2 -h "${HOST}" -p "${MASTER_PORT}" "$@"
}

ping_ok() {
  cli PING 2>/dev/null | grep -q PONG
}

wait_ping() {
  local deadline=$((SECONDS + SYNC_TIMEOUT_S))
  while (( SECONDS < deadline )); do
    if ping_ok; then
      return 0
    fi
    sleep 0.05
  done
  echo "error: master not responding within ${SYNC_TIMEOUT_S}s" >&2
  return 1
}

MASTER_PID=""
cleanup() {
  if [[ -n "${MASTER_PID}" ]] && kill -0 "${MASTER_PID}" 2>/dev/null; then
    kill "${MASTER_PID}" 2>/dev/null || true
    wait "${MASTER_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

if ping_ok; then
  echo "error: ${HOST}:${MASTER_PORT} already in use" >&2
  exit 1
fi

echo "==> start master ${HOST}:${MASTER_PORT}"
"${VEMORY_BIN}" "${MASTER_PORT}" >"${MASTER_LOG}" 2>&1 &
MASTER_PID=$!
wait_ping

echo "==> python mini-slave: FULLRESYNC → gap write → CONTINUE"
python3 "${HELPER}" --host "${HOST}" --port "${MASTER_PORT}"

if ! grep -q "PSYNC CONTINUE" "${MASTER_LOG}"; then
  echo "error: master log missing CONTINUE" >&2
  echo "--- master log ---" >&2
  cat "${MASTER_LOG}" >&2 || true
  exit 1
fi

echo
echo "done (PSYNC partial CONTINUE smoke OK)"
