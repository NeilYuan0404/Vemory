#!/usr/bin/env bash
# Thin wrapper: VSET → SAVE → dump.usearch smoke via Python + redis-py.
# Server must have persistence.dir matching DUMP_DIR (default: <repo>/data).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${ROOT}/bench/.venv/bin/python"
if [[ ! -x "${PY}" ]]; then
  PY="${PYTHON:-python3}"
fi
exec "${PY}" "${ROOT}/bench/smoke/vector_rdb.py" "$@"
