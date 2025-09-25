#define _POSIX_C_SOURCE 200809L

#include "plugin_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char END_SENTINEL[] = "<END>";
static plugin_context_t g_plugin_context;

/**
 * Returns 1 if s equals the END sentinel exactly, otherwise 0.
 * Treats NULL as "not END".
 */
inline int is_end(const char* s) {
    return (s != NULL && strcmp(s, END_SENTINEL) == 0);
}


/**
 * Generic consumer thread function
 * This function runs in a separate thread and processes items from the queue
 * @param arg Pointer to plugin_context_t
 * @return NULL
 */
void* plugin_consumer_thread(void* arg)
{
    plugin_context_t* ctx = (plugin_context_t*)arg;
    if (!ctx || !ctx->queue || !ctx->process_function) {
        /* Nothing we can safely do */
        return NULL;
    }

    for (;;) {
        /* 1) Blocking fetch from the queue (no busy-wait) */
        char* in = consumer_producer_get(ctx->queue);
        if (in == NULL) {
            continue;
        }

        /* 2) END propagation and shutdown */
        if (is_end(in)) {
            if (ctx->attached && ctx->next_place_work) {
                /* Forward END downstream; ownership passes on success */
                const char* err = ctx->next_place_work(in);
                if (err != NULL) {
                    /* Downstream refused END; we still own the buffer, free it here */
                    log_error(ctx, err);
                    free(in);
                }
            } else {
                /* Last plugin: free END here */
                free(in);
            }

            /* Mark finished and exit the loop (graceful shutdown) */
            ctx->finished = 1;
            consumer_producer_signal_finished(ctx->queue);
            return NULL;
        }

        /* 3) Process a regular string */
        const char* out_c = ctx->process_function(in);
        char* out = (char*)out_c; /* We may need to free it depending on ownership rules */

        if (out == NULL) {
            /* Transform failed: nothing to send downstream; we still own input */
            log_error(ctx, "transform failed");
            free(in);
            continue;
        }

        /* 4) Forward or finalize depending on whether there is a next stage */
        if (ctx->attached && ctx->next_place_work) {
            /* Forward downstream; ownership transfers on success */
            const char* err = ctx->next_place_work(out);
            if (err == NULL) {
                /* Successfully handed off `out`; release any old input we still own */
                if (out != in) {
                    free(in);
                }
                /* If out == in (in-place), we passed the same pointer; do not free here */
            } else {
                log_error(ctx, err);
                /* Downstream failed; we still own whatever we tried to send */
                if (out != in) {
                    /* We allocated a new buffer: free both the new output and old input */
                    free(out);
                    free(in);
                } else {
                    /* In-place case: we only have one buffer to free */
                    free(in);
                }
                /* Downstream is broken; exit to avoid endless failures */
                continue;
            }
        } else {
            /* Last plugin in the chain: nothing to forward
               We must free the final string locally to avoid leaks */
            if (out != in) {
                /* We created a new buffer: free both new output and old input */
                free(out);
                free(in);
            } else {
                /* In-place: single buffer to free */
                free(in);
            }
            /* Continue processing until END arrives */
        }
    }
    return NULL;
}




//  void* plugin_consumer_thread(void* arg)
// {
//     plugin_context_t* ctx = (plugin_context_t*)arg;

//     for (;;) {
//         /* Pull next item from our input queue */
//         char* in = consumer_producer_get(ctx->queue);
//         if (in == NULL) {
//             /* Graceful upstream end (nothing more to consume) */
//             break;
//         }

//         /* Handle end-of-stream token: never transform it */
//         if (is_end(in)) {
//             if (ctx->attached == 1 && ctx->next_place_work != NULL) {
//                 /* Forward the token downstream. Ownership moves to the next stage. */
//                 const char* err = ctx->next_place_work(in);
//                 if (err != NULL) {
//                     log_error(ctx, err);
//                     free(in);  /* We still own it if forwarding failed */
//                 }
//                 /* DO NOT free(in); next stage will free it. */
//             } else {
//                 /* Last stage: safe to release the token here. */
//                 free(in);
//             }

//             /* Mark our stage as finished and stop consuming */
//             ctx->finished = 1;
//             consumer_producer_signal_finished(ctx->queue);
//             break;
//         }

