#include "consumer_producer.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>


/**
 * Initialize a consumer-producer queue
 * @param queue Pointer to queue structure
 * @param capacity Maximum number of items
 * @return NULL on success, error message on failure
 */
const char* consumer_producer_init(consumer_producer_t* queue, int capacity)
{
    // 1. Validate input parameters
    if (queue == NULL) {
        return "Queue pointer is NULL";
    }
    if (capacity <= 0) {
        return "Invalid queue capacity";
    }
    if (queue->initialized == 1) {
        return "Queue already initialized";
    }
    if (capacity > INT_MAX / (int)sizeof(char*)) {
        return "Queue capacity too large";
    }

    // 2. Initialize base fields
    queue->items = NULL;
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->capacity = capacity;
    queue->initialized = 0; // Will be set to 1 only if init completes successfully
    queue->finished_flag = 0;

    // 2.1 Initialize the queue state mutex (NEW)
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        return "Failed to initialize queue lock";
    }

    // 3. Allocate memory for items array
    queue->items = (char**)calloc(capacity, sizeof(char*));
    if (queue->items == NULL) {
        pthread_mutex_destroy(&queue->lock);
        return "Failed to allocate memory for queue items";
    }

    // 4. Initialize monitors
    if (monitor_init(&queue->not_full_monitor) != 0) {
        free(queue->items);
        queue->items = NULL;
        pthread_mutex_destroy(&queue->lock);
        return "Failed to initialize monitors";
    }

    if (monitor_init(&queue->not_empty_monitor) != 0) {
        monitor_destroy(&queue->not_full_monitor);
        free(queue->items);
        queue->items = NULL;
        pthread_mutex_destroy(&queue->lock);
        return "Failed to initialize monitors";
    }

    if (monitor_init(&queue->finished_monitor) != 0) {
        monitor_destroy(&queue->not_full_monitor);
        monitor_destroy(&queue->not_empty_monitor);
        free(queue->items);
        queue->items = NULL;
        pthread_mutex_destroy(&queue->lock);
        return "Failed to initialize monitors";
    }

    // 5. Mark initialization success
    queue->initialized = 1;

    // 6. Return success
    return NULL;
}


/**
 * Destroy a consumer-producer queue and free its resources
 * @param queue Pointer to queue structure
 */
void consumer_producer_destroy(consumer_producer_t* queue)
{
    // 1. Validate input
    if (queue == NULL)
    {
        // Nothing to destroy
        return;
    }
    if (queue->initialized != 1)
    {
        // Queue was never successfully initialized
        return;
    }

    //  Drain remaining items (queue owns items) before freeing the items array
    //  This prevents memory leaks if destroy is called while items still remain.
    if (queue->items != NULL && queue->count > 0 && queue->capacity > 0) {
        int remaining = queue->count;
        for (int i = 0; i < remaining; ++i) {
            int idx = (queue->head + i) % queue->capacity;
            // Free any leftover item; free(NULL) is safe
            free(queue->items[idx]);
            queue->items[idx] = NULL; // Defensive: avoid accidental reuse
        }
    }

    // 2. Free the items array if allocated
    if (queue->items != NULL)
    {
        free(queue->items);
        queue->items = NULL;
    }

    // 3. Destroy monitors
    monitor_destroy(&queue->not_full_monitor);
    monitor_destroy(&queue->not_empty_monitor);
    monitor_destroy(&queue->finished_monitor);

    // 3.1 Destroy the queue state mutex (NEW)
    pthread_mutex_destroy(&queue->lock);

    // 4. Reset structure fields
    queue->capacity = 0;
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->initialized = 0;
    queue->finished_flag = 0;

}

/**
 * Add an item to the queue (producer). Blocks if queue is full.
 * @param queue Pointer to queue structure
 * @param item String to add (queue takes ownership)
 * @return NULL on success, error message on failure
 */
