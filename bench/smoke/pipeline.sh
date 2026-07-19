#!/usr/bin/env bash
# Pipeline-depth sweep: Vemory vs Redis, c=1, P in {10,20,40,100,160}.
# SET/GET via redis-benchmark (-t set / -t get).
# Request counts by pipeline depth:
#   P=1          → 10_000
#   P=10,20      → 100_000
#   P=40,100,160 → 1_000_000
set -euo pipefail

VEMORY_HOST="${VEMORY_HOST:-127.0.0.1}"
VEMORY_PORT="${VEMORY_PORT:-8989}"
REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
REDIS_PORT="${REDIS_PORT:-6379}"
R="${R:-10000}"
D="${D:-64}"
# Space-separated pipeline depths (override with PIPELINES="10 20").
PIPELINES="${PIPELINES:-10 20 40 100 160}"

N_P1="${N_P1:-10000}"
N_MID="${N_MID:-100000}"    # P=10,20
N_HIGH="${N_HIGH:-1000000}" # P=40,100,160

if ! command -v redis-benchmark >/dev/null 2>&1; then
  echo "error: redis-benchmark not found (install redis-tools)" >&2
  exit 1
fi
if ! command -v redis-cli >/dev/null 2>&1; then
  echo "error: redis-cli not found (install redis-tools)" >&2
  exit 1
fi

check_ping() {
  local host="$1" port="$2" label="$3"
  if ! redis-cli -2 -h "${host}" -p "${port}" PING 2>/dev/null | grep -q PONG; then
    echo "error: ${label} not responding at ${host}:${port}" >&2
    exit 1
  fi
}

# Requests for a given pipeline depth.
n_for_p() {
  local p="$1"
  case "${p}" in
    1) echo "${N_P1}" ;;
    10|20) echo "${N_MID}" ;;
    40|100|160) echo "${N_HIGH}" ;;
    *)
      if ((p <= 1)); then
        echo "${N_P1}"
      elif ((p <= 20)); then
        echo "${N_MID}"
      else
        echo "${N_HIGH}"
      fi
      ;;
  esac
}

# Run one redis-benchmark test; print only the RPS number.
# Prefer --csv: redis-benchmark 7.x -q can print bogus/negative RPS when the
# live '\r' throughput ticker hits a stall or bad elapsed sample (seen on WSL).
bench_rps() {
  local host="$1" port="$2"
  shift 2
  redis-benchmark -h "${host}" -p "${port}" --csv "$@" 2>/dev/null \
    | awk -F',' '
        NR == 1 { next }
        {
          gsub(/"/, "", $2)
          if ($2 != "") { print $2; exit }
        }'
}

bench_set_get() {
  local host="$1" port="$2" n="$3" p="$4"
  local set_rps get_rps
  set_rps="$(bench_rps "${host}" "${port}" \
    -t set -n "${n}" -c 1 -P "${p}" -r "${R}" -d "${D}")"
  get_rps="$(bench_rps "${host}" "${port}" \
    -t get -n "${n}" -c 1 -P "${p}" -r "${R}" -d "${D}")"
  printf '%s %s' "${set_rps}" "${get_rps}"
}

echo "==> ping Vemory ${VEMORY_HOST}:${VEMORY_PORT}"
check_ping "${VEMORY_HOST}" "${VEMORY_PORT}" "Vemory"
echo "==> ping Redis  ${REDIS_HOST}:${REDIS_PORT}"
check_ping "${REDIS_HOST}" "${REDIS_PORT}" "Redis"

echo
echo "==> config: c=1 pipelines=(${PIPELINES}) r=${R} d=${D}"
echo "    n: p=1→${N_P1}; p=10,20→${N_MID}; p=40,100,160→${N_HIGH}"

echo
echo "==> SET/GET baseline (redis-benchmark): c=1 p=1 n=${N_P1}"
read -r v_set_base v_get_base <<<"$(bench_set_get "${VEMORY_HOST}" "${VEMORY_PORT}" "${N_P1}" 1)"
read -r r_set_base r_get_base <<<"$(bench_set_get "${REDIS_HOST}" "${REDIS_PORT}" "${N_P1}" 1)"
printf "  %-8s  SET %12s rps   GET %12s rps\n" "vemory" "${v_set_base}" "${v_get_base}"
printf "  %-8s  SET %12s rps   GET %12s rps\n" "redis" "${r_set_base}" "${r_get_base}"

echo
echo "==> pipeline sweep: c=1"
printf "%-6s  %-10s  %14s  %14s  %14s  %14s\n" \
  "P" "n" "vemory_SET" "redis_SET" "vemory_GET" "redis_GET"
printf "%-6s  %-10s  %14s  %14s  %14s  %14s\n" \
  "------" "----------" "--------------" "--------------" "--------------" "--------------"

for P in ${PIPELINES}; do
  n="$(n_for_p "${P}")"
  read -r v_set v_get <<<"$(bench_set_get "${VEMORY_HOST}" "${VEMORY_PORT}" "${n}" "${P}")"
  read -r r_set r_get <<<"$(bench_set_get "${REDIS_HOST}" "${REDIS_PORT}" "${n}" "${P}")"
  printf "%-6s  %-10s  %14s  %14s  %14s  %14s\n" \
    "${P}" "${n}" "${v_set}" "${r_set}" "${v_get}" "${r_get}"
done

echo
echo "done (c=1; compare SET/GET pipeline scaling of Vemory vs Redis)"
