#!/usr/bin/env bash
# Vemory-only pipeline smoke: c=1 SET/GET via redis-benchmark.
# Pipeline depths default {10,20,40,100,160}. For Vemory vs Redis compare, use
# bench/pipeline_bench.py instead.
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6379}"
R="${R:-10000}"
D="${D:-64}"
PIPELINES="${PIPELINES:-10 20 40 100 160}"

N_P1="${N_P1:-10000}"
N_MID="${N_MID:-100000}"    # P=10,20
N_HIGH="${N_HIGH:-5000000}" # P=40,100,160

if ! command -v redis-benchmark >/dev/null 2>&1; then
  echo "error: redis-benchmark not found (install redis-tools)" >&2
  exit 1
fi
if ! command -v redis-cli >/dev/null 2>&1; then
  echo "error: redis-cli not found (install redis-tools)" >&2
  exit 1
fi

if ! redis-cli -2 -h "${HOST}" -p "${PORT}" PING 2>/dev/null | grep -q PONG; then
  echo "error: Vemory not responding at ${HOST}:${PORT}" >&2
  exit 1
fi

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

# Prefer --csv: redis-benchmark 7.x -q can print bogus/negative RPS on WSL.
bench_rps() {
  redis-benchmark -h "${HOST}" -p "${PORT}" --csv "$@" 2>/dev/null \
    | awk -F',' '
        NR == 1 { next }
        {
          gsub(/"/, "", $2)
          if ($2 != "") { print $2; exit }
        }'
}

bench_set_get() {
  local n="$1" p="$2"
  local set_rps get_rps
  set_rps="$(bench_rps -t set -n "${n}" -c 1 -P "${p}" -r "${R}" -d "${D}")"
  get_rps="$(bench_rps -t get -n "${n}" -c 1 -P "${p}" -r "${R}" -d "${D}")"
  printf '%s %s' "${set_rps}" "${get_rps}"
}

echo "==> ping Vemory ${HOST}:${PORT}"
echo "==> config: c=1 pipelines=(${PIPELINES}) r=${R} d=${D}"
echo "    n: p=1→${N_P1}; p=10,20→${N_MID}; p=40,100,160→${N_HIGH}"

echo
echo "==> SET/GET baseline: c=1 p=1 n=${N_P1}"
read -r set_base get_base <<<"$(bench_set_get "${N_P1}" 1)"
printf "  SET %12s rps   GET %12s rps\n" "${set_base}" "${get_base}"

echo
echo "==> pipeline sweep (Vemory only): c=1"
printf "%-6s  %-10s  %14s  %14s\n" "P" "n" "SET" "GET"
printf "%-6s  %-10s  %14s  %14s\n" "------" "----------" "--------------" "--------------"

for P in ${PIPELINES}; do
  n="$(n_for_p "${P}")"
  read -r set_rps get_rps <<<"$(bench_set_get "${n}" "${P}")"
  printf "%-6s  %-10s  %14s  %14s\n" "${P}" "${n}" "${set_rps}" "${get_rps}"
done

echo
echo "done (Vemory smoke; for Redis compare see bench/pipeline_bench.py)"