const char* consumer_producer_put(consumer_producer_t* queue, const char* item)
{
    // Validate input parameters
    if (queue == NULL) {
        return "Queue pointer is NULL";
    }
    if (item == NULL) {
        return "Item pointer is NULL";
    }
    if (queue->initialized != 1) {
        return "Queue not initialized";
    }
    if (queue->finished_flag == 1) {
        // Do not accept new items after finished was signaled
        return "Cannot add item after finished signal";
    }

    // Lock the queue state before checking/modifying the queue
    if (pthread_mutex_lock(&queue->lock) != 0) {
        return "Failed to lock queue";
    }

    // Re-check finished under the lock to avoid races with signal_finished()
    if (queue->finished_flag == 1) {
        pthread_mutex_unlock(&queue->lock);
        return "Cannot add item after finished signal";
    }

    // Wait while the queue is full (block without busy-wait)
    while (queue_is_full(queue)) {
        // Consume any stale signal before sleeping
        monitor_reset(&queue->not_full_monitor);

        // Release the lock while waiting so others can make progress
        pthread_mutex_unlock(&queue->lock);

        // Sleep until a producer can add (not full)
        if (monitor_wait(&queue->not_full_monitor) != 0) {
            // On monitor error, report failure (no lock held here)
            return "Failed during monitor operation (wait)";
        }

        // Re-acquire the lock and re-check the predicate in the loop
        if (pthread_mutex_lock(&queue->lock) != 0) {
            return "Failed to lock queue";
        }
        // Note: we intentionally do NOT reject here if finished_flag became 1,
        // because this put started before 'finished' and is allowed to complete.
    }

    // Insert the item into the queue (still holding the lock)
    queue->items[queue->tail] = (char*)item;  // queue takes ownership
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count++;

    // Release the lock before signaling
    pthread_mutex_unlock(&queue->lock);

    // Signal that the queue is not empty to wake waiting consumers
    monitor_signal(&queue->not_empty_monitor);

    // Success
    return NULL;
}

/**
 * Remove an item from the queue (consumer) and returns it. Blocks if queue is empty.
 * @param queue Pointer to queue structure
 * @return String item or NULL if queue is empty
 */
char* consumer_producer_get(consumer_producer_t* queue)
{
    // Validate input
    if (queue == NULL) {
        return NULL;
    }
    if (queue->initialized != 1) {
        return NULL;
    }

    // Lock the queue state before checking/modifying it
    if (pthread_mutex_lock(&queue->lock) != 0) {
        return NULL; // Failed to lock (rare)
    }

    // Block while the queue is empty and we are not finished yet
    while (queue_is_empty(queue) && queue->finished_flag == 0) {
        // Consume any stale signal before sleeping
        monitor_reset(&queue->not_empty_monitor);

        // Release the queue lock while waiting so producers can make progress
        pthread_mutex_unlock(&queue->lock);

        // Sleep until the queue becomes non-empty (or a related signal arrives)
        if (monitor_wait(&queue->not_empty_monitor) != 0) {
            // On monitor error, just return NULL (no lock is held here)
            return NULL;
        }

        // Re-acquire the queue lock and re-check the predicate in the loop
        if (pthread_mutex_lock(&queue->lock) != 0) {
            return NULL; // Failed to lock (rare)
        }
    }

    // If we are finished and still empty, nothing more to consume
    if (queue_is_empty(queue) && queue->finished_flag == 1) {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }

    // Dequeue one item under the queue lock
    int old_head = queue->head;
    char* item = queue->items[old_head];     // Ownership transfers to the caller
    queue->items[old_head] = NULL;           // Defensive: avoid accidental reuse
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;

    // Determine if we just made the queue empty after 'finished' was set
    int became_empty = (queue->count == 0 && queue->finished_flag == 1);

    // Release the queue lock before signaling
    pthread_mutex_unlock(&queue->lock);

    // Wake any producers waiting for space
    monitor_signal(&queue->not_full_monitor);

    // If the queue became empty after finishing, notify waiters on 'finished'
    if (became_empty) {
        monitor_signal(&queue->finished_monitor);
    }

    // Return the dequeued item to the caller (caller is responsible to free)
    return item;
}