//         /* Transform. We currently own 'in'. */
//         const char* out = ctx->process_function(in);
//         if (out == NULL) {
//             /* Transformation failed for this item; drop it and continue. */
//             log_error(ctx, "transform failed");
//             free(in);
//             continue;
//         }

//         if (ctx->attached == 1 && ctx->next_place_work != NULL) {
//             /* Non-final stage: forward result to next stage.
//                The queue takes ownership of 'out'. */
//             const char* err = ctx->next_place_work(out);
//             if (err != NULL) {
//                 log_error(ctx, err);
//             }

//             /* Memory rules:
//                - If 'out' is a NEW buffer (out != in), we no longer need 'in' -> free it.
//                - If 'out' == 'in', DO NOT free here; next stage now owns that buffer. */
//             if (out != in) {
//                 free(in);
//                 free(out);
//             }
//             /* DO NOT free(out) here; ownership has moved downstream. */

//         } else {
//             /* Final stage: the plugin already performed side-effects (e.g., printed).
//                We must free exactly once per allocated buffer: */
//             if (out == in) {
//                 /* Single buffer case (logger/typewriter): free once. */
//                 free((void*)out);
//             } else {
//                 /* Two distinct buffers: free both. */
//                 free((void*)out);
//                 free(in);
//             }
//         }
//     }

//     return NULL;
// }



// void* plugin_consumer_thread(void* arg)
// {
//     // Convert and validate context
//     plugin_context_t* ctx = (plugin_context_t*)arg;
//     if (ctx == NULL || ctx->queue == NULL) {
//         // Cannot even log safely without a context
//         return NULL;
//     }

//     if (ctx->process_function == NULL) {
//         log_error(ctx, "no process_function provided");
//         return NULL;
//     }

//     // Main work loop
//     for (;;)
//     {
//         // Pull next item (blocks if empty). NULL means queue was finished and drained.
//         char* in = consumer_producer_get(ctx->queue);
//         if (in == NULL) {
//             break;  // graceful end-of-input
//         }

//         // Handle end-of-stream token first (do not process/print it)
//         if (is_end(in)) {
//             if (ctx->attached == 1 && ctx->next_place_work != NULL) {
//                 const char* err = ctx->next_place_work(in);
//                 if (err != NULL) {
//                     log_error(ctx, err);
//                 }
//             }
//             free(in);
//             consumer_producer_signal_finished(ctx->queue);
//             ctx->finished = 1;
//             break;
//         }

//         // Transform the input. Ownership of 'in' is on the worker.
//         const char* out = ctx->process_function(in);

//         if (out == NULL) {
//             // Transform failed for this item — log and continue with the next one
//             log_error(ctx, "transform failed");
//             free(in);
//             continue;
//         }

//         // Free the input after transform 
//         if (out != in) {
//             free(in);
//         } 


//         // Forward downstream if attached; otherwise print to stdout
//         if (ctx->attached == 1 && ctx->next_place_work != NULL) {
//             const char* err = ctx->next_place_work(out);
//             if (err != NULL) {
//                 log_error(ctx, err);
//             }
//         } 
        
//         // own 'out' and must free after forwarding
//         free((void*)out);
//     }

//     // Mark worker finished (join is done in plugin_fini, not here)
//     ctx->finished = 1;
//     return NULL;
// }



/**
 * Print error message in the format [ERROR][Plugin Name] - message
 * @param context Plugin context
 * @param message Error message
 */
void log_error(plugin_context_t* context, const char* message)
{
    // Resolve plugin name (fallback to "unknown" if context or name is missing)
    const char* name = (context && context->name) ? context->name : "unknown";

    // Resolve message text (fallback to "unknown error" if NULL/empty)
    const char* text = (message && message[0] != '\0') ? message : "unknown error";

    // Avoid interleaving from multiple threads writing to stderr
    flockfile(stderr);

    // Print in the exact required format and ensure newline at the end
    // Use a fixed format string to avoid format-string vulnerabilities
    fprintf(stderr, "[ERROR][%s] - %s\n", name, text);

    // // Flush immediately so logs appear in real time
    // fflush(stderr);

    // Release the stream lock
    funlockfile(stderr);
}

