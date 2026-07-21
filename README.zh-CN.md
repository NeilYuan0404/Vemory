# Vemory

[English](README.md) | 中文

兼容 RESP 的向量集合（Vector Set）服务端（Redis Vector Set 风格子集）。可用 `redis-cli` 连接。

**v0.1.0 — 早期 MVP。** 数据**仅存内存**（重启即丢失）。单线程 epoll 反应器。命令集是 Redis Vector Set 的**子集**，另含基础 `SET`/`GET`/`DEL` —— 并非 Redis 的完整替代品。

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

未知节/键会被忽略（并告警）。位置参数端口仍会覆盖 `server.port`。

压测（服务端需已启动；依赖 `redis-benchmark` / `redis-cli`）：

```bash
./bench/smoke/kvs.sh       # PING / ECHO / SET / GET
./bench/smoke/pipeline.sh  # c=1 管道冒烟（仅 Vemory）
./bench/smoke/vector.sh    # VADD 预热 + VSIM
python3 bench/pipeline_bench.py                  # c=1 SET/GET：Vemory vs Redis
bench/.venv/bin/python bench/vector_metrics.py   # Recall@10 / p50·p99 / QPS@recall≥0.95（见 bench/README.md）
```

### 最近一次 pipeline 结果

运行：`python3 bench/pipeline_bench.py`（Vemory `127.0.0.1:8989`，Redis `127.0.0.1:6379`）

基线（`c=1`，`p=1`，`n=10000`）：

| Server | SET (rps) | GET (rps) |
|--------|-----------|-----------|
| Vemory | 9832.84 | 10559.66 |
| Redis | 8635.58 | 9433.96 |

Pipeline 扫描（`c=1`）：

| P | n | Vemory SET | Redis SET | Vemory GET | Redis GET |
|---|---:|-----------:|----------:|-----------:|----------:|
| 10 | 100000 | 110741.97 | 72621.64 | 71022.73 | 74682.60 |
| 20 | 100000 | 118203.30 | 71073.21 | 112866.82 | 112359.55 |
| 40 | 1000000 | 142979.70 | 100050.02 | 111358.58 | 141023.83 |
| 100 | 1000000 | 220312.84 | 169376.70 | 197863.08 | 180929.98 |
| 160 | 1000000 | 254777.08 | 190114.06 | 215656.67 | 261096.61 |

详见 [`bench/README.md`](bench/README.md)。

其他目标：

| 目标 | 用途 |
|--------|---------|
| `make run` | 构建并启动 `bin/vemory` |
| `make test` | GoogleTest 单元测试（`bin/unit_tests`） |
| `make proto` | 重新生成 `generated/VNode.pb.*` |
| `make compile-commands` | 刷新 `compile_commands.json`（供 clangd） |
| `make clean` | 删除 `build/`、`bin/`、`generated/` |

入口：[`src/Vemory.cc`](src/Vemory.cc)。维度在某 key 首次 `VADD` 时确定。

## 命令

线协议为 Redis RESP。Redis Vector Set 相关命令子集：

| 命令 | 参数 | 回复 |
|---------|------|-------|
| `VADD` | `<key> VALUES <dim> <f1> … <fN> <element>` | 整数 `1` |
| `VSIM` | `<key> ELE <element> [COUNT <n>] [WITHSCORES]` 或 `<key> VALUES <dim> <f1>…<fN> [COUNT <n>] [WITHSCORES]` | 元素数组（或 元素/分数 对） |
| `VDIM` | `<key>` | 整数维度 |
| `VEMB` | `<key> <element>` | 浮点数字符串数组 |
| `VCARD` | `<key>` | 整数基数（key 不存在时为 `0`） |

`COUNT` 默认 **10**。分数为余弦相似度（`1 - distance`）。

示例：

```bash
redis-cli VADD docs VALUES 8 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 apple
redis-cli VSIM docs VALUES 8 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 COUNT 3 WITHSCORES
redis-cli VSIM docs ELE apple COUNT 3
redis-cli VDIM docs
redis-cli VEMB docs apple
redis-cli VCARD docs
```

## 架构

```
redis-cli
  → TcpServer / EventLoop (epoll)
    → ProtocolExecutor + RespProtocolHandler
      → CommandHandler
        → VectorSetRegistry
          → VectorSet (name↔id, vectors, USearchEmbedIndex)
```

各层设计说明：

| 层 | 文档 |
|-------|-----|
| 网络 / 反应器 | [`docs/Network/Reactor.md`](docs/Network/Reactor.md) |
| 消息缓冲 | [`docs/Network/MessageBuffer.md`](docs/Network/MessageBuffer.md) |
| RESP / 命令 | [`docs/Protocol/Protocol.md`](docs/Protocol/Protocol.md) |
| 存储 | [`docs/Storage/StorageLayer.md`](docs/Storage/StorageLayer.md) |
| 嵌入索引 / 向量集合 | [`docs/Index/EmbedIndex.md`](docs/Index/EmbedIndex.md) |

目录布局：公开头文件在 `include/vemory/`，源码在 `src/`，schema 在 `proto/VNode.proto`（供后续复制的编解码）。