/**
 * Signal that processing is finished
 * @param queue Pointer to queue structure
 */
void consumer_producer_signal_finished(consumer_producer_t* queue)
{
    // Validate input
    if (queue == NULL) {
        return;
    }
    if (queue->initialized != 1) {
        return;
    }

    // Protect finished_flag with the queue lock
    if (pthread_mutex_lock(&queue->lock) != 0) {
        return; // Failed to lock (rare)
    }

    // If already finished, nothing to do
    if (queue->finished_flag == 1) {
        pthread_mutex_unlock(&queue->lock);
        return;
    }

    // Mark as finished under the lock
    queue->finished_flag = 1;

    // Release the lock before signaling
    pthread_mutex_unlock(&queue->lock);

    // Notify waiters:
    // 1) finished_monitor: wake wait_finished() "phase 1"
    // 2) not_empty_monitor: wake consumers sleeping on "empty"
    monitor_signal(&queue->finished_monitor);
    monitor_signal(&queue->not_empty_monitor);
}

/**
 * Wait for processing to be finished
 * @param queue Pointer to queue structure
 * @return 0 on success, -1 on timeout
 */
int consumer_producer_wait_finished(consumer_producer_t* queue)
{
    // Validate input
    if (queue == NULL) {
        return -1;
    }
    if (queue->initialized != 1) {
        return -1;
    }

    // Take the queue lock to safely read/modify the queue state
    if (pthread_mutex_lock(&queue->lock) != 0) {
        return -1; // Failed to lock (rare)
    }

    // Fast path: already finished and fully drained
    if (queue->finished_flag == 1 && queue->count == 0) {
        pthread_mutex_unlock(&queue->lock);
        return 0;
    }

    // Phase 1: wait for 'finished' to be signaled (without busy-wait)
    while (queue->finished_flag == 0) {
        // Consume any stale signal before sleeping
        monitor_reset(&queue->finished_monitor);

        // Release the queue lock while waiting so others can make progress
        pthread_mutex_unlock(&queue->lock);

        // Sleep until 'finished' is signaled
        if (monitor_wait(&queue->finished_monitor) != 0) {
            // On monitor error, report failure (no lock is held here)
            return -1;
        }

        // Re-acquire the lock and re-check the predicate in the loop
        if (pthread_mutex_lock(&queue->lock) != 0) {
            return -1;
        }
    }

    // Phase 2: 'finished' is set; wait until the queue is fully drained
    while (queue->count > 0) {
        // Consume any stale signal before sleeping
        monitor_reset(&queue->finished_monitor);

        // Release the lock while waiting
        pthread_mutex_unlock(&queue->lock);

        // Sleep until a consumer makes the queue empty after 'finished'
        if (monitor_wait(&queue->finished_monitor) != 0) {
            return -1; // No lock is held here
        }

        // Re-acquire the lock and re-check
        if (pthread_mutex_lock(&queue->lock) != 0) {
            return -1;
        }
    }

    // Done: finished was signaled and the queue is empty
    pthread_mutex_unlock(&queue->lock);
    return 0;
}

/**
 * Check if the queue is full
 * @param queue Pointer to the queue structure
 * @return 1 if full, 0 otherwise
 */
int queue_is_full(const consumer_producer_t* queue)
{
    // A queue is full when the number of items equals its capacity
    return queue->count == queue->capacity;
}

/**
 * Check if the queue is empty
 * @param queue Pointer to the queue structure
 * @return 1 if empty, 0 otherwise
 */
int queue_is_empty(const consumer_producer_t* queue)
{
    // A queue is empty when there are no items in it
    return queue->count == 0;
}
