#include "monitor.h"
#include <stdlib.h>     

int monitor_init(monitor_t* monitor)
{
    // Check for NULL pointer
    if (monitor == NULL)
    {
        return -1;
    }

    // Check if monitor is already initialized
    if (monitor->initialized == 1)
    {
        return -1;
    }

    // Reset internal values
    monitor->signaled = 0;
    monitor->initialized = 0; // Will set to 1 only if init is successful

    // Initialize mutex
    int res = pthread_mutex_init(&monitor->mutex, NULL);
    if (res != 0)
    {
        return -1;
    }

    // Initialize condition variable
    res = pthread_cond_init(&monitor->condition, NULL);
    if (res != 0)
    {
        pthread_mutex_destroy(&monitor->mutex);
        return -1;
    }

    // Mark as successfully initialized
    monitor->initialized = 1;

    // Return success
    return 0;
}

/** 
* Destroy a monitor and free its resources
* @param monitor Pointer to monitor structure
*/
void monitor_destroy(monitor_t* monitor)
{
    // Check for NULL pointer
    if (monitor == NULL) {
        return;
    }

    // Check if monitor was initialized
    if (monitor->initialized == 0) {
        return;
    }

    // Free condition variable resource
    pthread_cond_destroy(&monitor->condition);

    // Free mutex resource
    pthread_mutex_destroy(&monitor->mutex);

    // Reset internal values
    monitor->signaled = 0;
    monitor->initialized = 0;
}



/**
 * Signal a monitor (sets the monitor state)
 * @param monitor Pointer to monitor structure
 */
void monitor_signal(monitor_t* monitor)
{
    // Check for NULL pointer
    if (monitor == NULL) {
        return;
    }

    // Check if monitor was initialized
    if (monitor->initialized == 0) {
        return;
    }

    // Attempt to lock the mutex
    if (pthread_mutex_lock(&monitor->mutex) != 0) {
        // If lock fails, exit function without making changes
        return;
    }

    // Update the signaled state (set to 1 always, even if already 1)
    monitor->signaled = 1;

    // Send broadcast signal to wake all waiting threads
    pthread_cond_broadcast(&monitor->condition);

    // Unlock the mutex
    pthread_mutex_unlock(&monitor->mutex);
}

/**
 * Reset a monitor (clears the monitor state)
 * @param monitor Pointer to monitor structure
 */
void monitor_reset(monitor_t* monitor)
{
    // Check for NULL pointer
    if (monitor == NULL) {
        return;
    }

    // Check if monitor was initialized
    if (monitor->initialized == 0) {
        return;
    }

    // Attempt to lock the mutex
    if (pthread_mutex_lock(&monitor->mutex) != 0) {
        return; // Exit if lock fails
    }

    // Reset the signaled state
    monitor->signaled = 0;

    // Attempt to unlock the mutex
    pthread_mutex_unlock(&monitor->mutex);
}

/**
* Wait for a monitor to be signaled (infinite wait)
* @param monitor Pointer to monitor structure
* @return 0 on success, -1 on error
*/
int monitor_wait(monitor_t* monitor)
{
    // Check for NULL pointer
    if (monitor == NULL) {
        return -1;
    }

    // Check if monitor was initialized
    if (monitor->initialized == 0) {
        return -1;
    }

    // Attempt to lock the mutex
    if (pthread_mutex_lock(&monitor->mutex) != 0) {
        return -1;
    }

    // Wait for a signal (handle spurious wakeups)
    while (monitor->signaled == 0) {
        if (pthread_cond_wait(&monitor->condition, &monitor->mutex) != 0) {
            // Unlock mutex before returning in case of error
            pthread_mutex_unlock(&monitor->mutex);
            return -1;
        }
    }

    // Attempt to unlock the mutex
    if (pthread_mutex_unlock(&monitor->mutex) != 0) {
        return -1;
    }

    // Return success
    return 0;
}