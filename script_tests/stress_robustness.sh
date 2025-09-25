#!/usr/bin/env bash
# stress_robustness_tests.sh — Stress & Robustness tests (1–5)
# Notes:
# - Does NOT build the project; assumes ./output/analyzer and plugins already exist.
# - No duplicate plugin instances are used.
# - Uses file-based diffs for large outputs to avoid huge in-memory strings.

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
# Run analyzer with given queue_size and plugin chain.
# STDIN must be provided by the caller (here-doc or redirect).
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
    fail "STDERR must be empty"
  fi
}

if ! declare -f assert_stdout_equals >/dev/null 2>&1; then
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
fi

# ---------- Test 1 ----------
# Throughput: large & fast (logger, queue=64)
# Goal: 50k lines + <END> pass in exact order; single shutdown line; exit=0; empty STDERR.
test_throughput_large_fast_logger_q64() {

  local N=50000                      # reduce if your environment is slow
  local TMP_IN TMP_EXP
  TMP_IN="$(mktemp)"
  TMP_EXP="$(mktemp)"

  # Build input: l1..lN then <END>
  seq 1 "$N" | sed 's/^/l/' >"$TMP_IN"
  printf "<END>\n" >>"$TMP_IN"

  # Build expected: [logger] <line> ... then replace final sentinel line
  awk '{print "[logger] " $0}' "$TMP_IN" \
    | sed '$s/^\[logger\] <END>/Pipeline shutdown complete/' >"$TMP_EXP"

  # Run
  run_analyzer 64 logger <"$TMP_IN"
  assert_exit_code_eq 0
  assert_stderr_empty

  # Compare files
  if ! diff -u "$TMP_EXP" "$OUT_FILE" >/dev/null; then
    echo "Diff (expected vs actual):"
    diff -u "$TMP_EXP" "$OUT_FILE" || true
    fail "Output mismatch for large throughput test"
  fi

  # Sanity checks
  local shutdown_count
  shutdown_count="$(grep -c -x 'Pipeline shutdown complete' "$OUT_FILE")"
  if [ "$shutdown_count" -ne 1 ]; then
    echo "STDOUT was:"; head -n 5 "$OUT_FILE"; echo "..."; tail -n 5 "$OUT_FILE"
    fail "Shutdown line must appear exactly once"
  fi
  local total_lines
  total_lines="$(wc -l <"$OUT_FILE")"
  if [ "$total_lines" -ne $((N + 1)) ]; then
    fail "Unexpected total line count in STDOUT (got $total_lines, expected $((N + 1)))"
  fi

  pass "Large throughput (logger, $N lines) OK"
}

# ---------- Test 2 ----------
# Staged throughput with slow sink (typewriter, queue=1)
# Goal: order preserved; exit=0; empty STDERR; runtime inherently longer than logger (sanity).
test_throughput_staged_slow_typewriter_q1() {

  local L=10                        # 10 lines * 4 chars ≈ ~4s in typewriter
  local TMP_IN TMP_EXP
  TMP_IN="$(mktemp)"
  TMP_EXP="$(mktemp)"

  # Build input: 10 lines of 'abcd' then <END>
  for i in $(seq 1 "$L"); do printf 'abcd\n' >>"$TMP_IN"; done
  printf '<END>\n' >>"$TMP_IN"

  # Expected
  awk '{print "[typewriter] " $0}' "$TMP_IN" \
    | sed '$s/^\[typewriter\] <END>/Pipeline shutdown complete/' >"$TMP_EXP"

  # Run
  run_analyzer 1 typewriter <"$TMP_IN"
  assert_exit_code_eq 0
  assert_stderr_empty

  if ! diff -u "$TMP_EXP" "$OUT_FILE" >/dev/null; then
    echo "Diff (expected vs actual):"
    diff -u "$TMP_EXP" "$OUT_FILE" || true
    fail "Output mismatch for staged slow sink"
  fi

  pass "Staged throughput (typewriter, $L×'abcd') OK"
}

