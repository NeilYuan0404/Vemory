#!/usr/bin/env bash
# Replication smoke: master SET → slave GET after fullsync + stream.
# Requires a running master. Optionally starts the slave when AUTO_SLAVE=1.
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6379}"
SLAVE_HOST="${SLAVE_HOST:-127.0.0.1}"
SLAVE_PORT="${SLAVE_PORT:-6380}"
SYNC_TIMEOUT_S="${SYNC_TIMEOUT_S:-30}"
AUTO_SLAVE="${AUTO_SLAVE:-0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VEMORY_BIN="${VEMORY_BIN:-${ROOT}/bin/vemory}"

if ! command -v redis-cli >/dev/null 2>&1; then
  echo "error: redis-cli not found (install redis-tools)" >&2
  exit 1
fi

cli() {
  local host="$1" port="$2"
  shift 2
  redis-cli -2 -h "${host}" -p "${port}" "$@"
}

ping_ok() {
  local host="$1" port="$2"
  cli "${host}" "${port}" PING 2>/dev/null | grep -q PONG
}

echo "==> master ${HOST}:${PORT}"
if ! ping_ok "${HOST}" "${PORT}"; then
  echo "error: master not responding (start ./bin/vemory first)" >&2
  exit 1
fi

SLAVE_PID=""
cleanup() {
  if [[ -n "${SLAVE_PID}" ]] && kill -0 "${SLAVE_PID}" 2>/dev/null; then
    kill "${SLAVE_PID}" 2>/dev/null || true
    wait "${SLAVE_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

case "${AUTO_SLAVE}" in
  1|true|TRUE|yes|YES)
    if ping_ok "${SLAVE_HOST}" "${SLAVE_PORT}"; then
      echo "error: slave already on ${SLAVE_HOST}:${SLAVE_PORT}; stop it or unset AUTO_SLAVE" >&2
      exit 1
    fi
    if [[ ! -x "${VEMORY_BIN}" && ! -f "${VEMORY_BIN}" ]]; then
      echo "error: VEMORY_BIN not found: ${VEMORY_BIN}" >&2
      exit 1
    fi
    echo "==> AUTO_SLAVE start --slaveof ${HOST} ${PORT} ${SLAVE_PORT}"
    "${VEMORY_BIN}" --slaveof "${HOST}" "${PORT}" "${SLAVE_PORT}" \
      >"/tmp/vemory_repl_smoke_slave.log" 2>&1 &
    SLAVE_PID=$!
    ;;
esac

echo "==> wait slave ${SLAVE_HOST}:${SLAVE_PORT}"
deadline=$((SECONDS + ${SYNC_TIMEOUT_S%.*}))
while (( SECONDS < deadline )); do
  if ping_ok "${SLAVE_HOST}" "${SLAVE_PORT}"; then
    break
  fi
  sleep 0.05
done
if ! ping_ok "${SLAVE_HOST}" "${SLAVE_PORT}"; then
  echo "error: slave not responding within ${SYNC_TIMEOUT_S}s" >&2
  echo "  start: ${VEMORY_BIN} --slaveof ${HOST} ${PORT} ${SLAVE_PORT}" >&2
  exit 1
fi

KEY="replsmoke:$(date +%s%N)"
VAL="ok-$RANDOM"
echo "==> fullsync/stream check: SET ${KEY}=${VAL} on master, GET on slave"
cli "${HOST}" "${PORT}" SET "${KEY}" "${VAL}" >/dev/null

got=""
deadline=$((SECONDS + ${SYNC_TIMEOUT_S%.*}))
while (( SECONDS < deadline )); do
  got="$(cli "${SLAVE_HOST}" "${SLAVE_PORT}" GET "${KEY}" 2>/dev/null || true)"
  if [[ "${got}" == "${VAL}" ]]; then
    echo "  synced: GET ${KEY} → ${got}"
    break
  fi
  sleep 0.05
done
if [[ "${got}" != "${VAL}" ]]; then
  echo "error: replica not synced within ${SYNC_TIMEOUT_S}s (last GET=${got})" >&2
  exit 1
fi

KEY2="replsmoke:stream:$(date +%s%N)"
VAL2="stream-$RANDOM"
echo "==> stream check: SET ${KEY2}=${VAL2}"
cli "${HOST}" "${PORT}" SET "${KEY2}" "${VAL2}" >/dev/null
got=""
deadline=$((SECONDS + ${SYNC_TIMEOUT_S%.*}))
while (( SECONDS < deadline )); do
  got="$(cli "${SLAVE_HOST}" "${SLAVE_PORT}" GET "${KEY2}" 2>/dev/null || true)"
  if [[ "${got}" == "${VAL2}" ]]; then
    echo "  streamed: GET ${KEY2} → ${got}"
    break
  fi
  sleep 0.05
done
if [[ "${got}" != "${VAL2}" ]]; then
  echo "error: stream apply failed within ${SYNC_TIMEOUT_S}s (last GET=${got})" >&2
  exit 1
fi

echo
echo "done (PSYNC fullsync + stream smoke OK)"
