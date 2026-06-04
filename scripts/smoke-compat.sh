#!/usr/bin/env bash
# Optional: one-shot compat API check using env vars only (no keys in repo).
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [[ -z "${OPENAI_API_KEY:-}" || -z "${OPENAI_BASE_URL:-}" ]]; then
  echo "Set OPENAI_API_KEY and OPENAI_BASE_URL first." >&2
  exit 1
fi

export NEVERCODE_LIVE_COMPAT=1
make test-unit
