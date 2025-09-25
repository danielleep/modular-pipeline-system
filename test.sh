#!/usr/bin/env bash
set -euo pipefail
IFS=$'\n\t'

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

  # Build
  echo ""
  echo "building the project..."
  echo ""
  if ./build.sh; then
    echo  "Build succeeded!"
    echo ""
  else
    echo ""
    echo  "Build failed!"
    exit 1
    echo ""

  fi

  # # Smoke run (valid args, should not crash)
  # OUT_FILE="$(mktemp)"; ERR_FILE="$(mktemp)"
  # echo "<END>" | ./output/analyzer 5 logger >"$OUT_FILE" 2>"$ERR_FILE" || fail "Analyzer crashed on smoke run"
  # assert_shutdown_line
  # assert_stderr_empty
  # pass "build & smoke"

  # ----- RUN -----

chmod +x ./script_test/insiders_tests.sh ./script_test/happy_path.sh ./script_test/edge_tests.sh ./script_test/Backpressure_concurrence.sh ./script_test/output_hygiene_tests.sh ./script_test/stress_robustness.sh ./script_test/negative.sh

  echo ""
./script_test/insiders_tests.sh || fail "Insiders tests failed"
./script_test/happy_path.sh || fail "Happy path tests failed"
./script_test/edge_tests.sh || fail "Edge tests failed"
./script_test/negative.sh || fail "Negative tests failed"
./script_test/Backpressure_concurrence.sh  || fail "Backpressure tests failed"
./script_test/output_hygiene_tests.sh || fail "Output hygiene tests failed"
./script_test/stress_robustness.sh || fail "Stress & Robustness tests failed"
