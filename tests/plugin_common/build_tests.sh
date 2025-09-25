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
echo ""

compile_and_report "gcc -std=c11 -O2 -g -Wall -Wextra -pthread \
  -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L \
  plugin_common_unit_tests.c \
  ../../plugins/plugin_common.c ../../plugins/logger.c \
  ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c \
  -I../.. -I../../plugins \
  -o plugin_common_unit_tests"

echo ""
echo "Compiling integration test..."
echo ""

compile_and_report "gcc -std=c11 -O2 -g -Wall -Wextra -pthread \
  -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L \
  plugin_common_integration_tests.c \
  ../../plugins/plugin_common.c \
  ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c \
  -I../.. -I../../plugins \
  -o plugin_common_integration_tests"

echo ""
echo "Compiling  extra integration test..."
echo ""

compile_and_report "gcc -std=c11 -O2 -g -Wall -Wextra -pthread \
  -D_XOPEN_SOURCE=700 -D_POSIX_C_SOURCE=200809L \
  extra_tests_plugin_common.c \
  ../../plugins/plugin_common.c \
  ../../plugins/sync/consumer_producer.c ../../plugins/sync/monitor.c \
  -I../.. -I../../plugins \
  -o extra_tests_plugin_common"



echo ""
echo "Running unit tests..."
echo ""
./plugin_common_unit_tests
echo ""

echo "Running integration tests ..."
echo ""
./plugin_common_integration_tests
echo ""

echo "Running extra integration tests ..."
echo ""
./extra_tests_plugin_common
echo ""