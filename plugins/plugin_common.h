#include <pthread.h>
#include "sync/consumer_producer.h"

/**
 * Plugin context structure holding shared data and state for a plugin
 */
typedef struct
{
    const char* name;                         // Plugin name (for diagnosis)
    consumer_producer_t* queue;               // Input queue
    pthread_t consumer_thread;                // Consumer thread
    const char* (*next_place_work)(const char*);   // Next plugin's place_work function
    const char* (*process_function)(const char*);  // Plugin-specific processing function
    int initialized;                          // Initialization flag
    int finished;                             // Finished processing flag
    int attached;                             // 0 = not attached yet; 1 = attach() was called (even if next_place_work == NULL)
    int worker_joined;                        // 0 = not joined yet; 1 = pthread_join was performed
} plugin_context_t;


/**
 * Generic consumer thread function
 * This function runs in a separate thread and processes items from the queue
 * @param arg Pointer to plugin_context_t
 * @return NULL
 */
void* plugin_consumer_thread(void* arg);

/**
 * Print error message in the format [ERROR][Plugin Name] - message
 * @param context Plugin context
 * @param message Error message
 */
void log_error(plugin_context_t* context, const char* message);

/**
 * Print info message in the format [INFO][Plugin Name] - message
 * @param context Plugin context
 * @param message Info message
 */
void log_info(plugin_context_t* context, const char* message);

/**
 * Get the plugin's name
 * @return The plugin's name (should not be modified or freed)
 */
__attribute__((visibility("default")))
const char* plugin_get_name(void);

/**
 * Initialize the common plugin infrastructure with the specified queue size
 * @param process_function Plugin-specific processing function
 * @param name Plugin name
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
const char* common_plugin_init(const char* (*process_function)(const char*), const char* name, int queue_size);


/**
 * Finalize the plugin - drain queue and terminate thread gracefully (pthread_join)
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_fini(void);

/**
 * Place work (a string) into the plugin's queue
 * @param str The string to process (plugin takes ownership if it allocates new memory)
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_place_work(const char* str);

/**
 * Attach this plugin to the next plugin in the chain
 * @param next_place_work Function pointer to the next plugin's place_work function
 */
__attribute__((visibility("default")))
void plugin_attach(const char* (*next_place_work)(const char*));

/**
 * Wait until the plugin has finished processing all work and is ready to shutdown
 * This is a blocking function used for graceful shutdown coordination
 * @return NULL on success, error message on failure
 */
__attribute__((visibility("default")))
const char* plugin_wait_finished(void);


/**
 * Returns 1 if s equals the END sentinel exactly, otherwise 0.
 * Treats NULL as "not END".
 */
int is_end(const char* s);