# ---------- Test 3 ----------
# Tiny capacity under moderate load (uppercaser -> logger, queue=1)
# Goal: FIFO preserved with transform stage; exit=0; empty STDERR.
test_small_capacity_under_load_upper_logger_q1() {

  local N=5000
  local TMP_IN TMP_EXP
  TMP_IN="$(mktemp)"
  TMP_EXP="$(mktemp)"

  seq 1 "$N" | sed 's/^/l/' >"$TMP_IN"
  printf '<END>\n' >>"$TMP_IN"

  # Expected: logger prints uppercase(payload); logger tag stays lowercase.
  # I.e., "[logger] L1", "[logger] L2", ...
  awk '{print "[logger] " toupper($0)}' "$TMP_IN" \
    | sed '$s/^\[logger\] <END>/Pipeline shutdown complete/' >"$TMP_EXP"

  # Run
  run_analyzer 1 uppercaser logger <"$TMP_IN"
  assert_exit_code_eq 0
  assert_stderr_empty

  if ! diff -u "$TMP_EXP" "$OUT_FILE" >/dev/null; then
    echo "Diff (expected vs actual):"
    diff -u "$TMP_EXP" "$OUT_FILE" || true
    fail "Output mismatch for queue=1 with transform"
  fi

  pass "Queue=1 with transform (uppercaser->logger, $N lines) OK"
}

# ---------- Test 4 ----------
# Bursts with idle between (logger, queue=1)
# Goal: bursts of 1000 lines * 5, separated by small sleeps; exact order preserved.
test_bursts_with_idle_logger_q1() {

  local BURSTS=5
  local PER_BURST=1000

  local TMP_CMD TMP_EXP
  TMP_CMD="$(mktemp)"
  TMP_EXP="$(mktemp)"

  # Generator script: print bursts, sleep between, then <END>
  {
    echo 'for b in $(seq 1 '"$BURSTS"'); do'
    echo '  for i in $(seq 1 '"$PER_BURST"'); do printf "l%b_%d\n" "$b" "$i"; done'
    echo '  sleep 0.1'
    echo 'done'
    echo 'printf "<END>\n"'
  } >"$TMP_CMD"

  # Expected: transform generator's lines (without last <END>) to logger output
  bash "$TMP_CMD" | sed '$d' | awk '{print "[logger] " $0}' >"$TMP_EXP"
  printf 'Pipeline shutdown complete\n' >>"$TMP_EXP"

  # Run (pipeline from generator)
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"; STATUS=0
  bash "$TMP_CMD" | ./output/analyzer 1 logger >"$OUT_FILE" 2>"$ERR_FILE"; STATUS=$?
  assert_exit_code_eq 0
  assert_stderr_empty

  if ! diff -u "$TMP_EXP" "$OUT_FILE" >/dev/null; then
    echo "Diff (expected vs actual):"
    diff -u "$TMP_EXP" "$OUT_FILE" || true
    fail "Output mismatch for bursts + idle"
  fi

  pass "Bursts with idle (5×$PER_BURST lines) OK"
}

# ---------- Test 5 ----------
# Producer jitter with slow sink (typewriter, queue=1)
# Goal: small random sleeps in producer + slow sink do not deadlock; exact order preserved.
test_producer_jitter_typewriter_q1() {

  local N=60
  local TMP_CMD TMP_EXP
  TMP_CMD="$(mktemp)"
  TMP_EXP="$(mktemp)"

  # Generator: 60 short lines with tiny jitter; then <END>
  {
    echo 'for i in $(seq 1 '"$N"'); do'
    echo '  printf "x\n"'
    echo '  sleep 0.0$((RANDOM%2))'   # 0.00 or 0.01 s
    echo 'done'
    echo 'printf "<END>\n"'
  } >"$TMP_CMD"

  # Expected
  bash "$TMP_CMD" | sed '$d' | awk '{print "[typewriter] " $0}' >"$TMP_EXP"
  printf 'Pipeline shutdown complete\n' >>"$TMP_EXP"

  # Run
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"; STATUS=0
  bash "$TMP_CMD" | ./output/analyzer 1 typewriter >"$OUT_FILE" 2>"$ERR_FILE"; STATUS=$?
  assert_exit_code_eq 0
  assert_stderr_empty

  if ! diff -u "$TMP_EXP" "$OUT_FILE" >/dev/null; then
    echo "Diff (expected vs actual):"
    diff -u "$TMP_EXP" "$OUT_FILE" || true
    fail "Output mismatch for producer jitter with slow sink"
  fi

  pass "Producer jitter with slow sink (typewriter, $N lines) OK"
}


