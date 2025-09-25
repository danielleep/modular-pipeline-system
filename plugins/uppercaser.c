#include "plugin_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * Transformation logic for the uppercaser plugin
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

    // Empty string is a no-op; return as-is
    size_t len = strlen(input);
    if (len == 0) {
        return input;
    }

    // Allocate output buffer (len + 1 for the terminating NUL)
    char* out = (char*)malloc(len + 1);
    if (out == NULL) {
        // return original input without crashing
        return input;
    }

    // Convert ASCII lowercase letters to uppercase; copy other chars as-is
    for (size_t i = 0; i < len; ++i) {
        unsigned char ch = (unsigned char)input[i];
        if (ch >= 'a' && ch <= 'z') {
            out[i] = (char)('A' + (ch - 'a'));
        } else {
            out[i] = (char)ch;
        }
    }
    out[len] = '\0';

    return out;
}

/**
 * Initialize the uppercaser plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
const char* plugin_init(int queue_size)
{
    return common_plugin_init(plugin_transform, "uppercaser", queue_size);
}
