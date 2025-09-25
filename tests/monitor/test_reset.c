#include <stdio.h>
#include "../../plugins/sync/monitor.h"

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define NC "\033[0m" // No Color

/*
 * Test 1: Calling monitor_reset with NULL pointer
 * Expected: no crash
 */
void test_monitor_reset_null() {
    printf("[TEST] test_monitor_reset_null...\n");
    monitor_reset(NULL);
    printf(GREEN "[PASS] test_monitor_reset_null passed successfully.\n" NC);
}

/*
 * Test 2: Calling monitor_reset on uninitialized monitor
 * Expected: no crash
 */
void test_monitor_reset_uninitialized() {
    printf("[TEST] test_monitor_reset_uninitialized...\n");
    monitor_t monitor;
    monitor.initialized = 0;
    monitor_reset(&monitor);
    printf(GREEN "[PASS] test_monitor_reset_uninitialized passed successfully.\n" NC);
}

/*
 * Test 3: Reset after init only
 * Expected: signaled remains 0
 */
void test_monitor_reset_after_init() {
    printf("[TEST] test_monitor_reset_after_init...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_reset(&monitor);
    if (monitor.signaled == 0) {
        printf(GREEN "[PASS] test_monitor_reset_after_init passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_reset_after_init: signaled should be 0 after reset.\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 4: Reset after signal
 * Expected: signaled becomes 0
 */
void test_monitor_reset_after_signal() {
    printf("[TEST] test_monitor_reset_after_signal...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    monitor_reset(&monitor);
    if (monitor.signaled == 0) {
        printf(GREEN "[PASS] test_monitor_reset_after_signal passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_reset_after_signal: signaled should be 0 after reset.\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 5: Double reset
 * Expected: no issue, signaled stays 0
 */
void test_monitor_reset_double() {
    printf("[TEST] test_monitor_reset_double...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_signal(&monitor);
    monitor_reset(&monitor);
    monitor_reset(&monitor);
    if (monitor.signaled == 0) {
        printf(GREEN "[PASS] test_monitor_reset_double passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_reset_double: signaled should remain 0 after double reset.\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 6: Reset after destroy
 * Expected: no crash
 */
void test_monitor_reset_after_destroy() {
    printf("[TEST] test_monitor_reset_after_destroy...\n");
    monitor_t monitor;
    monitor_init(&monitor);
    monitor_destroy(&monitor);
    monitor_reset(&monitor);
    printf(GREEN "[PASS] test_monitor_reset_after_destroy passed successfully.\n" NC);
}

/*
 * MAIN FUNCTION TO RUN ALL RESET TESTS
 */
int main() {
    printf("=== Running monitor_reset unit tests ===\n");
    test_monitor_reset_null();
    test_monitor_reset_uninitialized();
    test_monitor_reset_after_init();
    test_monitor_reset_after_signal();
    test_monitor_reset_double();
    test_monitor_reset_after_destroy();
    printf(GREEN "✅ All monitor_reset tests finished.\n" NC);
    return 0;
}
