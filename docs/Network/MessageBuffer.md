## MessageBuffer

Userspace read buffer: contiguous `std::string` storage that handles TCP fragmentation / coalescing.  
**Not bound to a business protocol**; callers choose a read style per protocol and must not mix them.

I/O / connection layer: [`server.md`](server.md); RESP: [`Protocol.md`](Protocol.md).

### Two Read Styles (Do Not Mix)

| Style | API | Use for | Consume |
|-------|-----|---------|---------|
| **Line protocol** | `GetDataUntilCRLF` | One frame per line, ends with `\r\n` (debug, simple protocols) | `ReadCompleted(line_len + 2)` |
| **RESP (this project's command channel)** | `GetAllData` + `RespHandler` / `RespDecode` | Redis-style multi-line frames (`*n` + `$len`…) | `ReadCompleted(consumed)` (full-frame byte count from the decoder) |

Constraints:

- **Official Vemory commands use only the RESP path**; do not use `GetDataUntilCRLF` to "cut RESP frames".
- A RESP frame often spans multiple lines; line-based reads split `*3`, `$4`, `VSET` apart and cannot parse correctly.
- Pointers point into the internal buffer; views are invalid after `ReadCompleted`.

### Interface

| API | Signature | Notes |
|-----|-----------|-------|
| `Recv` | `int Recv(int fd, int *err)` | Read up to 4096 bytes from `fd` and append to the buffer. Return `>0` = bytes read; `0` = peer closed (`*err = 0`); `<0` = error (`*err = errno`). |
| `GetDataUntilCRLF` | `std::pair<char *, size_t> GetDataUntilCRLF()` | **Line protocol only**. First complete line `{start, length}`, **without** `\r\n`. No complete line → `{nullptr, 0}`. |
| `GetAllData` | `std::pair<char *, size_t> GetAllData()` | All unread bytes `{start, length}`. **RESP takes the stream from here**, then the decoder decides whether a full frame is present. Empty buffer → `{nullptr, 0}`. |
| `ReadCompleted` | `void ReadCompleted(size_t n)` | Drop the first `n` bytes. Line protocol: `line_len + 2`; RESP: `consumed` from the decoder. |
| `Size` | `size_t Size() const` | Current buffered byte count. |
| `Empty` | `bool Empty() const` | Whether the buffer is empty. |

### Typical usage: line protocol

```
Recv(fd, &err)
auto [p, n] = GetDataUntilCRLF()
// process [p, p+n)
ReadCompleted(n + 2)
```

### Typical usage: RESP (supported path)

See [`Protocol.md`](Protocol.md): `ProtocolExecutor` → `RespProtocolHandler::TryParse` (`RespHandler` + `FromArgv`) → `ReadCompleted(consumed)`.  
Do not call `GetDataUntilCRLF`.

---

## Paths

| Component | Header | Source |
|-----------|--------|--------|
| MessageBuffer | `include/vemory/net/MessageBuffer.h` | (header-only) |
