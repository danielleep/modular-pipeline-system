  #!/usr/bin/env bash
  set -euo pipefail

  # --- bootstrap ---
  SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
  REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
  cd "$REPO_ROOT"
  # --------------------------------

  RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
  fail(){ echo -e "${RED}[FAIL]${NC} $1"; exit 1; }
  pass(){ echo -e "${GREEN}[PASS]${NC} $1"; }

  echo "Monitor Tests:"
  echo ""
  (
    cd tests/monitor
    ./build_test.sh
  ) || fail "Monitor tests failed"
  echo ""
  echo "Consumer Producer Tests:"
  (
    cd tests/consumer\ producer
    ./build_test.sh
  ) || fail "Consumer Producer tests failed"
  echo "Plugin Common Tests:"
  (
    cd tests/plugin_common
    ./build_tests.sh
  ) || fail "Plugin Common tests failed"
  echo "Plugins Tests:"
  (
    cd tests/plugin_tests
    ./build_tests.sh
  ) || fail "Plugins tests failed"