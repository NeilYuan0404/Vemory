#!/usr/bin/env bash
# Replication reconnect smoke: sync → kill master → restart master → slave
# auto-fullsync → SET/GET again.
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
MASTER_PORT="${MASTER_PORT:-16379}"
SLAVE_PORT="${SLAVE_PORT:-16380}"
SYNC_TIMEOUT_S="${SYNC_TIMEOUT_S:-30}"
RECONNECT_TIMEOUT_S="${RECONNECT_TIMEOUT_S:-30}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VEMORY_BIN="${VEMORY_BIN:-${ROOT}/bin/vemory}"
MASTER_LOG="${MASTER_LOG:-/tmp/vemory_repl_reconnect_master.log}"
SLAVE_LOG="${SLAVE_LOG:-/tmp/vemory_repl_reconnect_slave.log}"

if ! command -v redis-cli >/dev/null 2>&1; then
  echo "error: redis-cli not found (install redis-tools)" >&2
  exit 1
fi
if [[ ! -x "${VEMORY_BIN}" && ! -f "${VEMORY_BIN}" ]]; then
  echo "error: VEMORY_BIN not found: ${VEMORY_BIN}" >&2
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

wait_ping() {
  local host="$1" port="$2" timeout_s="$3" label="$4"
  local deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    if ping_ok "${host}" "${port}"; then
      return 0
    fi
    sleep 0.05
  done
  echo "error: ${label} not responding within ${timeout_s}s" >&2
  return 1
}

wait_get() {
  local host="$1" port="$2" key="$3" want="$4" timeout_s="$5"
  local got="" deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    got="$(cli "${host}" "${port}" GET "${key}" 2>/dev/null || true)"
    if [[ "${got}" == "${want}" ]]; then
      echo "  synced: GET ${key} → ${got}"
      return 0
    fi
    sleep 0.05
  done
  echo "error: GET ${key} not ${want} within ${timeout_s}s (last=${got})" >&2
  return 1
}

MASTER_PID=""
SLAVE_PID=""
cleanup() {
  if [[ -n "${SLAVE_PID}" ]] && kill -0 "${SLAVE_PID}" 2>/dev/null; then
    kill "${SLAVE_PID}" 2>/dev/null || true
    wait "${SLAVE_PID}" 2>/dev/null || true
  fi
  if [[ -n "${MASTER_PID}" ]] && kill -0 "${MASTER_PID}" 2>/dev/null; then
    kill "${MASTER_PID}" 2>/dev/null || true
    wait "${MASTER_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

for p in "${MASTER_PORT}" "${SLAVE_PORT}"; do
  if ping_ok "${HOST}" "${p}"; then
    echo "error: ${HOST}:${p} already in use; stop it or set MASTER_PORT/SLAVE_PORT" >&2
    exit 1
  fi
done

echo "==> start master ${HOST}:${MASTER_PORT}"
"${VEMORY_BIN}" "${MASTER_PORT}" >"${MASTER_LOG}" 2>&1 &
MASTER_PID=$!
wait_ping "${HOST}" "${MASTER_PORT}" "${SYNC_TIMEOUT_S}" "master"

echo "==> start slave --slaveof ${HOST} ${MASTER_PORT} ${SLAVE_PORT}"
"${VEMORY_BIN}" --slaveof "${HOST}" "${MASTER_PORT}" "${SLAVE_PORT}" \
  >"${SLAVE_LOG}" 2>&1 &
SLAVE_PID=$!
wait_ping "${HOST}" "${SLAVE_PORT}" "${SYNC_TIMEOUT_S}" "slave"

KEY1="replreconnect:$(date +%s%N)"
VAL1="before-$RANDOM"
echo "==> initial sync: SET ${KEY1}=${VAL1}"
cli "${HOST}" "${MASTER_PORT}" SET "${KEY1}" "${VAL1}" >/dev/null
wait_get "${HOST}" "${SLAVE_PORT}" "${KEY1}" "${VAL1}" "${SYNC_TIMEOUT_S}"

echo "==> kill master (pid ${MASTER_PID})"
kill "${MASTER_PID}" 2>/dev/null || true
wait "${MASTER_PID}" 2>/dev/null || true
MASTER_PID=""
deadline=$((SECONDS + 5))
while (( SECONDS < deadline )) && ping_ok "${HOST}" "${MASTER_PORT}"; do
  sleep 0.05
done
if ping_ok "${HOST}" "${MASTER_PORT}"; then
  echo "error: master still responding after kill" >&2
  exit 1
fi

# Slave must stay up while master is down.
if ! kill -0 "${SLAVE_PID}" 2>/dev/null; then
  echo "error: slave exited after master down; see ${SLAVE_LOG}" >&2
  exit 1
fi
if ! ping_ok "${HOST}" "${SLAVE_PORT}"; then
  echo "error: slave not accepting clients while master is down" >&2
  exit 1
fi

echo "==> restart master ${HOST}:${MASTER_PORT}"
"${VEMORY_BIN}" "${MASTER_PORT}" >"${MASTER_LOG}" 2>&1 &
MASTER_PID=$!
wait_ping "${HOST}" "${MASTER_PORT}" "${SYNC_TIMEOUT_S}" "master(restart)"

KEY2="replreconnect:after:$(date +%s%N)"
VAL2="after-$RANDOM"
echo "==> post-reconnect: SET ${KEY2}=${VAL2} (wait slave auto-fullsync)"
# Retry SET briefly in case master just came up; then wait for replica.
cli "${HOST}" "${MASTER_PORT}" SET "${KEY2}" "${VAL2}" >/dev/null
wait_get "${HOST}" "${SLAVE_PORT}" "${KEY2}" "${VAL2}" "${RECONNECT_TIMEOUT_S}"

echo
echo "done (slave auto-reconnect + fullsync smoke OK)"
