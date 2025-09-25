#include <stdio.h>
#include "../../plugins/sync/monitor.h"

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define NC "\033[0m" // No Color

/*
 * Helper macro for test result printing
 */
#define PRINT_RESULT(test_name, condition, message) \
    do { \
        if (!(condition)) { \
            printf(RED "[FAIL] %s: %s\n" NC, test_name, message); \
            return; \
        } \
    } while (0)

/*
 * Test 1: Calling monitor_init with NULL pointer
 * Expected: return -1
 */
void test_monitor_init_null() {
    printf("[TEST] test_monitor_init_null...\n");
    int res = monitor_init(NULL);
    if (res == -1) {
        printf(GREEN "[PASS] test_monitor_init_null passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_init_null: Expected -1 when passing NULL pointer\n" NC);
    }
}

/*
 * Test 2: Successful initialization
 * Expected: return 0, monitor->initialized == 1, monitor->signaled == 0
 */
void test_monitor_init_success() {
    printf("[TEST] test_monitor_init_success...\n");
    monitor_t monitor;
    int res = monitor_init(&monitor);
    if (res == 0 && monitor.initialized == 1 && monitor.signaled == 0) {
        printf(GREEN "[PASS] test_monitor_init_success passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_init_success: Unexpected state after init\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 3: Double initialization on same object
 * Expected: second call returns -1
 */
void test_monitor_init_double() {
    printf("[TEST] test_monitor_init_double...\n");
    monitor_t monitor;
    int res1 = monitor_init(&monitor);
    int res2 = monitor_init(&monitor);
    if (res1 == 0 && res2 == -1) {
        printf(GREEN "[PASS] test_monitor_init_double passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_init_double: Expected second init to fail\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 4: Initialization after destroy
 * Expected: return 0 after destroy (can re-init)
 */
void test_monitor_init_after_destroy() {
    printf("[TEST] test_monitor_init_after_destroy...\n");
    monitor_t monitor;
    int res1 = monitor_init(&monitor);
    monitor_destroy(&monitor);
    int res2 = monitor_init(&monitor);
    if (res1 == 0 && res2 == 0) {
        printf(GREEN "[PASS] test_monitor_init_after_destroy passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_init_after_destroy: Re-init after destroy failed\n" NC);
    }
    monitor_destroy(&monitor);
}

/*
 * Test 5: Multiple instances initialized independently
 * Expected: both initialized successfully, no interference
 */
void test_monitor_init_multiple_instances() {
    printf("[TEST] test_monitor_init_multiple_instances...\n");
    monitor_t monitor1;
    monitor_t monitor2;
    int res1 = monitor_init(&monitor1);
    int res2 = monitor_init(&monitor2);
    if (res1 == 0 && res2 == 0 && monitor1.initialized == 1 && monitor2.initialized == 1) {
        printf(GREEN "[PASS] test_monitor_init_multiple_instances passed successfully.\n" NC);
    } else {
        printf(RED "[FAIL] test_monitor_init_multiple_instances: Unexpected state in one or both monitors\n" NC);
    }
    monitor_destroy(&monitor1);
    monitor_destroy(&monitor2);
}

/*
 * MAIN FUNCTION TO RUN ALL INIT TESTS
 */
int main() {
    printf("=== Running monitor_init unit tests ===\n");
    test_monitor_init_null();
    test_monitor_init_success();
    test_monitor_init_double();
    test_monitor_init_after_destroy();
    test_monitor_init_multiple_instances();
    printf(GREEN "✅ All monitor_init tests finished.\n" NC);
    return 0;
}
