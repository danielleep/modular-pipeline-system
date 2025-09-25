#include "plugin_common.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>   

/**
 * Transformation logic for the typewriter plugin
 * @param input String to process
 * @return Processed string
 */
/* Transformation logic for the typewriter plugin */
const char* plugin_transform(const char* input)
{
    if (input == NULL) {
        return NULL;
    }

    /* Do not print for the termination token; pass it through unchanged */
    if (is_end(input)) {
        return input;
    }

    const unsigned int DELAY_US = 100000U;  /* 100 ms */
    const char *prefix = "[typewriter] ";

    /* Type the prefix character-by-character */
    for (const char *p = prefix; *p; ++p) {
        if (fputc((unsigned char)*p, stdout) == EOF) {
            /* best-effort: stop on I/O error */
            break;
        }
        fflush(stdout);
        usleep(DELAY_US);
    }

    /* Type the input character-by-character */
    size_t len = strlen(input);
    for (size_t i = 0; i < len; ++i) {
        if (fputc((unsigned char)input[i], stdout) == EOF) {
            /* best-effort: stop on I/O error */
            break;
        }
        fflush(stdout);
        usleep(DELAY_US);
    }

    /* End the line */
    fputc('\n', stdout);
    fflush(stdout);

    /* No transformation: return the original pointer */
    return input;
}



/**  
 * Initialize the typewriter plugin
 * @param queue_size Maximum number of items that can be queued
 * @return NULL on success, error message on failure
 */
const char* plugin_init(int queue_size)
{
    return common_plugin_init(plugin_transform, "typewriter", queue_size);
}
