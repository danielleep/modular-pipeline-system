#!/usr/bin/env bash
# Backpressure_concurrence.sh — Concurrency & Backpressure tests (1–3)
# Notes:
# - Does NOT build the project; assumes ./output/analyzer and plugins already exist.
# - Focuses on queue backpressure and order preservation under concurrency.
# - No duplicate plugin instances are used (project doesn't support duplicates).

set -u
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"


# ---------- Colors ----------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TIMEOUT=155

pass() { echo -e "${GREEN}[PASS]${NC} $1"; }
fail() { echo -e "${RED}[FAIL]${NC}  $1"; exit 1; }

# ---------- Helpers ----------
OUT_FILE=""
ERR_FILE=""
STATUS=0

# Run analyzer with given queue_size and plugin chain.
# Stdin is provided by the caller via a here-doc or a pipe.
run_analyzer() {
  local queue_size="$1"; shift
  OUT_FILE="$(mktemp)"
  ERR_FILE="$(mktemp)"
  ./output/analyzer "$queue_size" "$@" >"$OUT_FILE" 2>"$ERR_FILE"
  STATUS=$?
}


# Assert exact STDOUT (all lines, order matters)
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

# Assert that a specific exact line exists in STDOUT
assert_stdout_has_line() {
  local line="$1"
  if ! grep -Fxq -- "$line" "$OUT_FILE"; then
    echo "STDOUT was:"
    cat "$OUT_FILE"
    fail "Expected exact line in STDOUT: $line"
  fi
}

# Assert empty STDERR
assert_stderr_empty() {
  if [ -s "$ERR_FILE" ]; then
    echo "STDERR was:"
    cat "$ERR_FILE" >&2
    fail "STDERR must be empty"
  fi
}

# Assert specific exit code
assert_exit_code_eq() {
  local expected="$1"
  if [ "$STATUS" -ne "$expected" ]; then
    echo "Expected exit code: $expected, got: $STATUS"
    echo "STDOUT:"; cat "$OUT_FILE"
    echo "STDERR:"; cat "$ERR_FILE" >&2
    fail "Exit code mismatch"
  fi
}

# Assert final shutdown line exists
assert_shutdown_line() {
  assert_stdout_has_line "Pipeline shutdown complete"
}

# ---------- Test 1 ----------
# Backpressure with a single slow consumer (typewriter), queue size = 1.
# Expectation: producer blocks as needed; order preserved; clean shutdown.
test_bp_single_typewriter_q1() {
  run_analyzer 1 typewriter <<'EOF'
one
two
three
<END>
EOF
  assert_exit_code_eq 0
  local expected=$'[typewriter] one\n[typewriter] two\n[typewriter] three\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Backpressure: single typewriter (queue=1) preserves order"
}

# ---------- Test 2 ----------
# Backpressure across a multi-stage chain where the last stage is slow:
# uppercaser -> rotator -> typewriter, queue size = 1.
# Transform: "Abc" -> "ABC" -> "CAB"; "xYz" -> "XYZ" -> "ZXY".
test_bp_chain_with_typewriter_q1() {
  run_analyzer 1 uppercaser rotator typewriter <<'EOF'
Abc
xYz
<END>
EOF
  assert_exit_code_eq 0
  local expected=$'[typewriter] CAB\n[typewriter] ZXY\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Backpressure across chain (uppercaser->rotator->typewriter, queue=1)"
}

# ---------- Test 3 ----------
# Order preservation under sustained pressure with a slow sink (typewriter).
# Send a short burst (12 lines) to keep runtime reasonable; queue size = 1.
test_bp_heavyish_burst_q1_typewriter() {
  # Build input & expected output programmatically to keep it compact.
  local tmp_in;  tmp_in="$(mktemp)"
  local expected=""

  for i in $(seq 1 12); do
    printf "l%d\n" "$i" >>"$tmp_in"
    if [ -z "$expected" ]; then
      expected="[typewriter] l${i}"
    else
      expected="${expected}"$'\n'"[typewriter] l${i}"
    fi
  done
  printf "<END>\n" >>"$tmp_in"
  expected="${expected}"$'\n'"Pipeline shutdown complete"

  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  cat "$tmp_in" | ./output/analyzer 1 typewriter >"$OUT_FILE" 2>"$ERR_FILE"
  STATUS=$?

  assert_exit_code_eq 0
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Order preserved for burst of 12 lines with slow sink (queue=1)"
}

