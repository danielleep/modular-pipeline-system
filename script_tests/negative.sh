#!/usr/bin/env bash
# negative.sh — Negative tests 1–3 for the analyzer pipeline
# Notes:
# - Does NOT build the project; assumes ./output/analyzer and plugins already exist.
# - Uses colored PASS/FAIL output.
# - Captures STDOUT/STDERR and validates exit codes precisely.
# - Implements only tests 1–3:
#   1) CLI usage errors
#   2) Plugin load/resolve failures (dlopen/dlsym)
#   3) Plugin init failure (exit code 2, no Usage)
#
# We avoid `set -e` to let tests report nice errors; helpers will exit on failure.
set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# ---------- Colors ----------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TIMEOUT=10

print_status()  { echo -e "${GREEN}[BUILD]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error()   { echo -e "${RED}[ERROR]${NC} $1"; }
pass()          { echo -e "${GREEN}[PASS]${NC} $1"; }
fail()          { echo -e "${RED}[FAIL]${NC}  $1"; exit 1; }

# ---------- Helpers ----------
OUT_FILE=""; ERR_FILE=""; STATUS=0

# Run a command, capturing STDOUT/STDERR and exit status.
run_capture() {
  OUT_FILE="$(mktemp)"
  ERR_FILE="$(mktemp)"
  timeout "$TIMEOUT" bash -c "$*" >"$OUT_FILE" 2>"$ERR_FILE"
  STATUS=$?
  if [ "$STATUS" -eq 124 ]; then
    echo "Timed out after ${TIMEOUT}s."
    echo "STDOUT:"; cat "$OUT_FILE"
    echo "STDERR:"; cat "$ERR_FILE" >&2
    fail "Command timed out"
  fi
}

# ---- Helpers missing in negative.sh (paste this under your existing helpers) ----

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

# Assert that stderr is empty.
assert_stderr_empty() {
  if [ -s "$ERR_FILE" ]; then
    echo "STDERR was:"
    cat "$ERR_FILE" >&2
    fail "STDERR must be empty"
  fi
}

assert_stdout_has_line() {
  local expected="$1"
  if ! grep -Fxq -- "$expected" "$OUT_FILE"; then
    echo "STDOUT was:"
    cat "$OUT_FILE"
    fail "Expected exact line in STDOUT: $expected"
  fi
}


# Assertions
assert_exit_code_eq() {
  local expected="$1"
  if [ "$STATUS" -ne "$expected" ]; then
    echo "Expected exit code: $expected, got: $STATUS"
    echo "STDOUT:"; cat "$OUT_FILE"
    echo "STDERR:"; cat "$ERR_FILE" >&2
    fail "Exit code mismatch"
  fi
}

assert_stdout_contains_usage() {
  if ! grep -Fq "Usage:" "$OUT_FILE"; then
    echo "STDOUT was:"
    cat "$OUT_FILE"
    fail "STDOUT must contain a Usage block"
  fi
}

# Optional stricter check that main Usage block includes headline and known plugin list.
assert_stdout_usage_block_complete() {
  local must_have=(
    "Usage: ./analyzer <queue_size> <plugin1> <plugin2> ... <pluginN>"
    "Available plugins:"
    "logger        - Logs all strings that pass through"
    "typewriter    - Simulates typewriter effect with delays"
    "uppercaser    - Converts strings to uppercase"
    "rotator       - Move every character to the right."
    "flipper       - Reverses the order of characters"
    "expander      - Expands each character with spaces"
  )
  local missing=0
  for needle in "${must_have[@]}"; do
    if ! grep -Fq -- "$needle" "$OUT_FILE"; then
      echo "Missing from Usage: $needle"
      missing=1
    fi
  done
  if [ $missing -ne 0 ]; then
    echo "STDOUT was:"
    cat "$OUT_FILE"
    fail "Usage block is incomplete"
  fi
}

assert_stderr_nonempty() {
  if [ ! -s "$ERR_FILE" ]; then
    echo "STDERR was empty"
    fail "STDERR must contain an error message"
  fi
}

assert_stdout_not_contains_finalize() {
  if grep -Fq "Pipeline shutdown complete" "$OUT_FILE"; then
    echo "STDOUT was:"
    cat "$OUT_FILE"
    fail "Usage/init errors must not print the final shutdown line"
  fi
}

# ---------- Test fixtures (built on-the-fly; do NOT modify project code) ----------
ensure_broken_plugin() {
  # Creates ./output/broken_plugin.so that is missing all required plugin_* symbols.
  [ -f "./output/broken_plugin.so" ] && return 0
  TMP_C="$(mktemp --suffix=.c)"
  cat >"$TMP_C" <<'EOF'
/* Broken test plugin: intentionally missing required plugin_* symbols */
int placeholder_symbol = 42;
EOF
  gcc -shared -fPIC -o ./output/broken_plugin.so "$TMP_C" \
    || fail "Failed to compile broken_plugin.so"
}

ensure_initfail_plugin() {
  # Creates ./output/initfail.so where plugin_init always fails (returns error string).
  [ -f "./output/initfail.so" ] && return 0
  TMP_C="$(mktemp --suffix=.c)"
  cat >"$TMP_C" <<'EOF'
/* Init-fail test plugin: exposes the required names; plugin_init fails. */
/* Variadic signatures avoid depending on project headers. */
const char* plugin_get_name(void) { return "initfail"; }
const char* plugin_init(...) { return "init failed (test)"; }
const char* plugin_fini(...) { return 0; }
const char* plugin_place_work(...) { return 0; }
const char* plugin_attach(...) { return 0; }
const char* plugin_wait_finished(...) { return 0; }
EOF
  gcc -shared -fPIC -o ./output/initfail.so "$TMP_C" \
    || fail "Failed to compile initfail.so"
}

# ---------- Negative 1: CLI usage errors (Exit 1 + Usage on STDOUT + STDERR non-empty; no finalize) ----------
test_negative_usage_errors() {

  # Case A: no arguments at all
  run_capture "./output/analyzer"
  assert_exit_code_eq 1
  assert_stdout_contains_usage
  assert_stderr_nonempty
  assert_stdout_not_contains_finalize
  pass "no arguments"

  # Case B: only queue_size provided, no plugins
  run_capture "./output/analyzer 10"
  assert_exit_code_eq 1
  assert_stdout_contains_usage
  assert_stderr_nonempty
  assert_stdout_not_contains_finalize
  pass "queue_size without plugins"

  # Case C: invalid queue_size: non-numeric
  run_capture "./output/analyzer abc logger"
  assert_exit_code_eq 1
  assert_stdout_contains_usage
  assert_stderr_nonempty
  assert_stdout_not_contains_finalize
  pass "non-numeric queue_size"

  # Case D: invalid queue_size: zero
  run_capture "./output/analyzer 0 logger"
  assert_exit_code_eq 1
  assert_stdout_contains_usage
  assert_stderr_nonempty
  assert_stdout_not_contains_finalize
  pass "zero queue_size"

  # Case E: invalid queue_size: negative
  run_capture "./output/analyzer -3 logger"
  assert_exit_code_eq 1
  assert_stdout_contains_usage
  assert_stderr_nonempty
  assert_stdout_not_contains_finalize
  pass "negative queue_size"

  # Case F: invalid plugin name (with .so suffix)
  run_capture "./output/analyzer 10 logger.so"
  assert_exit_code_eq 1
  assert_stdout_contains_usage
  assert_stderr_nonempty
  assert_stdout_not_contains_finalize
  pass "plugin name with .so"

  # Optional strict validation of Usage block structure/text:
  assert_stdout_usage_block_complete || true
}

# ---------- Negative 2: plugin load/resolve failures (dlopen/dlsym) (Exit 1 + Usage + STDERR; no finalize) ----------
test_negative_plugin_load_failures() {

  # Case A: Non-existent plugin name
  run_capture "./output/analyzer 10 idontexist"
  assert_exit_code_eq 1
  assert_stdout_contains_usage
  assert_stderr_nonempty
  assert_stdout_not_contains_finalize
  pass "Load A: non-existent plugin"

  # Case B: Broken plugin missing required symbol — build on-the-fly
  ensure_broken_plugin
  run_capture "./output/analyzer 10 broken_plugin"
  assert_exit_code_eq 1
  assert_stdout_contains_usage
  assert_stderr_nonempty
  assert_stdout_not_contains_finalize
  pass "Load B: broken plugin (missing required symbol)"
}

# ---------- Negative 3: plugin_init failure (Exit 2; STDERR non-empty; no Usage; no finalize) ----------
test_negative_plugin_init_failure() {

  ensure_initfail_plugin
  run_capture "./output/analyzer 10 initfail"

  # Expect exit code 2, error on stderr, NO Usage in stdout, and no finalize line.
  assert_exit_code_eq 2
  assert_stderr_nonempty
  if grep -Fq "Usage:" "$OUT_FILE"; then
    echo "STDOUT was:"; cat "$OUT_FILE"
    echo "STDERR was:"; cat "$ERR_FILE" >&2
    fail "Init failure must not print Usage to STDOUT"
  fi
  assert_stdout_not_contains_finalize
  pass "Init: failing plugin_init (initfail.so)"
}

# ---------- 4) Concurrency & Backpressure ----------

test_backpressure_typewriter_single_q1() {

  local input=$'one\ntwo\nthree\n<END>\n'
  run_capture "printf '%s' \"$input\" | ./output/analyzer 1 typewriter"
  assert_exit_code_eq 0
  local expected=$'[typewriter] one\n[typewriter] two\n[typewriter] three\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Backpressure with single typewriter (queue=1)"
}


test_backpressure_heavy_load_capacity1() {

  local INPUT_FILE EXPECT_FILE
  INPUT_FILE="$(mktemp)"
  EXPECT_FILE="$(mktemp)"
  for i in $(seq 1 200); do
    printf "l%u\n" "$i" >>"$INPUT_FILE"
    printf "[logger] l%u\n" "$i" >>"$EXPECT_FILE"
  done
  printf "<END>\n" >>"$INPUT_FILE"
  printf "Pipeline shutdown complete\n" >>"$EXPECT_FILE"

  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  cat "$INPUT_FILE" | ./output/analyzer 1 logger >"$OUT_FILE" 2>"$ERR_FILE" || fail "Analyzer failed on heavy load (queue=1)"

  if ! diff -u "$EXPECT_FILE" "$OUT_FILE" >/dev/null; then
    echo "Expected:"; cat "$EXPECT_FILE"
    echo "Actual:";   cat "$OUT_FILE"
    fail "Order/content mismatch under heavy load"
  fi
  assert_stderr_empty
  pass "Heavy load preserved order (queue=1)"
}

# ---------- 5) Happy-path ----------

test_chain_upper_expander_rotator_logger() {

  run_capture "printf 'Abc\n<END>\n' | ./output/analyzer 10 uppercaser expander rotator logger"
  assert_exit_code_eq 0
  local expected=$'[logger] CA B \nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "uppercaser->expander->rotator->logger on 'Abc'"
}

test_chain_repeated_plugins() {
  echo ""
  echo -e "-- Deterministic chain: repeated plugins --"
  echo ""

  # uppercaser -> uppercaser -> logger
  run_capture "printf 'aBc\n<END>\n' | ./output/analyzer 10 uppercaser uppercaser logger"
  assert_exit_code_eq 0
  local expected1=$'[logger] ABC\nPipeline shutdown complete'
  assert_stdout_equals "$expected1"
  assert_stderr_empty

  run_capture "printf 'abcd\n<END>\n' | ./output/analyzer 10 rotator rotator logger"
  assert_exit_code_eq 0
  local expected2=$'[logger] cdab\nPipeline shutdown complete'
  assert_stdout_equals "$expected2"
  assert_stderr_empty

  pass "Repeated plugins behave correctly"
}

# ---------- 6 ----------

test_sentinel_precision() {

  run_capture "{ printf '<END> \n'; printf '<END>\n'; } | ./output/analyzer 10 logger"
  assert_exit_code_eq 0
  local expected_A=$'[logger] <END> \nPipeline shutdown complete'
  assert_stdout_equals "$expected_A"
  assert_stderr_empty

  run_capture "{ printf '<end>\n'; printf '<END>\n'; } | ./output/analyzer 10 logger"
  assert_exit_code_eq 0
  local expected_B=$'[logger] <end>\nPipeline shutdown complete'
  assert_stdout_equals "$expected_B"
  assert_stderr_empty

  run_capture "{ printf 'foo<END>bar\n'; printf '<END>\n'; } | ./output/analyzer 10 logger"
  assert_exit_code_eq 0
  local expected_C=$'[logger] foo<END>bar\nPipeline shutdown complete'
  assert_stdout_equals "$expected_C"
  assert_stderr_empty

  pass "Only exact '<END>' closes the pipeline"
}

test_input_after_end_ignored() {

  run_capture "printf '<END>\nSHOULD_NOT_APPEAR\n' | ./output/analyzer 10 logger"
  assert_exit_code_eq 0
  local expected=$'Pipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Input after <END> is ignored"
}

test_expander_empty_and_symbols() {

  run_capture "printf '\n<END>\n' | ./output/analyzer 10 expander logger"
  assert_exit_code_eq 0
  local expected_empty=$'[logger] \nPipeline shutdown complete'
  assert_stdout_equals "$expected_empty"
  assert_stderr_empty

  run_capture "printf '123abc!?\n<END>\n' | ./output/analyzer 10 uppercaser logger"
  assert_exit_code_eq 0
  local expected_up=$'[logger] 123ABC!?\nPipeline shutdown complete'
  assert_stdout_equals "$expected_up"
  assert_stderr_empty

  run_capture "printf 'a b,c\n<END>\n' | ./output/analyzer 10 rotator logger"
  assert_exit_code_eq 0
  local expected_rot=$'[logger] ca b,\nPipeline shutdown complete'
  assert_stdout_equals "$expected_rot"
  assert_stderr_empty

  run_capture "printf 'a b,c\n<END>\n' | ./output/analyzer 10 flipper logger"
  assert_exit_code_eq 0
  local expected_flip=$'[logger] c,b a\nPipeline shutdown complete'
  assert_stdout_equals "$expected_flip"
  assert_stderr_empty

  run_capture "printf 'a b,c\n<END>\n' | ./output/analyzer 10 expander logger"
  assert_exit_code_eq 0
  local expected_exp=$'[logger] a   b , c\nPipeline shutdown complete'
  assert_stdout_equals "$expected_exp"
  assert_stderr_empty

  pass "Expander empty & symbols; punctuation cases validated"
}


# ---------- Run ----------
echo ""
echo "Negative Tests:"
echo ""

test_negative_usage_errors
test_negative_plugin_load_failures
test_negative_plugin_init_failure
test_backpressure_typewriter_single_q1
test_backpressure_heavy_load_capacity1
test_chain_upper_expander_rotator_logger
test_sentinel_precision
test_input_after_end_ignored
test_expander_empty_and_symbols

echo ""
echo -e "${GREEN}Negative tests finished.${NC}"

# Clean temporary fixtures
rm -f ./output/broken_plugin.so ./output/initfail.so 2>/dev/null || true
exit 0
