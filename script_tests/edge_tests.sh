#!/usr/bin/env bash
# edge_tests.sh — Edge cases 1–4 for the analyzer pipeline
# Notes:
# - This script is self-contained: it builds the project before running tests.
# - All messages use colored PASS/FAIL output.
# - STDOUT/STDERR are captured separately for each test.

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
# Track last analyzer exit status (needed by assert_exit_code_eq)
STATUS=0

# Run analyzer and capture stdout/stderr separately into temp files.
run_analyzer() {
  local queue_size="$1"; shift
  local plugins=("$@")
  OUT_FILE="$(mktemp)"
  ERR_FILE="$(mktemp)"
  # Read from stdin provided by caller through a here-doc / pipe
  cat | ./output/analyzer "$queue_size" "${plugins[@]}" >"$OUT_FILE" 2>"$ERR_FILE"
  STATUS=$?
}

# Assert that stdout contains an exact line (no regex interpretation).
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


# ---------- Edge 1: empty line ----------
test_edge_empty_line() {
  run_analyzer 10 logger <<'EOF'

<END>
EOF
  # Expect exactly two lines: empty payload and then finalize.
  local expected=$'[logger] \nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Empty line"
}

# ---------- Edge 2: whitespace-only (spaces and tabs) ----------
test_edge_whitespace_only() {
  # Case A: three spaces (payload = '   ')
  run_analyzer 10 logger <<'EOF'
   
<END>
EOF
  # Build expected as: "[logger] " + three extra spaces
  local expected_spaces_header='[logger] '
  local expected_spaces="${expected_spaces_header}   "
  local expected_spaces_block=$"${expected_spaces}"$'\n''Pipeline shutdown complete'
  assert_stdout_equals "$expected_spaces_block"
  assert_stderr_empty

  # Case B: two tabs (payload = '\t\t')
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  { printf "\t\t\n"; printf "<END>\n"; } | ./output/analyzer 10 logger >"$OUT_FILE" 2>"$ERR_FILE"
  local expected_tabs_header='[logger] '
  local expected_tabs="${expected_tabs_header}"$'\t\t'
  local expected_tabs_block=$"${expected_tabs}"$'\n''Pipeline shutdown complete'
  assert_stdout_equals "$expected_tabs_block"
  assert_stderr_empty

  pass "Whitespace-only (spaces/tabs)"
}

# ---------- Edge 3: max-length line (1024 chars) ----------
test_edge_max_length_1024() {
  # Build a 1024-char ASCII 'a' line (no internal newline)
  local LONG
  LONG="$(head -c 1024 </dev/zero | tr '\0' 'a')"

  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  { printf "%s\n" "$LONG"; printf "%s\n" "<END>"; } | ./output/analyzer 10 logger >"$OUT_FILE" 2>"$ERR_FILE" \
    || fail "Analyzer failed on 1024-char line"

  # Expect exactly two lines: the long payload and finalize.
  local expected_header='[logger] '
  local expected_payload="${expected_header}${LONG}"
  local expected=$"${expected_payload}"$'\n''Pipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Max-length (1024) line"
}

# ---------- Edge 4: line without trailing newline (then <END>) ----------
test_edge_no_newline_before_end() {
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  { printf "hello"; printf "\n<END>\n"; } | ./output/analyzer 10 logger >"$OUT_FILE" 2>"$ERR_FILE" \
    || fail "Analyzer failed on input without trailing newline"

  local expected=$'[logger] hello\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Line without trailing newline (then <END>)"
}

# ---------- Edge 5: CRLF (\r\n) compatibility ----------
test_edge_crlf_compatibility() {
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  { printf "hello\r\n"; printf "<END>\r\n"; } \
    | ./output/analyzer 10 logger >"$OUT_FILE" 2>"$ERR_FILE" \
    || fail "Analyzer failed on CRLF input"

  # Expect: no stray '\r' in output
  local expected=$'[logger] hello\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "CRLF input compatibility"
}

# ---------- Edge 6: <END> only (no work items) ----------
test_edge_end_only() {
  run_analyzer 10 logger <<'EOF'
<END>
EOF
  # Expect exactly one line on STDOUT
  local expected=$'Pipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "<END> only"
}

# ---------- Edge 7: <END> after multiple lines ----------
test_edge_end_after_lines() {
  run_analyzer 10 logger <<'EOF'
a
b
c
<END>
EOF
  # Exact order must be preserved
  local expected=$'[logger] a\n[logger] b\n[logger] c\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "<END> after multiple lines"
}