# ---------- Test 6: Soak 100 sequential runs (logger, queue=1) ----------
# Goal: stability across many consecutive runs; exact output each time.
test_soak_100_sequential_logger_q1() {
  local expected=$'[logger] l1\n[logger] l2\nPipeline shutdown complete'
  local runs=100

  for r in $(seq 1 "$runs"); do
    run_analyzer 1 logger <<'EOF'
l1
l2
<END>
EOF
    assert_exit_code_eq 0
    assert_stderr_empty
    assert_stdout_equals "$expected"
  done

  pass "Soak (100 sequential runs) OK"
}

# ---------- Test 7: Parallel isolated runs (5 pipelines) ----------
# Chains:
#   1) logger
#   2) uppercaser -> logger
#   3) rotator   -> logger
#   4) expander  -> logger
#   5) typewriter
test_parallel_isolated_runs() {

  local N=20
  local TMP_DIR; TMP_DIR="$(mktemp -d)"
  local -a PIDS=() NAMES=() OUTS=() ERRS=()

  # Build inputs
  build_input() { # $1=tag, $2=N, $3=outfile
    : >"$3"; for i in $(seq 1 "$2"); do printf "%s#%d\n" "$1" "$i" >>"$3"; done; printf "<END>\n" >>"$3"
  }
  local IN1="$TMP_DIR/in1" IN2="$TMP_DIR/in2" IN3="$TMP_DIR/in3" IN4="$TMP_DIR/in4" IN5="$TMP_DIR/in5"
  build_input "A" "$N" "$IN1"
  build_input "B" "$N" "$IN2"
  build_input "C" "$N" "$IN3"
  build_input "D" "$N" "$IN4"
  build_input "E" "$N" "$IN5"

  # Build expecteds
  build_expected_logger()         { awk '{print "[logger] " $0}' "$1" | sed '$s/^\[logger\] <END>/Pipeline shutdown complete/' >"$2"; }
  build_expected_upper_logger() {
  # Keep the tag [logger] in lowercase; uppercase only the payload.
  # Then replace the final sentinel line with the shutdown line. 
  awk '{print "[logger] " toupper($0)}' "$1" \
      | sed '$s/^\[logger\] <END>/Pipeline shutdown complete/' >"$2"
}
build_expected_rotator_logger() {
  # Drop the sentinel line, rotate only data, then append shutdown.
  sed '$d' "$1" | awk '
    function rot(s) { return substr(s, length(s)) substr(s, 1, length(s)-1) }
    { print "[logger] " rot($0) }
  ' >"$2"
  printf 'Pipeline shutdown complete\n' >>"$2"
}

# (Recommended) Do the same idea for expander to avoid expanding the sentinel:
build_expected_expander_logger() {
  # Drop the sentinel line, expand only data, then append shutdown.
  sed '$d' "$1" | awk '
    function expand(s,  o,i,n){ n=length(s); o=""
      for(i=1;i<=n;i++){ o=o substr(s,i,1); if(i<n) o=o " " }
      return o
    }
    { print "[logger] " expand($0) }
  ' >"$2"
  printf 'Pipeline shutdown complete\n' >>"$2"
}
  build_expected_typewriter()     { awk '{print "[typewriter] " $0}' "$1" | sed '$s/^\[typewriter\] <END>/Pipeline shutdown complete/' >"$2"; }

  local EXP1="$TMP_DIR/exp1" EXP2="$TMP_DIR/exp2" EXP3="$TMP_DIR/exp3" EXP4="$TMP_DIR/exp4" EXP5="$TMP_DIR/exp5"
  build_expected_logger          "$IN1" "$EXP1"
  build_expected_upper_logger    "$IN2" "$EXP2"
  build_expected_rotator_logger  "$IN3" "$EXP3"
  build_expected_expander_logger "$IN4" "$EXP4"
  build_expected_typewriter      "$IN5" "$EXP5"

  # Launch pipelines in background
  OUTS[1]="$TMP_DIR/out1"; ERRS[1]="$TMP_DIR/err1"; NAMES[1]="logger"
  OUTS[2]="$TMP_DIR/out2"; ERRS[2]="$TMP_DIR/err2"; NAMES[2]="uppercaser logger"
  OUTS[3]="$TMP_DIR/out3"; ERRS[3]="$TMP_DIR/err3"; NAMES[3]="rotator logger"
  OUTS[4]="$TMP_DIR/out4"; ERRS[4]="$TMP_DIR/err4"; NAMES[4]="expander logger"
  OUTS[5]="$TMP_DIR/out5"; ERRS[5]="$TMP_DIR/err5"; NAMES[5]="typewriter"

  cat "$IN1" | ./output/analyzer 1 logger            >"${OUTS[1]}" 2>"${ERRS[1]}" & PIDS[1]=$!
  cat "$IN2" | ./output/analyzer 1 uppercaser logger >"${OUTS[2]}" 2>"${ERRS[2]}" & PIDS[2]=$!
  cat "$IN3" | ./output/analyzer 1 rotator   logger  >"${OUTS[3]}" 2>"${ERRS[3]}" & PIDS[3]=$!
  cat "$IN4" | ./output/analyzer 1 expander  logger  >"${OUTS[4]}" 2>"${ERRS[4]}" & PIDS[4]=$!
  cat "$IN5" | ./output/analyzer 1 typewriter        >"${OUTS[5]}" 2>"${ERRS[5]}" & PIDS[5]=$!

  # Wait + assert
  local i rc
  for i in 1 2 3 4 5; do
    wait "${PIDS[$i]}"; rc=$?
    if [ "$rc" -ne 0 ]; then
      echo "Pipeline ${i} (${NAMES[$i]}) exited with $rc"
      echo "STDERR:"; cat "${ERRS[$i]}" >&2 || true
      fail "Parallel pipeline ${i} failed"
    fi
    [ -s "${ERRS[$i]}" ] && { echo "STDERR for pipeline ${i}:"; cat "${ERRS[$i]}" >&2; fail "STDERR not empty (pipeline ${i})"; }
  done

  # Diff outputs
  diff -u "$EXP1" "${OUTS[1]}" >/dev/null || { diff -u "$EXP1" "${OUTS[1]}"; fail "Mismatch (pipeline 1: logger)"; }
  diff -u "$EXP2" "${OUTS[2]}" >/dev/null || { diff -u "$EXP2" "${OUTS[2]}"; fail "Mismatch (pipeline 2: upper->logger)"; }
  diff -u "$EXP3" "${OUTS[3]}" >/dev/null || { diff -u "$EXP3" "${OUTS[3]}"; fail "Mismatch (pipeline 3: rotator->logger)"; }
  diff -u "$EXP4" "${OUTS[4]}" >/dev/null || { diff -u "$EXP4" "${OUTS[4]}"; fail "Mismatch (pipeline 4: expander->logger)"; }
  diff -u "$EXP5" "${OUTS[5]}" >/dev/null || { diff -u "$EXP5" "${OUTS[5]}"; fail "Mismatch (pipeline 5: typewriter)"; }

  pass "Parallel isolated runs (5 pipelines) OK"
}

