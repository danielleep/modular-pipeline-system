#!/bin/bash

# ===========================
# Colors
# ===========================
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# ===========================
# Variables
# ===========================
TEST_DIR="./"
MONITOR_SRC="../../plugins/sync/monitor.c"
INCLUDE_DIR="../../"
OUTPUT_DIR="./output_tests"

TESTS=("test_init" "test_destroy" "test_signal" "test_reset" "test_wait" "test_integration")

# ===========================
# Prepare output directory
# ===========================
mkdir -p "$OUTPUT_DIR"
rm -f "$OUTPUT_DIR"/*

echo -e "${GREEN}[BUILD] Starting compilation of monitor tests...${NC}"

# ===========================
# Compile all tests
# ===========================
for test in "${TESTS[@]}"; do
    echo -e "${GREEN}[BUILD] Compiling ${test}.c...${NC}"
    gcc "$TEST_DIR/${test}.c" "$MONITOR_SRC" \
        -I"$INCLUDE_DIR" -lpthread \
        -Wall -Wextra -Werror -Wno-unused-parameter \
        -o "$OUTPUT_DIR/$test"

    if [ $? -ne 0 ]; then
        echo -e "${RED}[ERROR] Compilation failed for $test. Stopping.${NC}"
        rm -rf "$OUTPUT_DIR"
        exit 1
    fi
done

echo -e "${GREEN}[BUILD SUCCESS] All tests compiled successfully.${NC}\n"

# ===========================
# Run all tests
# ===========================
for test in "${TESTS[@]}"; do
    echo -e "${GREEN}=== Running $test ===${NC}"
    "$OUTPUT_DIR/$test"
    echo -e "\n"
done

echo -e "${GREEN}✅ All monitor tests finished running.${NC}"

# ===========================
# Cleanup output directory
# ===========================
rm -rf "$OUTPUT_DIR"

