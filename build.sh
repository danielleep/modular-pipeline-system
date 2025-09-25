#!/bin/bash
set -e  # Exit immediately on error

# ========================
# Color definitions
# ========================
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${GREEN}[BUILD]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# ========================
# Clean and prepare output directory
# ========================
mkdir -p output
rm -f output/*

# ========================
# Check required files exist
# ========================
REQUIRED_FILES=(
    "main.c"
    "stage2_loader.c"
    "loader.h"
    "plugins/plugin_common.c"
    "plugins/plugin_common.h"
    "plugins/plugin_sdk.h"
    "plugins/sync/monitor.c"
    "plugins/sync/monitor.h"
    "plugins/sync/consumer_producer.c"
    "plugins/sync/consumer_producer.h"
)

print_status "Checking required files..."
for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        print_error "Missing required file: $file"
        exit 1
    fi
done

print_status "All required core files are present."

# ========================
# Build plugins individually (according to example)
# ========================
PLUGINS=("logger" "uppercaser" "rotator" "flipper" "expander" "typewriter")

for plugin_name in "${PLUGINS[@]}"; do
    if [ -f "plugins/${plugin_name}.c" ]; then
        print_status "Building plugin: $plugin_name"
        gcc -fPIC -shared -Wall -Wextra -Werror -Wno-unused-parameter \
            -o output/${plugin_name}.so \
            plugins/${plugin_name}.c \
            plugins/plugin_common.c \
            plugins/sync/monitor.c \
            plugins/sync/consumer_producer.c \
            -ldl -lpthread || {
                print_error "Failed to build plugin: $plugin_name"
                exit 1
            }
    else
        print_warning "Plugin file not found: plugins/${plugin_name}.c, skipping..."
    fi
done

# ========================
# Test compile of main program
# ========================
print_status "Testing compilation of main analyzer..."
gcc -Wall -Wextra -Werror -Wno-unused-parameter \
  -I. \
  -Wl,-rpath,'$ORIGIN' \
  -o output/analyzer \
  main.c stage2_loader.c \
  -ldl -lpthread || {
    print_error "Failed to compile main analyzer"
    exit 1
  }

# gcc -Wall -Wextra -Werror -Wno-unused-parameter -o output/analyzer main.c -ldl -lpthread || {
#     print_error "Failed to compile main analyzer"
#     exit 1
# }

# ========================
# Test compile of sync and common files individually
# ========================
print_status "Testing compilation of sync and common modules..."
gcc -Wall -Wextra -Werror -Wno-unused-parameter -c plugins/plugin_common.c -I. -o output/plugin_common.o
gcc -Wall -Wextra -Werror -Wno-unused-parameter -c plugins/sync/monitor.c -I. -o output/monitor.o
gcc -Wall -Wextra -Werror -Wno-unused-parameter -c plugins/sync/consumer_producer.c -I. -o output/consumer_producer.o
