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
 * Test 1: Calling monitor_destroy with NULL pointer
 * Expected: no crash, function should return safely
 */
void test_monitor_destroy_null() {
    printf("[TEST] test_monitor_destroy_null...\n");
    monitor_destroy(NULL);
    printf(GREEN "[PASS] test_monitor_destroy_null passed successfully.\n" NC);
}

/*
 * Test 2: Calling monitor_destroy on uninitialized monitor
 * Expected: no crash, monitor stays uninitialized
 */
void test_monitor_destroy_uninitialized() {
    printf("[TEST] test_monitor_destroy_uninitialized...\n");
    monitor_t monitor;
    monitor.initialized = 0; // simulate uninitialized
    monitor_destroy(&monitor);
    PRINT_RESULT("test_monitor_destroy_uninitialized", monitor.initialized == 0, "Monitor state should remain uninitialized");
    printf(GREEN "[PASS] test_monitor_destroy_uninitialized passed successfully.\n" NC);
}

/*
 * Test 3: Destroy after successful init
 * Expected: monitor.initialized == 0
 */
void test_monitor_destroy_after_init() {
    printf("[TEST] test_monitor_destroy_after_init...\n");
    monitor_t monitor;
    int res = monitor_init(&monitor);
    PRINT_RESULT("test_monitor_destroy_after_init", res == 0, "Expected init to succeed");
    monitor_destroy(&monitor);
    PRINT_RESULT("test_monitor_destroy_after_init", monitor.initialized == 0, "Monitor should be marked as uninitialized after destroy");
    printf(GREEN "[PASS] test_monitor_destroy_after_init passed successfully.\n" NC);
}

/*
 * Test 4: Double destroy
 * Expected: second call does nothing and does not crash
 */
void test_monitor_destroy_double() {
    printf("[TEST] test_monitor_destroy_double...\n");
    monitor_t monitor;
    int res = monitor_init(&monitor);
    PRINT_RESULT("test_monitor_destroy_double", res == 0, "Expected init to succeed");
    monitor_destroy(&monitor);
    monitor_destroy(&monitor); // second call
    PRINT_RESULT("test_monitor_destroy_double", monitor.initialized == 0, "Monitor should remain uninitialized after second destroy");
    printf(GREEN "[PASS] test_monitor_destroy_double passed successfully.\n" NC);
}

/*
 * Test 5: Destroy while monitor is in use (not fully testable without threads)
 * Expected: no crash, monitor becomes uninitialized
 */
void test_monitor_destroy_in_use_simulated() {
    printf("[TEST] test_monitor_destroy_in_use_simulated...\n");
    monitor_t monitor;
    int res = monitor_init(&monitor);
    PRINT_RESULT("test_monitor_destroy_in_use_simulated", res == 0, "Expected init to succeed");
    // Simulate usage (no real waiters here)
    monitor_destroy(&monitor);
    PRINT_RESULT("test_monitor_destroy_in_use_simulated", monitor.initialized == 0, "Monitor should be marked uninitialized after destroy");
    printf(GREEN "[PASS] test_monitor_destroy_in_use_simulated passed successfully.\n" NC);
}

/*
 * MAIN FUNCTION TO RUN ALL DESTROY TESTS
 */
int main() {
    printf("=== Running monitor_destroy unit tests ===\n");
    test_monitor_destroy_null();
    test_monitor_destroy_uninitialized();
    test_monitor_destroy_after_init();
    test_monitor_destroy_double();
    test_monitor_destroy_in_use_simulated();
    printf(GREEN "✅ All monitor_destroy tests finished.\n" NC);
    return 0;
}
