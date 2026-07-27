# Persist Layer — Replication (PSYNC + stream)

Master/slave replication: temporary RDB fullsync under `tmp/`, in-memory backlog for catch-up, then **main-thread direct push** of RESP write-command frames to synced replicas (Redis-style). Independent of local RDB persistence (`persistence.dir` / `dump.rdb` / `SAVE`).

Partial resync: reconnecting slaves send `PSYNC <replid> <offset>`; if the offset is still in the backlog, master replies `+CONTINUE` and sends the gap as raw frames (no RDB).

---

## Roles

| Mode | How | Behavior |
|------|-----|----------|
| Master (default) | no `--slaveof` | Accepts `PSYNC`; fullsync or partial via backlog; streams writes |
| Slave | `--slaveof <host> <port>` | `PSYNC`, load RDB or CONTINUE catch-up, then stream apply |

Slave still listens on its local port for clients; client writes are rejected (`-READONLY`).

---

## Decoupling from persistence

| Feature | Path | Requires `persistence.dir`? |
|---------|------|------------------------------|
| Local SAVE / load_on_startup | `{dir}/dump.rdb` | Yes |
| Replication fullsync | `tmp/repl-fullsync.rdb` / `tmp/repl-in.rdb` | **No** |

`SnapshotManager::BackgroundSaveToPath` / `LoadFromPath` do not check `dir_`. Empty `persistence.dir` still allows PSYNC.

---

## Protocol

Slave → master:

```text
*3\r\n$5\r\nPSYNC\r\n$1\r\n?\r\n$2\r\n-1\r\n          # first sync / force fullsync
*3\r\n$5\r\nPSYNC\r\n$40\r\n<replid>\r\n$…\r\n<offset>\r\n  # try partial
```

(`PSYNC` with no args is still accepted and means fullsync.)

Master → slave (fullsync):

```text
+FULLRESYNC <replid> <cut_offset>\r\n
$<rdb_nbytes>\r\n
<rdb bytes>              # sendfile(tmp/repl-fullsync.rdb)
$<backlog_nbytes>\r\n
<backlog bytes>           # RESP write commands [cut, tip)
# then continuous bare RESP write commands:
*3\r\n$3\r\nSET\r\n...
```

Master → slave (partial):

```text
+CONTINUE\r\n
*3\r\n$3\r\nSET\r\n...   # catch-up [offset, tip), then live stream
```

`replid` is a process-lifetime 40-hex id (new on master restart). Offset is a byte position in the encoded backlog stream.

---

## Backlog + direct push

- Ring buffer (default 16 MiB) of encoded RESP write-command frames
- On successful client mutation: **encode once** → always append backlog → `Send` to each `kSynced` slave (main thread)
- Slaves still in fullsync (`kWaitRdb` / `kSending*`) only receive via the backlog bulk, not live `Send`
- Fullsync start records `backlog_start_offset`; after RDB sendfile, master sends `[start, tip)` as the second RESP bulk
- Overflow that drops a waiter’s start offset → that slave is dropped (retry PSYNC)
- Synced replica **output buffer soft limit: 32 MiB** (`TcpConn::OutputBufferedBytes`); exceeded → kick replica (`ForceClose`)

No dedicated replication I/O thread.

---

## Components

| Type | Header | Source |
|------|--------|--------|
| ReplicationBacklog | `include/vemory/replication/ReplicationBacklog.h` | `src/replication/ReplicationBacklog.cc` |
| ReplicationMaster | `include/vemory/replication/ReplicationMaster.h` | `src/replication/ReplicationMaster.cc` |
| ReplicationSlave | `include/vemory/replication/ReplicationSlave.h` | `src/replication/ReplicationSlave.cc` |
| TcpConn::SendFile | `include/vemory/net/TcpConnection.h` | `src/net/TcpConnection.cc` |
| TcpConnector | `include/vemory/net/TcpConnector.h` | `src/net/TcpConnector.cc` |

Wire: `PSYNC` → `ReplicationDispatcher` → `ReplicationMaster::OnPsync`.

---

## Example

```bash
# master
./bin/vemory 6379

# slave
./bin/vemory --slaveof 127.0.0.1 6379 6380

# after fullsync, writes on master appear on slave
redis-cli -p 6379 SET hello world
redis-cli -p 6380 GET hello
```

Smoke: `AUTO_SLAVE=1 ./bench/smoke/repl.sh`; reconnect fullsync: `./bench/smoke/repl_reconnect.sh`; partial: `./bench/smoke/repl_reconnect_partial.sh`. Demo: `demo/04_repl.py`.

---

## Limits

| Limit | Behavior |
|-------|----------|
| Partial resync | Matching `replid` + offset still in backlog → `+CONTINUE` catch-up; else / mismatch → `+FULLRESYNC` |
| Auto-reconnect | Slave link failure → exponential backoff (1s→2s→…→60s) → re-`Connect` + `PSYNC` (with offset when known) |
| Replica-readonly | `--slaveof` rejects client writes (`SET`/`DEL`/`VSET`/`VDEL`/`SAVE`) with `-READONLY …`; replication stream apply is unchanged |
| Backlog overflow | Waiters whose start offset is dropped are kicked (retry PSYNC); reconnecting slave whose offset fell out → fullsync |
| Slow replica | Synced output buffer > 32 MiB → kick; slave retries with offset or fullsync |
| Master restart | New `replid` → slaves fullsync (offset alone is not trusted across processes) |
