# Persist Layer — Replication (PSYNC + stream)

Master/slave replication: temporary RDB fullsync under `tmp/`, in-memory backlog for the sync window, then **main-thread direct push** of `WalEntry` frames to synced replicas (Redis-style). Independent of local RDB persistence (`persistence.dir` / `dump.rdb` / `SAVE`).

---

## Roles

| Mode | How | Behavior |
|------|-----|----------|
| Master (default) | no `--slaveof` | Accepts `PSYNC`, fullsync via `tmp/repl-fullsync.rdb` + backlog, then streams writes |
| Slave | `--slaveof <host> <port>` | `PSYNC`, load `tmp/repl-in.rdb`, apply backlog, then stream apply |

Slave still listens on its local port for clients.

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
*1\r\n$5\r\nPSYNC\r\n
```

Master → slave:

```text
+FULLRESYNC\r\n
$<rdb_nbytes>\r\n
<rdb bytes>              # sendfile(tmp/repl-fullsync.rdb)
$<backlog_nbytes>\r\n
<backlog bytes>           # u32le + WalEntry frames (AOF-compatible)
# then continuous bare frames (no RESP wrapper):
<u32le len><WalEntry proto> ...
```

---

## Backlog + direct push

- Ring buffer (default 16 MiB) of encoded `WalEntry` frames
- On successful client mutation: **encode once** → append backlog → `Send` to each `kSynced` slave (main thread)
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

Smoke: `AUTO_SLAVE=1 ./bench/smoke/repl.sh` (master must already be running). Demo: `demo/04_repl.py`.

---

## Limits

| Limit | Behavior |
|-------|----------|
| No partial resync | `PSYNC` has no offset / replid; every reconnect is a fullsync |
| No auto-reconnect | Slave `Fail` → `kError`; restart the process with `--slaveof` |
| No replica-readonly | Slave still listens and accepts `SET` / `VSET` (can diverge) |
| Backlog overflow | Waiters whose start offset is dropped are kicked (retry PSYNC) |
| Slow replica | Synced output buffer > 32 MiB → kick; slave must fullsync again |