/**
 * Print info message in the format [INFO][Plugin Name] - message
 * @param context Plugin context
 * @param message Info message
 */
void log_info(plugin_context_t* context, const char* message)
{
    // Resolve plugin name (fallback to "unknown" if context or name is missing)
    const char* name = (context && context->name) ? context->name : "unknown";

    // Resolve message text (fallback to "no info" if NULL/empty)
    const char* text = (message && message[0] != '\0') ? message : "no info";

    // Prevent interleaved lines from multiple threads
    flockfile(stderr);

    // Print in the required format and ensure newline
    fprintf(stderr, "[INFO][%s] - %s\n", name, text);

    // // Flush to show logs immediately
    // fflush(stderr);

    // Release the stream lock
    funlockfile(stderr);
}

/**
 * Get the plugin's name
 * @return The plugin's name (should not be modified or freed)
 */
const char* plugin_get_name(void)
{
    // Snapshot fields locally to avoid reading changing state twice
    const int inited = g_plugin_context.initialized;
    const char* name = g_plugin_context.name;

    // If called before init, during/after fini, or name missing → return a safe fallback
    if (inited != 1 || name == NULL || name[0] == '\0') {
        return "unknown";
    }

    return name;
}


/**
 * Initialize the common plugin infrastructure with the specified queue size
 * @param process_function Plugin-specific processing function
 * @param name Plugin name
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
const char* common_plugin_init(const char* (*process_function)(const char*),
                               const char* name,
                               int queue_size)
{
    // Validate inputs
    if (process_function == NULL) {
        return "invalid process function";
    }
    if (name == NULL || name[0] == '\0') {
        return "invalid plugin name";
    }
    if (queue_size <= 0) {
        return "invalid queue size";
    }
    if (g_plugin_context.initialized == 1) {
        return "plugin already initialized";
    }

    // Reset context to a known base state (do not mark initialized yet)
    g_plugin_context.attached       = 0;
    g_plugin_context.initialized    = 0;
    g_plugin_context.finished       = 0;
    g_plugin_context.worker_joined  = 0;
    g_plugin_context.next_place_work = NULL;
    g_plugin_context.queue          = NULL;
    g_plugin_context.name           = name;               // set name early for logging
    g_plugin_context.process_function = process_function;

    // Allocate and initialize the queue
    g_plugin_context.queue = (consumer_producer_t*)malloc(sizeof(consumer_producer_t));
    if (g_plugin_context.queue == NULL) {
        log_error(&g_plugin_context, "out of memory");
        return "out of memory";
    }

    const char* qerr = consumer_producer_init(g_plugin_context.queue, queue_size);
    if (qerr != NULL) {
        // Propagate the queue's error upward; clean up the allocation
        log_error(&g_plugin_context, qerr);
        free(g_plugin_context.queue);
        g_plugin_context.queue = NULL;
        return qerr;
    }

    // Start the worker thread
    int trc = pthread_create(&g_plugin_context.consumer_thread,
                             NULL,
                             plugin_consumer_thread,
                             (void*)&g_plugin_context);
    if (trc != 0) {
        log_error(&g_plugin_context, "thread create failed");
        consumer_producer_destroy(g_plugin_context.queue);
        free(g_plugin_context.queue);
        g_plugin_context.queue = NULL;

        // keep context in a non-initialized, clean state
        g_plugin_context.attached       = 0;
        g_plugin_context.finished       = 0;
        g_plugin_context.initialized    = 0;
        g_plugin_context.worker_joined  = 0;
        g_plugin_context.next_place_work = NULL;
        return "thread create failed";
    }

    // Mark success only after everything is ready
    g_plugin_context.initialized = 1;
    return NULL;
}

/**
 * Finalize the plugin - drain queue and terminate thread gracefully (pthread_join)
 * @return NULL on success, error message on failure
 */
