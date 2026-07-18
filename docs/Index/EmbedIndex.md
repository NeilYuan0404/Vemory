# Embed Index / Vector Sets

Owns named Redis-style vector sets. Each set wraps `USearchEmbedIndex` plus element name ↔ id maps and a copy of vectors for `VEMB` / `VSIM ELE`.

```
VADD key VALUES dim … element
  → HandlerRegister → VectorDispatcher
  → VectorSetRegistry::GetOrCreate
  → VectorSet::Add → USearchEmbedIndex::Add

VSIM / VDIM / VEMB / VCARD
  → HandlerRegister → VectorDispatcher
  → VectorSetRegistry::Find
  → VectorSet::{Search,SearchByElement,GetEmbedding,size,dimensions}
```

---

## Boundary

| Belongs here | Does not belong here |
|--------------|----------------------|
| `VectorSet` / `VectorSetRegistry` | RESP wire decode |
| `USearchEmbedIndex` | `ProtobufVNodeCodec` / protobuf (replication) |
| kNN + element name resolution | Prompt/answer `VNodeStorage` (unused by commands) |
| | `KvStore` / SET GET DEL (string KVS) |

`VectorDispatcher` (via `CommandHandler` / `HandlerRegister`) syncs the vector wire verbs to the registry.

---

## VectorSet

| API | Notes |
|-----|-------|
| Constructor | `(dim, capacity=1024)` — dim locked for the set |
| `Add(element, data, dim)` | Insert or replace; assigns `uint16` id |
| `Search(query, dim, k, out)` | Hits with element name + cosine similarity |
| `SearchByElement(element, k, out)` | Load stored vector then Search |
| `GetEmbedding(element, out)` | For `VEMB` |
| `size()` / `dimensions()` | `VCARD` / `VDIM` |

## VectorSetRegistry

| API | Notes |
|-----|-------|
| `Find(key)` | Null if missing |
| `GetOrCreate(key, dim)` | Create on first `VADD`; dim must match later |

---

## USearchEmbedIndex

Usearch-backed ANN API used inside `VectorSet`. Cosine metric, `f32`.

| API | Notes |
|-----|-------|
| `Add` / `Search` / `Del` | Keyed by `uint16_t` |
| `dimensions` | Fixed at construction |

### Constraints

- Callers must not include usearch headers outside `USearchEmbedIndex.cc`.
- Not thread-safe today.

---

## Paths

| Component | Header | Source |
|-----------|--------|--------|
| VectorSet | `include/vemory/index/VectorSet.h` | `src/index/VectorSet.cc` |
| VectorSetRegistry | `include/vemory/index/VectorSetRegistry.h` | `src/index/VectorSetRegistry.cc` |
| USearchEmbedIndex | `include/vemory/index/USearchEmbedIndex.h` | `src/index/USearchEmbedIndex.cc` |

---

## Follow-ups (not implemented)

- `FP32` blob / `REDUCE` / quantization options
- `VREM`, `SETATTR`, `FILTER`, `EPSILON`
- Configurable capacity / persistence
