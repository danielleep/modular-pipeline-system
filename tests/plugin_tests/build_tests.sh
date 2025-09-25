#!/bin/bash

GREEN="\033[0;32m"
RED="\033[0;31m"
NC="\033[0m"


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

compile_and_report "gcc -std=c11 -Wall -Wextra -O2   -I../../plugins   -o ../../output/logger_tests   logger_tests.c"
compile_and_report "gcc -std=c11 -Wall -Wextra -O2 -I../../plugins   -o ../../output/uppercaser_tests   uppercaser_tests.c"
compile_and_report "gcc -std=c11 -Wall -Wextra -O2 -I../../plugins   -o ../../output/flipper_tests   flipper_tests.c"
compile_and_report "gcc -std=c11 -Wall -Wextra -O2 -I../../plugins   -o ../../output/rotator_tests   rotator_tests.c"
compile_and_report "gcc -std=c11 -Wall -Wextra -O2 -I../../plugins   -o ../../output/expander_tests  expander_tests.c"
compile_and_report "gcc -std=c11 -Wall -Wextra -O2 -I../../plugins   -o ../../output/typewriter_tests typewriter_tests.c"



echo ""
echo "Running unit tests..."
echo ""
../../output/logger_tests
echo ""
../../output/uppercaser_tests
echo ""
../../output/flipper_tests
echo ""
../../output/rotator_tests
echo ""
../../output/expander_tests
echo ""
../../output/typewriter_tests
echo ""