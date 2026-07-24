# Vemory

[English](README.md) | 中文

兼容 RESP 的语义缓存服务端（另含字符串 KVS）。可用 RESP 客户端连接（字符串可用 `redis-cli`；二进制 `VSET`/`VGET` 需客户端库）。

**v0.3.0 — 早期 MVP。** 可选多文件 RDB 快照（`SAVE` / `persistence.dir`）；无 WAL。单线程 epoll。主 API 为语义缓存（`VSET`/`VGET`/`VDEL`，二进制 float blob），另含 `SET`/`GET`/`DEL` / `PING`/`ECHO` / `SAVE`。并非 Redis / Redis Vector Set 替代品。详见 [`CHANGELOG.md`](CHANGELOG.md)。

## 依赖

- C++17 工具链（`g++`）
- [Protocol Buffers](https://protobuf.dev/)（`protoc`、`libprotobuf`）——编解码保留，供后续复制使用
- 已内置 [usearch](https://github.com/unum-cloud/usearch)，位于 `third_party/usearch`（可用 `make usearch-fetch` 刷新）
- 已内置 [spdlog](https://github.com/gabime/spdlog)，位于 `third_party/spdlog`（可用 `make spdlog-fetch` 刷新）

## 构建与运行

```bash
make              # → bin/vemory
./bin/vemory      # 监听 0.0.0.0:6379
./bin/vemory 8989 # 自定义端口
./bin/vemory -c conf/vemory.ini
./bin/vemory -c conf/vemory.ini 8989  # CLI 端口会覆盖 server.port
```

```bash
redis-cli                 # 默认端口 6379
redis-cli -p 8989
```

### 配置（INI）

通过 `-c` 指定可选配置文件（见 [`conf/vemory.ini`](conf/vemory.ini)）。未使用 `-c` 时采用内置默认值。

| 节 | 键 | 默认值 | 含义 |
|---------|-----|---------|---------|
| `server` | `port` | `6379` | 监听端口 |
| `server` | `bind` | `0.0.0.0` | IPv4 绑定地址 |
| `logging` | `level` | `info` | `trace`/`debug`/`info`/`warn`/`error`/`critical`/`off` |
| `storage` | `kv_reserve` | `100000` | `KvStore` 预留容量 |
| `index` | `default_capacity` | `1024` | 向量集合初始容量 |
| `persistence` | `dir` | `data` | RDB 快照目录；空则 `SAVE` 不可用 |
| `persistence` | `load_on_startup` | `false` | 启动时从 `dir` 加载 `dump.*` |
| `persistence` | `aof` | `false` | Protobuf AOF：`dir/appendonly.aof` |

未知节/键会被忽略（并告警）。位置参数端口仍会覆盖 `server.port`。

快照文件（多文件）默认在 `data/`：`dump.meta` / `dump.kv` / `dump.nodes` / `dump.usearch`。`SAVE` 为 fork 后台写盘（无 WAL）。

压测（服务端需已启动；依赖 `redis-benchmark` / `redis-cli`）：

```bash
./bench/smoke/kvs.sh       # PING / ECHO / SET / GET
./bench/smoke/pipeline.sh  # c=1 管道冒烟（仅 Vemory）
./bench/smoke/vector.sh    # VSET 灌库 + VGET + VDEL 抽检（redis-py）
./bench/smoke/vector_rdb.sh  # VSET → SAVE → dump.usearch → VGET（需 persistence.dir）
python3 bench/pipeline_bench.py                  # c=1 SET/GET：Vemory vs Redis
bench/.venv/bin/python bench/vector_metrics.py   # agree / p50·p99 / QPS@agree≥0.95（见 bench/README.md）
HOST=127.0.0.1 PORT=8989 python3 bench/rdb_save_bench.py  # SAVE 频率 vs SET QPS
python3 bench/aof_bench.py                               # AOF SET/GET vs Redis
```

### 最近一次 pipeline 结果

运行：`python3 bench/pipeline_bench.py`（Vemory `127.0.0.1:8989`，Redis `127.0.0.1:6379`）

基线（`c=1`，`p=1`，`n=10000`；release `bin/vemory`）：

| Server | SET (rps) | GET (rps) |
|--------|-----------|-----------|
| Vemory | 13531.80 | 13404.83 |
| Redis | 12437.81 | 13531.80 |

Pipeline 扫描（`c=1`）：

| P | n | Vemory SET | Redis SET | Vemory GET | Redis GET |
|---|---:|-----------:|----------:|-----------:|----------:|
| 10 | 100000 | 105820.11 | 87719.30 | 89445.44 | 96339.12 |
| 20 | 100000 | 165289.25 | 130208.34 | 146842.88 | 152905.20 |
| 40 | 5000000 | 225641.95 | 147999.05 | 194552.52 | 198720.25 |
| 100 | 5000000 | 206568.89 | 166284.22 | 179649.31 | 184352.19 |
| 160 | 5000000 | 219934.91 | 170160.62 | 201126.30 | 200980.78 |

详见 [`bench/README.md`](bench/README.md)。

### 最近一次 vector metrics

运行：`HOST=127.0.0.1 PORT=8989 bench/.venv/bin/python bench/vector_metrics.py`  
（debug `bin/vemory` 监听 `:8989`，单连接；`glove-25-angular` 子集）

| 指标 | 结果 |
|------|------:|
| CARD / QUERIES / THRESHOLD | 10000 / 200 / 0.2 |
| dim | 25 |
| agree | 1.0000 |
| latency p50 / p99 | 1.83 ms / 2.81 ms |
| QPS@agree≥0.95 | 536.2 |
| VSET 灌库 | 329.4 ops/s |

仅供参考——单线程事件循环，非多客户端打满压测。

### 最近一次 RDB SAVE vs SET QPS

运行：`HOST=127.0.0.1 PORT=8989 python3 bench/rdb_save_bench.py`  
（release `bin/vemory` 监听 `:8989`；`CLIENT=benchmark`，`N=1000000`，`SAVE_BUSY=skip`）

| interval | saves_ok | saves_skipped | elapsed_s | set_qps |
|----------|---------:|--------------:|----------:|--------:|
| baseline | 0 | 0 | 74.773 | 13373.8 |
| 1000000 | 1 | 0 | 74.965 | 13339.6 |
| 100000 | 10 | 0 | 75.568 | 13233.1 |
| 10000 | 100 | 0 | 79.685 | 12549.5 |
| 1000 | 984 | 16 | 111.473 | 8970.8 |

SET 走 `redis-benchmark`（`c=1 p=1`）；SAVE 在 chunk 之间用 `redis-cli` 触发。仅供参考。

### 最近一次 AOF QPS

运行：`python3 bench/aof_bench.py`  
（release `bin/vemory`；`c=1 P=1`，`N=100000`；无 AOF `:8989`，AOF `:8990` / `conf/vemory_aof_bench.ini`，Redis `appendonly yes` `:6379`）

ECHO（vemory_no_aof）：**13509.86** rps

| mode | SET (rps) | GET (rps) |
|------|----------:|----------:|
| vemory_no_aof | 13113.03 | 12573.87 |
| vemory_aof | 8722.20 | 12828.74 |
| redis_aof | 9790.48 | 12682.31 |

仅供参考——单线程事件循环；AOF 写路径与 Redis 不同。

其他目标：

| 目标 | 用途 |
|--------|---------|
| `make run` | 构建并启动 `bin/vemory` |
| `make test` | GoogleTest 单元测试（`bin/unit_tests`） |
| `make proto` | 重新生成 `generated/VNode.pb.*` |
| `make compile-commands` | 刷新 `compile_commands.json`（供 clangd） |
| `make clean` | 删除 `build/`、`bin/`、`generated/` |

入口：[`src/Vemory.cc`](src/Vemory.cc)。维度在首次成功 `VSET` 时锁定（`dim = blob字节数 / sizeof(float)`）。

## 命令

线协议为 Redis RESP（bulk 支持二进制）。语义缓存命令：

| 命令 | 参数 | 回复 |
|---------|------|-------|
| `VSET` | `<vector_blob> <user_key> <question> <answer>` | `+OK` 或 `-ERR …` |
| `VGET` | `<query_vector_blob> <threshold>` | bulk `answer`，未命中为 null bulk |
| `VDEL` | `<user_key>` | `:1` / `:0` |

向量为小端 `float32` 原始字节；`threshold` 为余弦**距离**上限。另有 `SET`/`GET`/`DEL`、`PING`/`ECHO`、`SAVE`（默认写入 `data/`）。

二进制 blob 不适合手敲 `redis-cli`；缓存命令请用客户端库、压测脚本（`bench/smoke/vector.sh`、`bench/smoke/vector_rdb.sh`、`vector_metrics.py`）或单测。字符串 KVS 与 `SAVE` 仍可用 `redis-cli`。

## 架构

```
client
  → TcpServer / EventLoop (epoll)
    → ProtocolExecutor + RespProtocolHandler
      → CommandHandler
        → VNodeIndex (VNodeStorage + USearchEmbedIndex)
        → KvStore
        → SnapshotManager
```

各层设计说明：

| 层 | 文档 |
|-------|-----|
| 网络 / 反应器 | [`docs/Network/Reactor.md`](docs/Network/Reactor.md) |
| 消息缓冲 | [`docs/Network/MessageBuffer.md`](docs/Network/MessageBuffer.md) |
| RESP / 命令 | [`docs/Protocol/Protocol.md`](docs/Protocol/Protocol.md) |
| 存储 | [`docs/Storage/StorageLayer.md`](docs/Storage/StorageLayer.md) |
| 持久化 / RDB | [`docs/Persist/Snapshot.md`](docs/Persist/Snapshot.md) |
| 持久化 / AOF | [`docs/Persist/Aof.md`](docs/Persist/Aof.md) |
| 嵌入索引 / 向量集合 | [`docs/Index/EmbedIndex.md`](docs/Index/EmbedIndex.md) |

目录布局：公开头文件在 `include/vemory/`，源码在 `src/`（含 `persist/`），schema 在 `proto/VNode.proto`（供后续复制的编解码）。
