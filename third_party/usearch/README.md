# USearch (vendored)

Header-only ANN library used by `USearchEmbedIndex`.

| Item | Value |
|------|-------|
| Upstream | https://github.com/unum-cloud/usearch |
| Version | see `VERSION` |
| Layout | `include/usearch/` + `fp16/include/` (fp16 is required by usearch on non-AVX512 hosts) |

Only the C++ headers needed to compile are kept here (not the full upstream tree).

Restore / upgrade: `make usearch-fetch`