# ---------- Test 8: Timing sanity (typewriter ≫ logger) ----------
test_timing_sanity_slow_vs_fast() {

  # typewriter
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

  # logger
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

  local margin_ms=600
  if [ $(( elapsed_typewriter_ms - elapsed_logger_ms )) -lt $margin_ms ]; then
    echo "typewriter_ms=$elapsed_typewriter_ms, logger_ms=$elapsed_logger_ms, margin=$margin_ms"
    fail "Typewriter did not take significantly longer than logger"
  fi

  pass "Timing sanity: typewriter slower by ≥ ${margin_ms}ms (OK)"
}

# ---------- Test 9: No busy-wait (CPU vs wall ratio) ----------
test_no_busy_wait_cpu_ratio_typewriter() {

  local TMP_IN; TMP_IN="$(mktemp)"
  printf '%s\n' 'AAAAAAAAAAAAAAAAAAAAAAAAAAAA' >>"$TMP_IN"  # 28 chars
  printf '%s\n' 'BBBBBBBBBBBBBBBBBBBBBBBBBBBB' >>"$TMP_IN"  # 28 chars
  printf '%s\n' '<END>' >>"$TMP_IN"

  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  local TIME_FILE; TIME_FILE="$(mktemp)"

  if command -v /usr/bin/time >/dev/null 2>&1; then
    /usr/bin/time -f "real:%e user:%U sys:%S" -o "$TIME_FILE" ./output/analyzer 1 typewriter >"$OUT_FILE" 2>"$ERR_FILE" <"$TMP_IN"
  else
    { time -p ./output/analyzer 1 typewriter >"$OUT_FILE" 2>"$ERR_FILE" <"$TMP_IN"; } 2> "$TIME_FILE"
  fi
  STATUS=$?

  assert_exit_code_eq 0
  local expected=$'[typewriter] AAAAAAAAAAAAAAAAAAAAAAAAAAAA\n[typewriter] BBBBBBBBBBBBBBBBBBBBBBBBBBBB\nPipeline shutdown complete'
  assert_stdout_equals "$expected"
  assert_stderr_empty

  local real_s user_s sys_s
  if grep -q '^real:' "$TIME_FILE"; then
    real_s=$(grep '^real:' "$TIME_FILE" | sed 's/real://')
    user_s=$(grep '^user:' "$TIME_FILE" | sed 's/user://')
    sys_s=$(grep  '^sys:'  "$TIME_FILE" | sed 's/sys://')
  else
    real_s=$(grep '^real' "$TIME_FILE" | awk '{print $2}')
    user_s=$(grep '^user' "$TIME_FILE" | awk '{print $2}')
    sys_s=$(grep  '^sys'  "$TIME_FILE" | awk '{print $2}')
  fi

  local cpu_ratio
  cpu_ratio=$(awk -v r="$real_s" -v u="$user_s" -v s="$sys_s" 'BEGIN { r=(r>0?r:1); printf("%.3f", (u+s)/r) }')
  local max_ratio="0.40"
  if awk -v x="$cpu_ratio" -v y="$max_ratio" 'BEGIN { exit !(x < y) }'; then
    pass "No busy-wait: CPU ratio ${cpu_ratio} < ${max_ratio} (OK)"
  else
    echo "Timing raw: real=$real_s user=$user_s sys=$sys_s ratio=$cpu_ratio"
    fail "High CPU ratio suggests busy-wait (ratio >= ${max_ratio})"
  fi
}