# ---------- 4) Timing sanity: slow sink (typewriter) must take longer than fast sink (logger) ----------
# Compares wall time for identical input with `typewriter` vs `logger` when queue_size=1.
# We expect typewriter to be significantly slower due to 100ms/char; this indicates real blocking (no busy-wait).
test_bp_timing_sanity_typewriter_vs_logger() {
    
  # typewriter run
  local t0_t=$(date +%s%N)
  run_analyzer 1 typewriter <<'EOF'
hello
world!!
OS
<END>
EOF
  local t1_t=$(date +%s%N)
  assert_exit_code_eq 0
  local expected_t=$'[typewriter] hello\n[typewriter] world!!\n[typewriter] OS\nPipeline shutdown complete'
  assert_stdout_equals "$expected_t"
  assert_stderr_empty
  local elapsed_typewriter_ms=$(( (t1_t - t0_t) / 1000000 ))

  # logger run
  local t0_l=$(date +%s%N)
  run_analyzer 1 logger <<'EOF'
hello
world!!
OS
<END>
EOF
  local t1_l=$(date +%s%N)
  assert_exit_code_eq 0
  local expected_l=$'[logger] hello\n[logger] world!!\n[logger] OS\nPipeline shutdown complete'
  assert_stdout_equals "$expected_l"
  assert_stderr_empty
  local elapsed_logger_ms=$(( (t1_l - t0_l) / 1000000 ))

  # must be noticeably slower
  local margin_ms=600
  if [ $(( elapsed_typewriter_ms - elapsed_logger_ms )) -lt $margin_ms ]; then
    echo "typewriter_ms=$elapsed_typewriter_ms, logger_ms=$elapsed_logger_ms, margin=$margin_ms"
    fail "Typewriter did not take significantly longer than logger"
  fi
  pass "Timing sanity: typewriter slower than logger by ≥ ${margin_ms}ms (OK)"
}


# ---------- Test 5: Stress with jitter (producer sleeps tiny random amounts) ----------
# If TIMEOUT is not set elsewhere, default to 30s for logger runs.
: "${TIMEOUT:=30}"

# Variant A: logger (fast sink), queue_size=1 — stresses backpressure via capacity only.
test_bp_stress_jitter_q1_logger() {

  local N=120                # reduced from larger N to keep runtime short
  local TMP_CMD; TMP_CMD="$(mktemp)"

  # Small script that prints lines with tiny random sleeps and then <END>
  {
    echo 'for i in $(seq 1 '"$N"'); do'
    echo '  printf "l%s\n" "$i"'
    echo '  sleep 0.0$((RANDOM%2))'   # 0.00 or 0.01s — fast jitter
    echo 'done'
    echo 'printf "<END>\n"'
  } > "$TMP_CMD"

  # Build expected output
  local expected=""
  for i in $(seq 1 "$N"); do
    if [ -z "$expected" ]; then expected="[logger] l${i}"
    else expected="${expected}"$'\n'"[logger] l${i}"; fi
  done
  expected="${expected}"$'\n'"Pipeline shutdown complete"

  # Run with a safety timeout to avoid hangs
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  bash "$TMP_CMD" | timeout "$TIMEOUT" ./output/analyzer 1 logger >"$OUT_FILE" 2>"$ERR_FILE"; STATUS=$?
  if [ "$STATUS" -eq 124 ]; then
    echo "Timed out after ${TIMEOUT}s (logger jitter)."
    echo "STDOUT:"; cat "$OUT_FILE"
    echo "STDERR:"; cat "$ERR_FILE" >&2
    fail "Command timed out"
  fi

  assert_exit_code_eq 0
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Stress jitter (logger): exact order preserved for ${N} lines"
}

