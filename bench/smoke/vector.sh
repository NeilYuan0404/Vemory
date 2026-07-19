#!/usr/bin/env bash
# Vector path microbench: VADD warm-up + timed VSIM (ELE / VALUES).
# Uses redis-cli (RESP2). Avoid --pipe: redis-cli pipe mode trips Vemory's parser.
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6379}"
KEY="${KEY:-bench}"
DIM="${DIM:-8}"
CARD="${CARD:-1000}"
QUERIES="${QUERIES:-500}"
COUNT="${COUNT:-10}"

if ! command -v redis-cli >/dev/null 2>&1; then
  echo "error: redis-cli not found (install redis-tools)" >&2
  exit 1
fi

CLI=(redis-cli -2 -h "${HOST}" -p "${PORT}")

echo "==> ping server ${HOST}:${PORT}"
if ! "${CLI[@]}" PING | grep -q PONG; then
  echo "error: server not responding (start ./bin/vemory first)" >&2
  exit 1
fi

float_at() {
  awk -v s="$1" -v d="$2" -v n="$3" \
    'BEGIN { printf "%.6f", (s + d + 1) / (n + s + 1) }'
}

floats_for() {
  local dim="$1" seed="$2" d
  local -a out=()
  for ((d = 0; d < dim; d++)); do
    out+=("$(float_at "${seed}" "${d}" "${dim}")")
  done
  printf '%s' "${out[*]}"
}

now_ns() { date +%s%N; }

elapsed_ms() {
  awk -v s="$1" -v e="$2" 'BEGIN { printf "%.2f", (e - s) / 1e6 }'
}

qps() {
  awk -v n="$1" -v ms="$2" 'BEGIN {
    if (ms <= 0) { print "inf"; exit }
    printf "%.1f", n * 1000.0 / ms
  }'
}

echo
echo "==> VADD load key=${KEY} dim=${DIM} card=${CARD}"
echo "    (one redis-cli per command; use smaller CARD for a quick check)"
t0="$(now_ns)"
for ((i = 1; i <= CARD; i++)); do
  # shellcheck disable=SC2046
  "${CLI[@]}" VADD "${KEY}" VALUES "${DIM}" $(floats_for "${DIM}" "${i}") "e${i}" \
    >/dev/null
done
t1="$(now_ns)"
load_ms="$(elapsed_ms "${t0}" "${t1}")"
echo "    VADD ${CARD} in ${load_ms} ms ($(qps "${CARD}" "${load_ms}") ops/s)"
echo "    VCARD=$("${CLI[@]}" VCARD "${KEY}") VDIM=$("${CLI[@]}" VDIM "${KEY}")"

echo
echo "==> VSIM ELE COUNT=${COUNT} queries=${QUERIES}"
t0="$(now_ns)"
for ((i = 1; i <= QUERIES; i++)); do
  ele="e$(( (i % CARD) + 1 ))"
  "${CLI[@]}" VSIM "${KEY}" ELE "${ele}" COUNT "${COUNT}" >/dev/null
done
t1="$(now_ns)"
ele_ms="$(elapsed_ms "${t0}" "${t1}")"
echo "    VSIM ELE ${QUERIES} in ${ele_ms} ms ($(qps "${QUERIES}" "${ele_ms}") qps)"

echo
echo "==> VSIM VALUES COUNT=${COUNT} queries=${QUERIES}"
t0="$(now_ns)"
for ((i = 1; i <= QUERIES; i++)); do
  # shellcheck disable=SC2046
  "${CLI[@]}" VSIM "${KEY}" VALUES "${DIM}" $(floats_for "${DIM}" "${i}") \
    COUNT "${COUNT}" >/dev/null
done
t1="$(now_ns)"
val_ms="$(elapsed_ms "${t0}" "${t1}")"
echo "    VSIM VALUES ${QUERIES} in ${val_ms} ms ($(qps "${QUERIES}" "${val_ms}") qps)"

echo
echo "done"