# ---------- Edge 8: sentinel precision (only exact '<END>' closes) ----------
test_edge_sentinel_precision() {
  # Case A: '<END> ' (with a trailing space) should NOT close
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  { printf "<END> \n"; printf "<END>\n"; } \
    | ./output/analyzer 10 logger >"$OUT_FILE" 2>"$ERR_FILE" \
    || fail "Analyzer failed on sentinel case A"
  local expected_A=$'[logger] <END> \nPipeline shutdown complete'
  assert_stdout_equals "$expected_A"
  assert_stderr_empty

  # Case B: '<end>' (lowercase) should NOT close
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  { printf "<end>\n"; printf "<END>\n"; } \
    | ./output/analyzer 10 logger >"$OUT_FILE" 2>"$ERR_FILE" \
    || fail "Analyzer failed on sentinel case B"
  local expected_B=$'[logger] <end>\nPipeline shutdown complete'
  assert_stdout_equals "$expected_B"
  assert_stderr_empty

  # Case C: 'foo<END>bar' should NOT close
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  { printf "foo<END>bar\n"; printf "<END>\n"; } \
    | ./output/analyzer 10 logger >"$OUT_FILE" 2>"$ERR_FILE" \
    || fail "Analyzer failed on sentinel case C"
  local expected_C=$'[logger] foo<END>bar\nPipeline shutdown complete'
  assert_stdout_equals "$expected_C"
  assert_stderr_empty

  pass "Sentinel precision (A/B/C)"
}

# ---------- Edge 9: queue capacity = 1 under light load ----------
test_edge_queue_capacity1_load() {
  run_analyzer 1 logger <<'EOF'
a
b
c
<END>
EOF
  # Expect exact order with capacity=1
  local expected=$'[logger] a\n[logger] b\n[logger] c\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Queue size 1 (a, b, c then finalize)"
}

# ---------- Edge 10: typewriter backpressure with small queue ----------
test_edge_typewriter_backpressure() {
  run_analyzer 2 typewriter <<'EOF'
one
two
three
<END>
EOF
  # We don't assert timings, only final content and order.
  local expected=$'[typewriter] one\n[typewriter] two\n[typewriter] three\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Typewriter backpressure (queue=2)"
}

# ---------- Edge 11: single-character inputs across transforms ----------
test_edge_single_char_transforms() {
  # Case A: rotator should keep single char unchanged
  run_analyzer 10 rotator logger <<'EOF'
X
<END>
EOF
  local expected_rot=$'[logger] X\nPipeline shutdown complete'
  assert_stdout_equals "$expected_rot"
  assert_stderr_empty

  # Case B: flipper should keep single char unchanged
  run_analyzer 10 flipper logger <<'EOF'
X
<END>
EOF
  local expected_flip=$'[logger] X\nPipeline shutdown complete'
  assert_stdout_equals "$expected_flip"
  assert_stderr_empty

  # Case C: expander should not add spaces for single char
  run_analyzer 10 expander logger <<'EOF'
X
<END>
EOF
  local expected_exp=$'[logger] X\nPipeline shutdown complete'
  assert_stdout_equals "$expected_exp"
  assert_stderr_empty

  pass "Single-character transforms (rotator/flipper/expander)"
}

# ---------- Edge 12: uppercaser with non-alphabetic characters ----------
test_edge_uppercaser_nonalpha() {
  run_analyzer 10 uppercaser logger <<'EOF'
123abc!?
<END>
EOF
  local expected=$'[logger] 123ABC!?\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Uppercaser + non-alpha"
}

# ---------- Edge 13: punctuation + spaces inside string ----------
test_edge_punct_and_spaces_transforms() {
  # Case A: rotator on "a b,c" -> "ca b,"
  run_analyzer 10 rotator logger <<'EOF'
a b,c
<END>
EOF
  local expected_rot=$'[logger] ca b,\nPipeline shutdown complete'
  assert_stdout_equals "$expected_rot"
  assert_stderr_empty

  # Case B: flipper on "a b,c" -> "c,b a"
  run_analyzer 10 flipper logger <<'EOF'
a b,c
<END>
EOF
  local expected_flip=$'[logger] c,b a\nPipeline shutdown complete'
  assert_stdout_equals "$expected_flip"
  assert_stderr_empty

  # Case C: expander on "a b,c"
  # Expander inserts ONE space between EVERY pair of characters, including spaces/punctuation:
  # "a b,c" => 'a␠␠␠b␠,␠c'  (three spaces between 'a' and 'b', one before comma, one before 'c')
  run_analyzer 10 expander logger <<'EOF'
a b,c
<END>
EOF
  local expected_exp=$'[logger] a   b , c\nPipeline shutdown complete'
  assert_stdout_equals "$expected_exp"
  assert_stderr_empty

  pass "Transforms with spaces/punctuation (rotator/flipper/expander)"
}

