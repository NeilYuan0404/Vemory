# Persist Layer — Replication (PSYNC fullsync)

Master/slave full resynchronization using a temporary RDB under `tmp/` plus an in-memory backlog for writes during the sync window. Independent of local RDB persistence (`persistence.dir` / `dump.rdb` / `SAVE`).

---

## Roles

| Mode | How | Behavior |
|------|-----|----------|
| Master (default) | no `--slaveof` | Accepts `PSYNC`, tracks slaves, generates `tmp/repl-fullsync.rdb`, `sendfile` + backlog |
| Slave | `--slaveof <host> <port>` | Connects to master, sends `PSYNC`, loads `tmp/repl-in.rdb`, applies backlog |

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
<backlog bytes>          # u32le + WalEntry frames (AOF-compatible)
```

---

## Backlog

- Ring buffer (default 16 MiB) of encoded `WalEntry` frames
- Fed on successful client mutations via `ReplicationMaster::FeedBacklog` (independent of disk AOF)
- Fullsync start records `backlog_start_offset`; after RDB sendfile, master sends `[start, tip)`
- Overflow that drops a slave’s start offset → slave dropped (next PSYNC retries fullsync)

MVP: after RDB + backlog, slave is `kSynced`; no continuous streaming after that.

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

# seed data (optional; no SAVE required)
redis-cli -p 6379 SET hello world

# slave
./bin/vemory --slaveof 127.0.0.1 6379 6380

# after fullsync
redis-cli -p 6380 GET hello
```
