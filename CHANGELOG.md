# Changelog

All notable changes to Vemory are documented in this file.

## [0.1.0] — 2026-07-19

First public MVP tag.

### Added
- RESP server over single-threaded epoll (`TcpServer` / `EventLoop`)
- Vector set commands: `VADD`, `VSIM`, `VDIM`, `VEMB`, `VCARD` (USearch-backed cosine ANN)
- String KVS: `SET`, `GET`, `DEL`
- Assist: `PING`, `ECHO`
- Optional INI config via `-c` (`conf/vemory.ini`): port, bind, log level, `kv_reserve`, `default_capacity`
- Unit tests (`make test`), bench scripts under `bench/`

### Limits
- No persistence / WAL — process exit clears all data
- No auth; bind carefully for non-local use
- Per-set element ids are `uint16` (~65k elements per key)
- Partial Redis Vector Set API (no `VREM`, filters, attrs, etc.)
