#!/usr/bin/env bash
# One-shot eBPF QPS run (agent via privileged docker when sudo unavailable).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-8989}"
SLAVE="${SLAVE_PORT:-8993}"
INGEST="${INGEST_PORT:-8994}"
N="${N:-10000}"
VEMORY_BIN="${VEMORY_BIN:-$ROOT/bin/vemory}"
SYNC_USER="${SYNC_USER:-$ROOT/ebpf_sync/sync_user}"

cleanup() {
  [[ -n "${MP:-}" ]] && kill "$MP" 2>/dev/null || true
  [[ -n "${SP:-}" ]] && kill "$SP" 2>/dev/null || true
  docker rm -f vemory_ebpf_agent >/dev/null 2>&1 || true
  wait "$MP" 2>/dev/null || true
  wait "$SP" 2>/dev/null || true
}
trap cleanup EXIT

rps() {
  redis-benchmark -h 127.0.0.1 -p "$1" --csv "${@:2}" 2>/dev/null \
    | python3 -c 'import csv,sys; r=list(csv.reader(sys.stdin));
print(next(row[1] for row in r[1:] if len(row)>=2 and row[1].strip()))'
}

ping_ok() { redis-cli -2 -p "$1" PING 2>/dev/null | grep -q PONG; }

echo "==> master --repl-ebpf :$PORT"
"$VEMORY_BIN" --repl-ebpf "$PORT" >/tmp/vemory_ebpf_bench_master.log 2>&1 &
MP=$!
for _ in $(seq 1 80); do ping_ok "$PORT" && break; sleep 0.05; done
ping_ok "$PORT" || { echo "master failed"; cat /tmp/vemory_ebpf_bench_master.log; exit 1; }

echo "# eBPF repl QPS  c=1 P=1 N=$N"
echo "ECHO (vemory_no_repl)  $(rps "$PORT" -n "$N" -c 1 -P 1 ECHO hello) rps"
printf '%-18s %12s %12s\n' mode SET_rps GET_rps
SET1=$(rps "$PORT" -n "$N" -c 1 -P 1 -r 10000 -d 64 -t set)
GET1=$(rps "$PORT" -n "$N" -c 1 -P 1 -r 10000 -d 64 -t get)
printf '%-18s %12s %12s\n' vemory_no_repl "$SET1" "$GET1"
echo

echo "==> slave :$SLAVE ingest :$INGEST"
"$VEMORY_BIN" --slaveof 127.0.0.1 "$PORT" --ebpf-ingest-port "$INGEST" "$SLAVE" \
  >/tmp/vemory_ebpf_bench_slave.log 2>&1 &
SP=$!
for _ in $(seq 1 80); do ping_ok "$SLAVE" && break; sleep 0.05; done
ping_ok "$SLAVE" || { echo "slave failed"; cat /tmp/vemory_ebpf_bench_slave.log; exit 1; }
sleep 1

echo "==> agent (docker privileged, host net)"
docker run -d --name vemory_ebpf_agent --privileged --network host \
  -v "$SYNC_USER:/sync_user:ro" \
  -v /lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:ro \
  -v /lib64:/lib64:ro \
  -v /sys:/sys \
  -e "TARGET_PORT=$PORT" \
  ubuntu:22.04 \
  /sync_user 127.0.0.1 "$INGEST" >/tmp/docker_agent_run.log 2>&1
sleep 1.5
if ! docker ps --filter name=vemory_ebpf_agent --format '{{.Names}}' | grep -q vemory_ebpf_agent; then
  echo "agent container not running"
  docker logs vemory_ebpf_agent 2>&1 || cat /tmp/docker_agent_run.log
  exit 1
fi
docker logs vemory_ebpf_agent 2>&1 | tail -5

KEY="ebpfbench:$RANDOM$RANDOM"
VAL="ok-$RANDOM"
redis-cli -2 -p "$PORT" SET "$KEY" "$VAL" >/dev/null
synced=0
got=""
for _ in $(seq 1 150); do
  got=$(redis-cli -2 -p "$SLAVE" GET "$KEY" 2>/dev/null || true)
  if [[ "$got" == "$VAL" ]]; then synced=1; break; fi
  sleep 0.1
done
if [[ "$synced" != 1 ]]; then
  echo "FAIL: not synced (got='$got')"
  docker logs vemory_ebpf_agent 2>&1 | tail -40
  exit 1
fi
echo "# eBPF replica synced"

SET2=$(rps "$PORT" -n "$N" -c 1 -P 1 -r 10000 -d 64 -t set)
GET2=$(rps "$PORT" -n "$N" -c 1 -P 1 -r 10000 -d 64 -t get)
printf '%-18s %12s %12s\n' vemory_ebpf_repl "$SET2" "$GET2"
echo
echo "done"
