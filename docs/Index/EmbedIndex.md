# Embed Index / Semantic Cache ANN

`USearchEmbedIndex` backs [`VNodeIndex`](../Storage/StorageLayer.md): cosine ANN keyed by the same `uint16_t` id as `VNodeStorage`.

```
VSET vector_blob user_key question answer
  → VNodeDispatcher → VNodeIndex::Set
  → VNodeStorage::Put + USearchEmbedIndex::Add

VGET query_blob threshold
  → VNodeIndex::Get → Search(k=1) → distance ≤ threshold → answer

VDEL user_key
  → index.Del + VNodeStorage::DelByUserKey
```

---

## Boundary

| Belongs here | Does not belong here |
|--------------|----------------------|
| `USearchEmbedIndex` | RESP wire decode |
| Cosine kNN by id | `KvStore` / SET GET DEL |
| | Redis Vector Set verbs (`VADD`/`VSIM`/…) — removed |

---

## USearchEmbedIndex

Usearch-backed ANN. Cosine metric, `f32`.

| API | Notes |
|-----|-------|
| `Add` / `Search` / `Del` | Keyed by `uint16_t` |
| `Save` / `Load` | usearch file path; used by RDB `dump.usearch` |
| `dimensions` | Fixed at construction / load |

`Search` hit `score` is **cosine distance** (lower is closer). `VNodeIndex` compares distance to `VGET` threshold.

### Constraints

- Callers must not include usearch headers outside `USearchEmbedIndex.cc`.
- Not thread-safe today.

---

## Paths

| Component | Header | Source |
|-----------|--------|--------|
| USearchEmbedIndex | `include/vemory/index/USearchEmbedIndex.h` | `src/index/USearchEmbedIndex.cc` |
| VNodeIndex | `include/vemory/storage/VNodeIndex.h` | `src/storage/VNodeIndex.cc` |

---

## Follow-ups (not implemented)

- Multi-tenant / namespaced indexes
- WAL / AOF; replication stream with vectors in protobuf
