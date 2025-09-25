#include "plugin_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * Transformation logic for the expander plugin
 * @param input String to process
 * @return Processed string
 */
const char* plugin_transform(const char* input)
{
    // Guard: null pointer should not be processed
    if (input == NULL) {
        return NULL;
    }

    // Do not transform the termination token; pass it through unchanged
    if (is_end(input)) {
        return input;
    }

    size_t len = strlen(input);

    // Nothing to expand for empty or single-character strings
    if (len <= 1) {
        return input;
    }

    // Output length: one space between each pair => len + (len - 1)
    size_t out_len = len + (len - 1);

    // Allocate output buffer (out_len + 1 for the terminating NUL)
    char* out = (char*)malloc(out_len + 1);
    if (out == NULL) {
        // Best-effort fallback: return original input without crashing
        return input;
    }

    // Build expanded string: copy char, then (if not last) a single space
    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
        out[j++] = input[i];
        if (i < len - 1) {
            out[j++] = ' ';
        }
    }
    out[out_len] = '\0';

    return out;
}

/**
 * Initialize the expander plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
const char* plugin_init(int queue_size)
{
    return common_plugin_init(plugin_transform, "expander", queue_size);
}