# ---------- Edge 14: many lines in sequence (order + stability) ----------
test_edge_many_lines_sequence() {
  run_analyzer 10 logger <<'EOF'
l1
l2
l3
l4
l5
l6
l7
l8
l9
l10
<END>
EOF
  local expected=$'[logger] l1\n[logger] l2\n[logger] l3\n[logger] l4\n[logger] l5\n[logger] l6\n[logger] l7\n[logger] l8\n[logger] l9\n[logger] l10\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Many lines preserved in order"
}

# ---------- Edge 15: long chain with empty/single-char inputs ----------
test_edge_long_chain_empty_and_single() {
  # Chain: uppercaser -> rotator -> logger -> flipper -> expander -> typewriter

  # Subcase A: empty line
  run_analyzer 10 uppercaser rotator logger flipper expander typewriter <<'EOF'

<END>
EOF
  # We don't enforce order between logger/typewriter lines; just require both and finalize.
  assert_stdout_has_line "[logger] "
  assert_stdout_has_line "[typewriter] "
  assert_shutdown_line
  assert_stderr_empty

  # Subcase B: single char 'X'
  run_analyzer 10 uppercaser rotator logger flipper expander typewriter <<'EOF'
X
<END>
EOF
  assert_stdout_has_line "[logger] X"
  assert_stdout_has_line "[typewriter] X"
  assert_shutdown_line
  assert_stderr_empty

  pass "Long chain with empty and single-char inputs"
}

# ---------- Edge 16: STDOUT hygiene (only pipeline output) ----------
test_edge_stdout_hygiene_again() {
  # Case A: uppercaser + logger -> exactly two lines
  run_analyzer 10 uppercaser logger <<'EOF'
hello
<END>
EOF
  local expected_A=$'[logger] HELLO\nPipeline shutdown complete'
  assert_stdout_equals "$expected_A"
  assert_stderr_empty

  # Case B: typewriter only -> exactly two lines
  run_analyzer 10 typewriter <<'EOF'
hello
<END>
EOF
  local expected_B=$'[typewriter] hello\nPipeline shutdown complete'
  assert_stdout_equals "$expected_B"
  assert_stderr_empty

  pass "STDOUT hygiene (no internal logs, exact lines)"
}

# ---------- Edge Cases: END precision, input after END, multiple ENDs ----------

# 1) END precision — only exact "<END>" closes the pipeline
test_end_precision() {

  # Case A: "<END> " (with trailing space) should NOT close
  run_analyzer 1 logger <<'EOF'
<END> 
<END>
EOF
  assert_exit_code_eq 0
  # NOTE: keep the trailing space after <END>
  local expected_A=$'[logger] <END> \nPipeline shutdown complete'
  assert_stdout_equals "$expected_A"
  assert_stderr_empty

  # Case B: "<end>" (lowercase) should NOT close
  run_analyzer 1 logger <<'EOF'
<end>
<END>
EOF
  assert_exit_code_eq 0
  local expected_B=$'[logger] <end>\nPipeline shutdown complete'
  assert_stdout_equals "$expected_B"
  assert_stderr_empty

  # Case C: "foo<END>bar" should NOT close
  run_analyzer 1 logger <<'EOF'
foo<END>bar
<END>
EOF
  assert_exit_code_eq 0
  local expected_C=$'[logger] foo<END>bar\nPipeline shutdown complete'
  assert_stdout_equals "$expected_C"
  assert_stderr_empty

  pass "END precision validated (logger, queue=1)"
}

# 2) Input after END is ignored — nothing after sentinel should be processed
test_input_after_end_ignored() {
  run_analyzer 1 logger <<'EOF'
<END>
SHOULD_NOT_APPEAR
EOF
  assert_exit_code_eq 0
  local expected=$'Pipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Only final shutdown line appears after <END>"
}