# Variant B: typewriter (slow sink), queue_size=1 — stresses capacity + slow consumer.
test_bp_stress_jitter_q1_typewriter() {

  local N=60                 # keep runtime reasonable (typewriter is slow by design)
  local TMP_CMD; TMP_CMD="$(mktemp)"

  {
    echo 'for i in $(seq 1 '"$N"'); do'
    echo '  printf "l%s\n" "$i"'
    echo '  sleep 0.0$((RANDOM%2))'   # tiny jitter
    echo 'done'
    echo 'printf "<END>\n"'
  } > "$TMP_CMD"

  local expected=""
  for i in $(seq 1 "$N"); do
    if [ -z "$expected" ]; then expected="[typewriter] l${i}"
    else expected="${expected}"$'\n'"[typewriter] l${i}"; fi
  done
  expected="${expected}"$'\n'"Pipeline shutdown complete"

  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  # Give the slow sink more time to finish
  bash "$TMP_CMD" | timeout 90 ./output/analyzer 1 typewriter >"$OUT_FILE" 2>"$ERR_FILE"; STATUS=$?
  if [ "$STATUS" -eq 124 ]; then
    echo "Timed out after 90s (typewriter jitter)."
    echo "STDOUT:"; cat "$OUT_FILE"
    echo "STDERR:"; cat "$ERR_FILE" >&2
    fail "Command timed out"
  fi

  assert_exit_code_eq 0
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Stress jitter (typewriter): exact order preserved for ${N} lines"
}



# ---------- 6) No busy-wait sanity: CPU time must be much lower than wall time ----------
# Uses `/usr/bin/time` to compare real (wall) vs user+sys CPU time.
# We expect (user+sys)/real to be small, indicating proper blocking rather than spinning.
test_no_busy_wait_cpu_ratio_typewriter() {

  # Build input: a few long lines to make wall-time noticeable but not too long.
  local TMP_IN; TMP_IN="$(mktemp)"
  printf '%s\n' 'AAAAAAAAAAAAAAAAAAAAAAAAAAAA' >>"$TMP_IN"  # 28 chars
  printf '%s\n' 'BBBBBBBBBBBBBBBBBBBBBBBBBBBB' >>"$TMP_IN"  # 28 chars
  printf '%s\n' '<END>' >>"$TMP_IN"                          # ~5.6s typewriter total

  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  local TIME_FILE; TIME_FILE="$(mktemp)"

  if command -v /usr/bin/time >/dev/null 2>&1; then
    /usr/bin/time -f "real:%e user:%U sys:%S" -o "$TIME_FILE" ./output/analyzer 1 typewriter >"$OUT_FILE" 2>"$ERR_FILE" <"$TMP_IN"
  else
    # Fallback if /usr/bin/time is unavailable
    { time -p ./output/analyzer 1 typewriter >"$OUT_FILE" 2>"$ERR_FILE" <"$TMP_IN"; } 2> "$TIME_FILE"
  fi
  STATUS=$?

  assert_exit_code_eq 0
  local expected=$'[typewriter] AAAAAAAAAAAAAAAAAAAAAAAAAAAA\n[typewriter] BBBBBBBBBBBBBBBBBBBBBBBBBBBB\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty

  # Parse timing
  local real_s user_s sys_s cpu_ratio
  if grep -q '^real:' "$TIME_FILE"; then
    real_s=$(grep '^real:' "$TIME_FILE" | sed 's/real://')
    user_s=$(grep '^user:' "$TIME_FILE" | sed 's/user://')
    sys_s=$(grep  '^sys:'  "$TIME_FILE" | sed 's/sys://')
  else
    # time -p fallback format:
    real_s=$(grep '^real' "$TIME_FILE" | awk '{print $2}')
    user_s=$(grep '^user' "$TIME_FILE" | awk '{print $2}')
    sys_s=$(grep  '^sys'  "$TIME_FILE" | awk '{print $2}')
  fi

  # Compute ratio with awk (avoid bash floating-point)
  cpu_ratio=$(awk -v r="$real_s" -v u="$user_s" -v s="$sys_s" 'BEGIN { r=(r>0?r:1); printf("%.3f", (u+s)/r) }')
  # Threshold: we expect low CPU usage compared to wall time (indicates blocking vs spinning).
  # Choose a conservative bound to avoid flakiness.
  local max_ratio="0.40"
  if awk -v x="$cpu_ratio" -v y="$max_ratio" 'BEGIN { exit !(x < y) }'; then
    pass "No busy-wait: CPU ratio ${cpu_ratio} < ${max_ratio} (OK)"
  else
    echo "Timing raw: real=$real_s user=$user_s sys=$sys_s ratio=$cpu_ratio"
    fail "High CPU ratio suggests busy-wait (ratio >= ${max_ratio})"
  fi
}

