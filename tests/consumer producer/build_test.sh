#!/bin/bash

# צבעים
GREEN="\033[0;32m"
RED="\033[0;31m"
NC="\033[0m"


# פונקציה עם אינדיקציה צבעונית
compile_and_report() {
  CMD=$1
  DESC=$2

  echo "Compiling $DESC..."
  eval "$CMD"
  if [ $? -eq 0 ]; then
    echo -e "[${GREEN}PASS${NC}] $DESC compiled successfully"
  else
    echo -e "[${RED}FAIL${NC}] $DESC failed to compile"
    exit 1
  fi
}

echo ""
echo "Compiling unit tests..."

compile_and_report "gcc -o test_init test_init.c ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c -lpthread" "test_init"
compile_and_report "gcc -o test_destroy test_destroy.c ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c -lpthread" "test_destroy"
compile_and_report "gcc -o test_put test_put.c ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c -lpthread" "test_put"
compile_and_report "gcc -o test_get test_get.c ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c -lpthread" "test_get"
compile_and_report "gcc -o test_signal_finished test_signal_finished.c ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c -lpthread" "test_signal_finished"
compile_and_report "gcc -o test_wait_finished test_wait_finished.c ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c -lpthread" "test_wait_finished"

echo ""
echo "Compiling integration test..."
compile_and_report "gcc -o test_integration test_integration.c ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c -lpthread" "test_integration"
compile_and_report "gcc -std=c11 -O2 -Wall -Wextra -I../../plugins/sync   -o ../../output/test_integration2   test_integration2.c   ../../plugins/sync/consumer_producer.c   ../../plugins/sync/monitor.c   -lpthread"
compile_and_report "gcc -std=c11 -O2 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -I../../plugins/sync   -o ../../output/extra_integration_tests   extra_integration_tests.c   ../../plugins/sync/consumer_producer.c   ../../plugins/sync/monitor.c   -lpthread"


echo ""
echo "Running unit tests..."
./test_init
echo ""
./test_destroy
echo ""
./test_put
echo ""
./test_get
echo ""
./test_signal_finished
echo ""
./test_wait_finished
echo ""

echo "Running integration tests 1..."
echo ""
./test_integration
echo ""
echo "Running integration tests 2..."
echo ""
../../output/test_integration2
echo ""
echo "Running extra integration tests ..."
echo ""
../../output/extra_integration_tests
echo ""