// Finalize the plugin: ensure all work is drained, join the worker, and release resources.
// Returns NULL on success, or a constant error string on failure.
const char* plugin_fini(void)
{
    // Validate initialization state
    if (g_plugin_context.initialized != 1) {
        log_error(&g_plugin_context, "plugin_fini: plugin not initialized");
        return "plugin not initialized";
    }

    // Guard against joining from the worker thread itself
    if (pthread_equal(pthread_self(), g_plugin_context.consumer_thread)) {
        log_error(&g_plugin_context, "plugin_fini: cannot join self");
        return "cannot join self";
    }

    // Block until the queue has been fully drained 
    const char* werr = plugin_wait_finished();
    if (werr != NULL) {
        log_error(&g_plugin_context, werr);
        return werr;
    } 

    // Join the worker thread exactly once
    if (g_plugin_context.worker_joined == 0) {
        int jrc = pthread_join(g_plugin_context.consumer_thread, NULL);
        if (jrc != 0) {
            log_error(&g_plugin_context, "plugin_fini: join failed");
            return "join failed";
        }
        g_plugin_context.worker_joined = 1;
    }

    // Destroy and free the queue
    if (g_plugin_context.queue != NULL) {
        consumer_producer_destroy(g_plugin_context.queue);
        free(g_plugin_context.queue);
        g_plugin_context.queue = NULL;
    }

    // Reset context fields (do not free 'name' — no ownership)
    g_plugin_context.next_place_work  = NULL;
    g_plugin_context.process_function = NULL;
    g_plugin_context.attached         = 0;
    g_plugin_context.finished         = 0;
    g_plugin_context.name             = NULL;   // optional: prevent accidental reuse
    // (optional) clear thread handle
    g_plugin_context.consumer_thread  = (pthread_t)0;

    // Mark as not initialized
    g_plugin_context.initialized = 0;

    // Success
    return NULL;
}


/**
 * Place work (a string) into the plugin's queue
 * @param str The string to process (plugin takes ownership if it allocates new memory)
 * @return NULL on success, error message on failure
 */
const char* plugin_place_work(const char* str)
{
    // Basic validation
    if (str == NULL) {
        log_error(&g_plugin_context, "plugin_place_work: invalid input (NULL)");
        return "invalid input";  // SDK: non-NULL on failure
    }
    if (g_plugin_context.initialized != 1) {
        log_error(&g_plugin_context, "plugin_place_work: plugin not initialized");
        return "plugin not initialized";
    }

    // Duplicate input so the queue/worker owns the memory
    char* dup = strdup(str);
    if (dup == NULL) {
        log_error(&g_plugin_context, "plugin_place_work: out of memory");
        return "out of memory";
    }

    // Enqueue (queue takes ownership on success)
    const char* err = consumer_producer_put(g_plugin_context.queue, dup);
    if (err != NULL) {
        // put failed — we still own 'dup'
        free(dup);
        log_error(&g_plugin_context, err);
        return err;  // propagate queue's constant error string
    }

    // Success
    return NULL;
}

/**
 * Attach this plugin to the next plugin in the chain
 * @param next_place_work Function pointer to the next plugin's place_work function
 */
void plugin_attach(const char* (*next_place_work)(const char*))
{
    // Ensure attach is called only after successful init
    if (g_plugin_context.initialized != 1) {
        log_error(&g_plugin_context, "attach called before init");
        return;
    }

    // Prevent attaching while/after finishing
    if (g_plugin_context.finished == 1) {
        log_error(&g_plugin_context, "attach after finish");
        return;
    }

    // Prevent double attach: keep the original wiring
    if (g_plugin_context.attached == 1) {
        log_error(&g_plugin_context, "attach called twice");
        return;
    }


    // Store downstream hook (NULL means this is the last plugin in the chain)
    g_plugin_context.next_place_work = next_place_work;

    // Mark that attach() was explicitly called (even if next_place_work == NULL)
    g_plugin_context.attached = 1;
}

/**
 * Wait until the plugin has finished processing all work and is ready to shutdown
 * This is a blocking function used for graceful shutdown coordination
 * @return NULL on success, error message on failure
 */
const char* plugin_wait_finished(void)
{
    // Validate initialization state
    if (g_plugin_context.initialized != 1) {
        log_error(&g_plugin_context, "plugin_wait_finished: plugin not initialized");
        return "plugin not initialized";
    }

    // Block until the queue is marked finished and fully drained
    int er = consumer_producer_wait_finished(g_plugin_context.queue);
    if (er != 0) {
        log_error(&g_plugin_context, "plugin_wait_finished: wait finished failed");
        return "wait finished failed";
    }

    // Success 
    return NULL;
}
