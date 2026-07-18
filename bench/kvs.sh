#!/usr/bin/env bash
# Protocol / KVS smoke bench.
# Use command-form redis-benchmark (PING / ECHO …), not "-t ping", which also
# runs PING_INLINE (non-RESP) and Vemory rejects.
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6379}"
N="${N:-100000}"
C="${C:-50}"
P="${P:-16}"
R="${R:-10000}"
D="${D:-64}"
ECHO_MSG="${ECHO_MSG:-hello}"

if ! command -v redis-benchmark >/dev/null 2>&1; then
  echo "error: redis-benchmark not found (install redis-tools)" >&2
  exit 1
fi
if ! command -v redis-cli >/dev/null 2>&1; then
  echo "error: redis-cli not found (install redis-tools)" >&2
  exit 1
fi

CLI=(redis-cli -2 -h "${HOST}" -p "${PORT}")
BENCH=(redis-benchmark -h "${HOST}" -p "${PORT}")

echo "==> ping server ${HOST}:${PORT}"
if ! "${CLI[@]}" PING | grep -q PONG; then
  echo "error: server not responding (start ./bin/vemory first)" >&2
  exit 1
fi

echo
echo "==> baseline PING/ECHO: c=1 p=1 n=${N}"
"${BENCH[@]}" -n "${N}" -c 1 -P 1 -q PING
"${BENCH[@]}" -n "${N}" -c 1 -P 1 -q ECHO "${ECHO_MSG}"

echo
echo "==> baseline SET/GET: c=1 p=1 n=${N}"
"${BENCH[@]}" -t set,get -n "${N}" -c 1 -P 1 -q

echo
echo "==> load PING/ECHO: c=${C} p=${P} n=${N}"
"${BENCH[@]}" -n "${N}" -c "${C}" -P "${P}" -q PING
"${BENCH[@]}" -n "${N}" -c "${C}" -P "${P}" -q ECHO "${ECHO_MSG}"

echo
echo "==> load SET/GET: c=${C} p=${P} n=${N} r=${R} d=${D}"
"${BENCH[@]}" -t set,get \
  -n "${N}" -c "${C}" -P "${P}" -r "${R}" -d "${D}" -q

echo
echo "done (single-threaded server; KvStore is unordered_map)"