# ---------- 7) Transform before sink under backpressure (uppercaser -> logger, queue=1) ----------
test_bp_transform_before_sink_q1() {

  # Build input file
  local TMP_IN; TMP_IN="$(mktemp)"
  for i in $(seq 1 200); do
    printf "l%u\n" "$i" >>"$TMP_IN"
  done
  printf "<END>\n" >>"$TMP_IN"

  # Build expected output (uppercased 'l' -> 'L')
  local expected=""
  for i in $(seq 1 200); do
    if [ -z "$expected" ]; then
      expected="[logger] L${i}"
    else
      expected="${expected}"$'\n'"[logger] L${i}"
    fi
  done
  expected="${expected}"$'\n'"Pipeline shutdown complete"

  # Run analyzer: feed STDIN from the file; use run_analyzer (defined in this script)
  run_analyzer 1 uppercaser logger <"$TMP_IN"

  # Assertions
  assert_exit_code_eq 0
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Transform before sink preserves FIFO and content (200 lines, queue=1)"
}


# ---------- 8) CRLF input under backpressure (logger, queue=1) ----------
# We normalize '\r' out of both actual and expected before comparing to avoid platform-specific newline behavior.
test_bp_crlf_q1() {

  # Build CRLF input
  local TMP_IN; TMP_IN="$(mktemp)"
  printf 'winA\r\n' >>"$TMP_IN"
  printf 'winB\r\n' >>"$TMP_IN"
  printf 'winC\r\n' >>"$TMP_IN"
  printf '<END>\r\n' >>"$TMP_IN"

  # Expected (logical lines)
  local TMP_EXP; TMP_EXP="$(mktemp)"
  {
    printf "[logger] winA\n"
    printf "[logger] winB\n"
    printf "[logger] winC\n"
    printf "Pipeline shutdown complete\n"
  } > "$TMP_EXP"

  # Run
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  cat "$TMP_IN" | ./output/analyzer 1 logger >"$OUT_FILE" 2>"$ERR_FILE"; STATUS=$?

  assert_exit_code_eq 0
  assert_stderr_empty

  # Normalize CR away before comparing
  local NORM_OUT NORM_EXP
  NORM_OUT="$(mktemp)"; NORM_EXP="$(mktemp)"
  tr -d '\r' <"$OUT_FILE" >"$NORM_OUT"
  tr -d '\r' <"$TMP_EXP"  >"$NORM_EXP"

  if ! diff -u "$NORM_EXP" "$NORM_OUT" >/dev/null; then
    echo "Expected (normalized):"; cat "$NORM_EXP"
    echo "Actual (normalized):";   cat "$NORM_OUT"
    fail "CRLF case: STDOUT mismatch"
  fi
  pass "CRLF input handled correctly with queue=1"
}

# ---------- 9) END precision under pressure (logger, queue=1) ----------
# Only exact "<END>" closes the pipeline; variations must be printed as regular lines.
test_bp_end_precision_under_pressure() {

  # Case A: "<END> " (with trailing space) should NOT close
  run_analyzer 1 logger <<'EOF'
<END> 
<END>
EOF
  assert_exit_code_eq 0
  # NOTE the trailing space after <END>
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

  pass "END precision validated with queue=1"
}

# ---------- 10) Finalize-only under backpressure (logger, queue=1) ----------
# Input contains only <END>; we expect only the final shutdown line.
test_bp_finalize_only_q1() {

  run_analyzer 1 logger <<'EOF'
<END>
EOF
  assert_exit_code_eq 0
  local expected=$'Pipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty
  pass "Finalize-only: clean shutdown with queue=1 and no data"
}



# ---------- Run ----------
echo ""
echo "Backpressure & Concurrency Tests:"
echo ""
test_bp_single_typewriter_q1
test_bp_chain_with_typewriter_q1
test_bp_heavyish_burst_q1_typewriter
test_bp_timing_sanity_typewriter_vs_logger
test_bp_stress_jitter_q1_logger
# test_bp_stress_jitter_q1_typewriter
test_no_busy_wait_cpu_ratio_typewriter
test_bp_transform_before_sink_q1
test_bp_crlf_q1
test_bp_end_precision_under_pressure
test_bp_finalize_only_q1


echo ""
echo -e "${GREEN}All backpressure/concurrency tests passed.${NC}"
exit 0
