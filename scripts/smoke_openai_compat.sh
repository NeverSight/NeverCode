#!/bin/sh
# Optional live check against an OpenAI-compatible endpoint.
# Do not put API keys in this file; use environment variables only.
set -e
cd "$(dirname "$0")/.."
if [ ! -x ./nevercode ] && [ -f Makefile ]; then
  make -s
fi
if [ -z "${OPENAI_API_KEY}" ] || [ -z "${OPENAI_BASE_URL}" ]; then
  echo "smoke_openai_compat.sh: set OPENAI_API_KEY and OPENAI_BASE_URL" >&2
  exit 2
fi
MODEL="${NEVERCODE_MODEL:-gpt-5.4}"
# Default text must match NC_SMOKE_ONESHOT_PROMPT in include/defaults.h
PROMPT="${NEVERCODE_SMOKE_PROMPT:-Reply with exactly the two characters OK and nothing else.}"
exec ./nevercode -b "${OPENAI_BASE_URL}" -k "${OPENAI_API_KEY}" -m "${MODEL}" \
  -p "${PROMPT}"
