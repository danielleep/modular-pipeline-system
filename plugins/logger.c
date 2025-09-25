#include "plugin_common.h"
#include <stdio.h>
#include <string.h>

/**
 * Transformation logic for the logger plugin
 * @param input String to process
 * @return Processed string
 */
const char* plugin_transform(const char* input)
{
    // null pointer should not be processed
    if (input == NULL) {
        return NULL;
    }

    // Do not print for the termination token; pass it through unchanged
    if (is_end(input)) {
        return input;
    }

    // Print the log line to STDOUT (empty strings are allowed)
    // Single call to keep the line as atomic as possible
    fprintf(stdout, "[logger] %s\n", input);
    fflush(stdout);

    // No transformation is done; return the original pointer
    return input;
}

/**
 * Initialize the logger plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
const char* plugin_init(int queue_size)
{
    return common_plugin_init(plugin_transform, "logger", queue_size);
}
