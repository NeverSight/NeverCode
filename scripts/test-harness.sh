#!/bin/sh
# test-harness.sh — Structured test runner with CI-friendly output.
# Runs unit tests, captures results, optionally outputs JSON report.

set -e

PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ_ROOT"

JSON_OUTPUT="${1:-}"
START_TS=$(date +%s)
TMP_BASE="${NEVERCODE_SCRATCH_DIR:-${TMPDIR:-/tmp}}"
TEST_OUTPUT="$TMP_BASE/nc_test_output.txt"

echo "=== nevercode test harness ==="
echo "Date:    $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "Host:    $(uname -s) $(uname -m)"
echo ""

# Phase 1: Build
echo "[phase 1/3] Building..."
BUILD_START=$(date +%s)
if make -j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4) 2>&1 | tail -1; then
    BUILD_STATUS="pass"
else
    BUILD_STATUS="fail"
    echo "FATAL: build failed"
    exit 1
fi
BUILD_END=$(date +%s)
BUILD_TIME=$((BUILD_END - BUILD_START))
echo "  Build:    ${BUILD_STATUS} (${BUILD_TIME}s)"

# Phase 2: Unit tests
echo "[phase 2/3] Unit tests..."
mkdir -p "$TMP_BASE"
TEST_START=$(date +%s)
if make test-unit 2>&1 | tee "$TEST_OUTPUT" | tail -5; then
    TEST_STATUS="pass"
else
    TEST_STATUS="fail"
fi
TEST_END=$(date +%s)
TEST_TIME=$((TEST_END - TEST_START))

echo "  Tests:    ${TEST_STATUS} (${TEST_TIME}s)"

# Phase 3: Binary health check
echo "[phase 3/3] Binary validation..."
BIN_SIZE=$(stat -f%z nevercode 2>/dev/null || stat -c%s nevercode 2>/dev/null || echo 0)
BIN_KB=$((BIN_SIZE / 1024))
echo "  Binary:   ${BIN_KB}KB"

VERSION=$(./nevercode -V 2>&1 | head -1)
echo "  Version:  ${VERSION}"

HELP_RC=0
./nevercode -h >/dev/null 2>&1 || HELP_RC=$?
if [ "$HELP_RC" = "0" ]; then
    echo "  Help:     pass"
else
    echo "  Help:     fail (exit $HELP_RC)"
fi

END_TS=$(date +%s)
TOTAL_TIME=$((END_TS - START_TS))

echo ""
echo "=== Complete (${TOTAL_TIME}s) ==="

if [ "$JSON_OUTPUT" = "--json" ]; then
    cat <<ENDJSON
{
  "timestamp": "$(date -u '+%Y-%m-%dT%H:%M:%SZ')",
  "platform": "$(uname -s) $(uname -m)",
  "version": "${VERSION}",
  "build": {"status": "${BUILD_STATUS}", "time_s": ${BUILD_TIME}},
  "tests": {"status": "${TEST_STATUS}", "time_s": ${TEST_TIME}},
  "binary": {"size_kb": ${BIN_KB}},
  "total_time_s": ${TOTAL_TIME}
}
ENDJSON
fi

[ "$TEST_STATUS" = "pass" ] && exit 0 || exit 1
