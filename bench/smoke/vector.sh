#!/usr/bin/env bash
# Thin wrapper: binary VSET/VGET/VDEL smoke via Python + redis-py.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PY="${ROOT}/bench/.venv/bin/python"
if [[ ! -x "${PY}" ]]; then
  PY="${PYTHON:-python3}"
fi
exec "${PY}" "${ROOT}/bench/smoke/vector.py" "$@"