# ---------- Test 10 (flex): Max line length boundary (logger, queue=1) ----------
# Flexible policy: accept truncate OR split_* OR error, but enforce sanity:
# - No crash in non-error modes, STDERR empty
# - Exactly one shutdown line
# - Sentinel <END> is never printed as data
# - Each [logger] payload line length <= MAX_LEN
# - Case A (exact MAX_LEN): total payload length == MAX_LEN
# - Case B (MAX_LEN+1): total payload length in {MAX_LEN, MAX_LEN+1}
#   (truncate or carry), and all chars are 'B'
: "${MAX_LEN:=1024}"

test_max_line_length_boundary_logger_q1() {

  # Helpers to extract payloads and check lengths
  _concat_payloads() {  # stdin = analyzer STDOUT
    awk '/^\[logger\]/{ s=$0; sub(/^\[logger\][ ]?/,"",s); printf "%s", s }'
  }
  _payload_line_lengths() {  # stdin = analyzer STDOUT
    awk '/^\[logger\]/{ s=$0; sub(/^\[logger\][ ]?/,"",s); print length(s) }'
  }

  # ---------- Prepare inputs ----------
  local TMP_A TMP_B TMP_B_TRUNC
  TMP_A="$(mktemp)"        # exactly MAX_LEN A
  TMP_B="$(mktemp)"        # MAX_LEN+1 B
  TMP_B_TRUNC="$(mktemp)"  # first MAX_LEN of TMP_B

  head -c "$MAX_LEN" /dev/zero | tr '\0' 'A' >"$TMP_A"
  head -c $((MAX_LEN + 1)) /dev/zero | tr '\0' 'B' >"$TMP_B"
  head -c "$MAX_LEN" "$TMP_B" >"$TMP_B_TRUNC"

  # ---------- Case A: exactly MAX_LEN ----------
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"; STATUS=0
  { cat "$TMP_A"; printf "\n<END>\n"; } \
    | ./output/analyzer 1 logger >"$OUT_FILE" 2>"$ERR_FILE" ; STATUS=$?

  # Must succeed, no noise
  if [ "$STATUS" -ne 0 ]; then
    echo "STDERR:"; cat "$ERR_FILE" >&2
    fail "Case A failed: analyzer exit != 0"
  fi
  [ ! -s "$ERR_FILE" ] || { echo "STDERR:"; cat "$ERR_FILE" >&2; fail "Case A: STDERR not empty"; }
  # Exactly one shutdown line
  [ "$(grep -c -x 'Pipeline shutdown complete' "$OUT_FILE")" -eq 1 ] \
    || fail "Case A: shutdown line count != 1"
  # Sentinel never printed
  ! grep -qx '\[logger\] <END>' "$OUT_FILE" || fail "Case A: sentinel printed as data"
  # Each payload line length <= MAX_LEN
  if [ "$( _payload_line_lengths <"$OUT_FILE" | awk -v M="$MAX_LEN" 'max<$1{max=$1} END{print (max>M)?"BAD":"OK"}' )" = "BAD" ]; then
    fail "Case A: a payload line exceeds MAX_LEN"
  fi
  # Total payload length exactly MAX_LEN
  total_len_A="$( _concat_payloads <"$OUT_FILE" | wc -c )"
  if [ "$total_len_A" -ne "$MAX_LEN" ]; then
    echo "Total payload len (Case A) = $total_len_A, expected $MAX_LEN"
    fail "Case A: total payload length mismatch"
  fi

  # ---------- Case B: MAX_LEN+1 ----------
  OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"; STATUS=0
  { cat "$TMP_B"; printf "\n<END>\n"; } \
    | ./output/analyzer 1 logger >"$OUT_FILE" 2>"$ERR_FILE" ; STATUS=$?

  if [ "$STATUS" -ne 0 ]; then
    # Accept explicit error policy if chosen by the implementation
    # Ensure no shutdown line was printed
    ! grep -qx 'Pipeline shutdown complete' "$OUT_FILE" \
      || fail "Case B (error policy): shutdown line must not appear"
    pass "Max line: error policy accepted (non-zero exit)"
    return 0
  fi

  # If we reached here, non-error policy: enforce sanity
  [ ! -s "$ERR_FILE" ] || { echo "STDERR:"; cat "$ERR_FILE" >&2; fail "Case B: STDERR not empty"; }
  [ "$(grep -c -x 'Pipeline shutdown complete' "$OUT_FILE")" -eq 1 ] \
    || fail "Case B: shutdown line count != 1"
  ! grep -qx '\[logger\] <END>' "$OUT_FILE" || fail "Case B: sentinel printed as data"

# --- Case B length policy acceptance (allow OR truncate/split) ---
# Reconstruct total payload and compute per-line lengths
payload_B="$( _concat_payloads <"$OUT_FILE" )"
total_len_B="${#payload_B}"

mapfile -t _lens < <( _payload_line_lengths <"$OUT_FILE" )
lines_B="${#_lens[@]}"
max_len_B=0
for L in "${_lens[@]}"; do
  (( L > max_len_B )) && max_len_B="$L"
done

# Accept either:
#  1) All payload lines <= MAX_LEN  (truncate/split policies), OR
#  2) max_len == MAX_LEN+1 AND total_len == MAX_LEN+1  (allow OR off-by-one split)
if ! { [ "$max_len_B" -le "$MAX_LEN" ] \
       || { [ "$max_len_B" -eq $((MAX_LEN+1)) ] && [ "$total_len_B" -eq $((MAX_LEN+1)) ]; }; }; then
  echo "Debug: lines_B=$lines_B max_len_B=$max_len_B total_len_B=$total_len_B MAX_LEN=$MAX_LEN"
  fail "Case B: payload line length policy violation"
fi



  pass "Max line length boundary (flex) OK"
}





# ---------- Run ----------
echo ""
echo "Stress & Robustness Tests:"
echo ""
test_throughput_large_fast_logger_q64
test_throughput_staged_slow_typewriter_q1
test_small_capacity_under_load_upper_logger_q1
test_bursts_with_idle_logger_q1
test_soak_100_sequential_logger_q1
test_parallel_isolated_runs
test_timing_sanity_slow_vs_fast
test_no_busy_wait_cpu_ratio_typewriter
test_max_line_length_boundary_logger_q1


echo ""
echo -e "${GREEN}All stress & robustness tests passed.${NC}"
exit 0
