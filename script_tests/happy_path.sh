#!/usr/bin/env bash
# test.sh — Happy-path tests 1–4

set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# ---------- Colors ----------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_status()  { echo -e "${GREEN}[BUILD]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error()   { echo -e "${RED}[ERROR]${NC} $1"; }
pass()          { echo -e "${GREEN}[PASS]${NC} $1"; }
fail()          { echo -e "${RED}[FAIL]${NC}  $1"; exit 1; }

# ---------- Helpers ----------
# Run analyzer and capture stdout/stderr separately into temp files.
run_analyzer() {
  local queue_size="$1"; shift
  local plugins=("$@")
  OUT_FILE="$(mktemp)"
  ERR_FILE="$(mktemp)"
  # Read from stdin provided by caller through a here-doc / pipe
  cat | ./output/analyzer "$queue_size" "${plugins[@]}" >"$OUT_FILE" 2>"$ERR_FILE"
}

# Assert that stdout contains an exact line (without regex interpretation).
assert_stdout_has_line() {
  local expected="$1"
  if ! grep -Fxq -- "$expected" "$OUT_FILE"; then
    echo "STDOUT was:"
    cat "$OUT_FILE"
    fail "Expected exact line in STDOUT: $expected"
  fi
}

# Assert that stderr is empty.
assert_stderr_empty() {
  if [ -s "$ERR_FILE" ]; then
    echo "STDERR was:"
    cat "$ERR_FILE" >&2
    fail "STDERR must be empty"
  fi
}

# Assert that process printed the final shutdown line.
assert_shutdown_line() {
  assert_stdout_has_line "Pipeline shutdown complete"
}

# Assert that stdout equals the exact expected content (all lines, order matters).
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

# ---------- logger only ----------
test_logger_only() {
  run_analyzer 10 logger <<'EOF'
hello
<END>
EOF
  assert_stdout_has_line "[logger] hello"
  assert_shutdown_line
  assert_stderr_empty
  pass "Logger only"
}

# ---------- uppercaser + logger ----------
test_uppercaser_logger() {
  run_analyzer 10 uppercaser logger <<'EOF'
hello
<END>
EOF
  assert_stdout_has_line "[logger] HELLO"
  assert_shutdown_line
  assert_stderr_empty
  pass "Uppercaser + logger"
}

# ---------- rotator + logger ----------
test_rotator_logger() {
  run_analyzer 10 rotator logger <<'EOF'
ABCD
<END>
EOF
  assert_stdout_has_line "[logger] DABC"
  assert_shutdown_line
  assert_stderr_empty
  pass "Rotator + logger"
}

# ---------- flipper + logger ----------
test_flipper_logger() {
  run_analyzer 10 flipper logger <<'EOF'
abc
<END>
EOF
  assert_stdout_has_line "[logger] cba"
  assert_shutdown_line
  assert_stderr_empty
  pass "Flipper + logger"
}

# ---------- expander + logger ----------
test_expander_logger() {
  run_analyzer 10 expander logger <<'EOF'
abc
<END>
EOF
  assert_stdout_has_line "[logger] a b c"
  assert_shutdown_line
  assert_stderr_empty
  pass "Expander + logger"
}

# ---------- typewriter only ----------
test_typewriter_only() {
  run_analyzer 10 typewriter <<'EOF'
hello
<END>
EOF
  # We don't assert timing — only final content.
  assert_stdout_has_line "[typewriter] hello"
  assert_shutdown_line
  assert_stderr_empty
  pass "Typewriter only"
}

# ---------- full pipeline (official example) ----------
test_full_pipeline_example() {
  run_analyzer 20 uppercaser rotator logger flipper typewriter <<'EOF'
hello
<END>
EOF
  # Order is not enforced; just ensure both lines exist.
  assert_stdout_has_line "[logger] OHELL"
  assert_stdout_has_line "[typewriter] LLEHO"
  assert_shutdown_line
  assert_stderr_empty
  pass "Full pipeline example"
}


# ---------- sequence with small capacity (2) ----------
test_logger_sequence_capacity2() {
  run_analyzer 2 logger <<'EOF'
a
b
<END>
EOF
  # Expect exact order and only these lines in STDOUT:
  local expected=$'[logger] a\n[logger] b\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Two lines with queue size 2"
}

# ---------- max-length line (1024 chars) ----------
test_long_line_boundary() {
  # Build a 1024-char ASCII 'a' line (no trailing newline inside the line)
  local LONG
  LONG="$(head -c 1024 </dev/zero | tr '\0' 'a')"

  # Feed LONG then <END>
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  { printf "%s\n" "$LONG"; printf "%s\n" "<END>"; } | ./output/analyzer 10 logger >"$OUT_FILE" 2>"$ERR_FILE" || fail "Analyzer failed on 1024-char line"

  assert_stdout_has_line "[logger] $LONG"
  assert_shutdown_line
  assert_stderr_empty
  pass "Max-length (1024) line"
}

# ---------- finalize only (no work items) ----------
test_finalize_only() {
  run_analyzer 10 logger <<'EOF'
<END>
EOF
  # Expect exactly one line: the finalize message
  local expected=$'Pipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Finalize-only path"
}

# ---------- stdout hygiene (no internal logs) ----------
test_stdout_hygiene() {
  run_analyzer 10 uppercaser logger <<'EOF'
hello
<END>
EOF
  # Expect exactly two lines on STDOUT, nothing else:
  local expected=$'[logger] HELLO\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Stdout hygiene (only pipeline output)"
}

# ---------- deterministic long-chain: uppercaser -> expander -> rotator -> logger ----------
test_upper_expander_rotator_logger_exact() {
  # Input "Abc" → uppercaser: "ABC" → expander: "A B C" → rotator: "CA B " (note the trailing space)
  run_analyzer 10 uppercaser expander rotator logger <<'EOF'
Abc
<END>
EOF
  local expected=$'[logger] CA B \nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Deterministic chain (upper->expander->rotator->logger) exact output"
}


# ---------- Run ----------
echo ""
echo "Happy Path Tests:"
echo ""
test_logger_only
test_uppercaser_logger
test_rotator_logger
test_flipper_logger
test_expander_logger
test_typewriter_only
test_full_pipeline_example
test_logger_sequence_capacity2
test_long_line_boundary
test_finalize_only
test_stdout_hygiene
test_upper_expander_rotator_logger_exact



echo -e "${GREEN}All happy path tests passed.${NC}"
exit 0
