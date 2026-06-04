#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v clang-format >/dev/null 2>&1; then
  echo "Error: clang-format not found in PATH."
  echo "Install it first (e.g. brew install clang-format)."
  exit 1
fi

if ! command -v git >/dev/null 2>&1; then
  echo "Error: git not found in PATH."
  exit 1
fi

files=()
while IFS= read -r f; do
  [[ -n "$f" && -f "$f" ]] && files+=("$f")
done < <(git ls-files "include/*.h" "src/*.c" "tests/*.c" "tests/*.h")

if [[ ${#files[@]} -eq 0 ]]; then
  echo "No C/C header files found under include/, src/, tests/."
  exit 0
fi

clang-format -i "${files[@]}"

echo "Formatted ${#files[@]} files with .clang-format"
