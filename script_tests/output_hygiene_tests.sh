#!/usr/bin/env bash
# output_hygiene_tests.sh — Output hygiene tests (clean run)
# Notes:
# - Does NOT build the project; assumes ./output/analyzer and plugins already exist.
# - This file is self-contained: includes minimal helpers used by the test.
# - All comments are in English only.

set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# ---------- Colors ----------
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC}  $1"; exit 1; }

# ---------- Globals ----------
OUT_FILE=""
ERR_FILE=""
STATUS=0

# ---------- Helpers ----------
# Run analyzer and capture stdout/stderr. Stdin must be provided by caller (here-doc/redirect).
run_analyzer() {
  local queue_size="$1"; shift
  OUT_FILE="$(mktemp)"
  ERR_FILE="$(mktemp)"
  ./output/analyzer "$queue_size" "$@" >"$OUT_FILE" 2>"$ERR_FILE"
  STATUS=$?
}

# Assert specific exit code
assert_exit_code_eq() {
  local expected="$1"
  if [ "${STATUS:-0}" -ne "$expected" ]; then
    echo "Expected exit code: $expected, got: ${STATUS:-unset}"
    echo "STDOUT:"; cat "$OUT_FILE"
    echo "STDERR:"; cat "$ERR_FILE" >&2
    fail "Exit code mismatch"
  fi
}

# Assert empty STDERR
assert_stderr_empty() {
  if [ -s "$ERR_FILE" ]; then
    echo "STDERR was:"
    cat "$ERR_FILE" >&2
    fail "STDERR must be empty on a clean run"
  fi
}

# Assert exact STDOUT (all lines; order matters)
assert_stdout_equals() {
  local expected="$1"
  if ! diff -u <(printf "%s\n" "$expected") "$OUT_FILE" >/dev/null; then
    echo "Expected STDOUT:"
    printf "%s\n" "$expected"
    echo "Actual STDOUT:"
    cat "$OUT_FILE"
    fail "STDOUT does not match exactly"
  fi
}

# ---------- Test: Output hygiene on a clean run ----------
# Goal:
# - STDOUT contains only pipeline lines and a single final shutdown line.
# - STDERR is empty.
# - Exit code is 0.
test_stdout_hygiene_clean_run() {
  run_analyzer 1 logger <<'EOF'
hello
[DEBUG] should be treated as data
INFO: still data
<END>
EOF

  # Core assertions
  assert_exit_code_eq 0
  local expected=$'[logger] hello\n[logger] [DEBUG] should be treated as data\n[logger] INFO: still data\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty

  # Hygiene extras:
  # 1) Verify there are no non-pipeline lines in STDOUT
  if grep -Evq '^\[logger\] |^Pipeline shutdown complete$' "$OUT_FILE"; then
    echo "STDOUT was:"; cat "$OUT_FILE"
    fail "STDOUT contains non-pipeline lines"
  fi

  # 2) Verify the final shutdown line appears exactly once
  if [ "$(grep -c -x 'Pipeline shutdown complete' "$OUT_FILE")" -ne 1 ]; then
    echo "STDOUT was:"; cat "$OUT_FILE"
    fail "Finalize line must appear exactly once"
  fi

  # 3) Verify the number of data lines is exactly 3
  if [ "$(grep -c -E '^\[logger\] ' "$OUT_FILE")" -ne 3 ]; then
    echo "STDOUT was:"; cat "$OUT_FILE"
    fail "Unexpected number of data lines in STDOUT"
  fi

  pass "Output hygiene: clean run is correct"
}



# ---------- Run ----------
echo ""
echo "Output Hygiene Tests:"
echo ""
test_stdout_hygiene_clean_run

echo ""
echo -e "${GREEN}All output hygiene tests passed.${NC}"
exit 0
