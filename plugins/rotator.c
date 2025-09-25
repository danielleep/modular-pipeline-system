#include "plugin_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * Transformation logic for the rotator plugin
 * @param input String to process
 * @return Processed string
 */
const char* plugin_transform(const char* input)
{
    // null pointer should not be processed
    if (input == NULL) {
        return NULL;
    }

    // Do not transform the termination token; pass it through unchanged
    if (is_end(input)) {
        return input;
    }

    // For empty or single-character strings, rotation is a no-op
    size_t len = strlen(input);
    if (len <= 1) {
        return input;
    }

    // Allocate output buffer (len + 1 for the terminating NUL)
    char* out = (char*)malloc(len + 1);
    if (out == NULL) {
        // Best-effort fallback: return original input without crashing
        return input;
    }

    // Right-rotate by one: last char goes to index 0, others shift right by one
    out[0] = input[len - 1];
    for (size_t i = 0; i < len - 1; ++i) {
        out[i + 1] = input[i];
    }
    out[len] = '\0';

    return out;}

/**
 * Initialize the rotator plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
const char* plugin_init(int queue_size)
{
    return common_plugin_init(plugin_transform, "rotator", queue_size);
}