# 3) Multiple END lines — exactly one shutdown line; data before first END is processed
test_multiple_end_single_finalize() {

  run_analyzer 1 logger <<'EOF'
abc
<END>
<END>
EOF
  assert_exit_code_eq 0
  local expected=$'[logger] abc\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty

  # Optional sanity: ensure shutdown line appears exactly once
  if [ "$(grep -c -F 'Pipeline shutdown complete' "$OUT_FILE")" -ne 1 ]; then
    echo "STDOUT was:"; cat "$OUT_FILE"
    fail "Shutdown line must appear exactly once"
  fi

  pass "Multiple ENDs yield a single clean shutdown (logger, queue=1)"
}

# ---------- 4) END with leading/trailing spaces/tabs: only exact "<END>" closes ----------
test_end_precision_with_spaces_tabs() {

  # Case A: spaces BEFORE <END> should NOT close (two spaces before)
  local TMP_A; TMP_A="$(mktemp)"
  printf '  <END>\n<END>\n' >"$TMP_A"
  run_analyzer 1 logger <"$TMP_A"
  assert_exit_code_eq 0
  # NOTE: "[logger] " + two leading spaces + "<END>"
  local expected_A=$'[logger]   <END>\nPipeline shutdown complete'
  assert_stdout_equals "$expected_A"
  assert_stderr_empty

  # Case B: TAB BEFORE <END> should NOT close
  local TMP_B; TMP_B="$(mktemp)"
  printf '\t<END>\n<END>\n' >"$TMP_B"
  run_analyzer 1 logger <"$TMP_B"
  assert_exit_code_eq 0
  # NOTE: "[logger] " + <TAB> + "<END>"
  local expected_B=$'[logger] \t<END>\nPipeline shutdown complete'
  assert_stdout_equals "$expected_B"
  assert_stderr_empty

  # Case C: spaces/tabs AFTER <END> should NOT close
  local TMP_C; TMP_C="$(mktemp)"
  printf '<END> \t\n<END>\n' >"$TMP_C"   # one space then TAB after <END>
  run_analyzer 1 logger <"$TMP_C"
  assert_exit_code_eq 0
  # NOTE: "<END> " then <TAB> preserved in output
  local expected_C=$'[logger] <END> \t\nPipeline shutdown complete'
  assert_stdout_equals "$expected_C"
  assert_stderr_empty

  pass "END precision with leading/trailing whitespace validated"
}

# ---------- 5) Multiple occurrences of "<END>" inside a data line do NOT close ----------
test_end_inside_data_multiple_times() {

  run_analyzer 1 logger <<'EOF'
<END>inside<END>and<END>again
<END>
EOF
  assert_exit_code_eq 0
  local expected=$'[logger] <END>inside<END>and<END>again\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Inline '<END>' substrings do not terminate the pipeline"
}

# ---------- 6) No sentinel at all (EOF without <END>) must not terminate (expect timeout) ----------
test_no_sentinel_timeout() {

  local TMP_IN; TMP_IN="$(mktemp)"
  printf 'foo\nbar\n' >"$TMP_IN"     # NO <END> on purpose

  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  # Use a short, local timeout to assert non-termination without <END>
  timeout 5 ./output/analyzer 1 logger >"$OUT_FILE" 2>"$ERR_FILE" <"$TMP_IN"
  STATUS=$?

  if [ "$STATUS" -eq 124 ]; then
    # Timed out as expected: process did not exit without <END>
    pass "Process does not terminate without <END> (timeout as expected)"
  else
    echo "Expected a timeout (124), got: $STATUS"
    echo "STDOUT:"; cat "$OUT_FILE"
    echo "STDERR:"; cat "$ERR_FILE" >&2
    fail "Process should not terminate without <END>"
  fi
}



# ---------- Run ----------
echo ""
echo "Edge Cases Tests:"
echo ""
test_edge_empty_line
test_edge_whitespace_only
test_edge_max_length_1024
test_edge_no_newline_before_end
test_edge_crlf_compatibility
test_edge_end_only
test_edge_end_after_lines
test_edge_sentinel_precision
test_edge_queue_capacity1_load
test_edge_typewriter_backpressure
test_edge_single_char_transforms
test_edge_uppercaser_nonalpha
test_edge_punct_and_spaces_transforms
test_edge_many_lines_sequence
test_edge_long_chain_empty_and_single
test_edge_stdout_hygiene_again
test_end_precision
test_input_after_end_ignored
test_multiple_end_single_finalize
test_end_precision_with_spaces_tabs
test_end_inside_data_multiple_times
test_no_sentinel_timeout



echo ""
echo -e "${GREEN}All edge case tests passed.${NC}"
exit 0
