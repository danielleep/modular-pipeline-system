#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <errno.h>  
#include <limits.h>   
#include <ctype.h>    
#include "loader.h"

/* Safe helper for writing an error message into a user-provided buffer */
static void write_err(char* errbuf, size_t errsz, const char* msg) {
    if (!errbuf || errsz == 0) return;
    if (!msg) { errbuf[0] = '\0'; return; }
    /* snprintf guarantees NUL-termination within the given size */
    (void)snprintf(errbuf, errsz, "%s", msg);
}

/*
 * Parses a positive integer queue size from string `s`.
 * Returns 0 on success and writes the value to *out_size.
 * Returns non-zero on failure and writes a short error message to errbuf (if provided).
 */
int parse_queue_size(const char* s, int* out_size, char* errbuf, size_t errsz)
{
    const char* p;
    char* endptr = NULL;
    long val;

    /* Basic parameter validation */
    if (!out_size) {
        write_err(errbuf, errsz, "internal error: out_size is NULL");
        return 1;
    }
    if (!s) {
        write_err(errbuf, errsz, "missing queue_size");
        return 1;
    }

    /* Skip leading whitespace manually for a precise no-digits check */
    p = s;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '\0') {
        /* string is empty or only whitespace */
        write_err(errbuf, errsz, "missing queue_size");
        return 1;
    }

    /* Robust conversion using strtol (base 10) */
    errno = 0;
    val = strtol(p, &endptr, 10);

    if (errno == ERANGE) {
        write_err(errbuf, errsz, "queue_size out of range");
        return 1;
    }
    if (endptr == p) {
        /* no digits consumed at all (e.g. "+", "  +  ", or non-digit) */
        write_err(errbuf, errsz, "queue_size has no digits");
        return 1;
    }

    /* Disallow trailing non-space characters (allow trailing whitespace/newline) */
    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') {
        write_err(errbuf, errsz, "invalid queue_size: trailing characters");
        return 1;
    }

    /* Check int range */
    if (val > (long)INT_MAX) {
        write_err(errbuf, errsz, "queue_size out of range (>INT_MAX)");
        return 1;
    }

    /* Must be strictly positive */
    if (val <= 0) {
        write_err(errbuf, errsz, "queue_size must be a positive integer");
        return 1;
    }

    /* Success */
    *out_size = (int)val;
    return 0;
}

/* Returns a newly-allocated trimmed copy of `raw` (trim leading/trailing spaces).
 * On NULL input or OOM returns NULL.
 */
static char* trim_and_dup(const char* raw) {
    if (!raw) return NULL;
    const char* s = raw;
    while (*s && isspace((unsigned char)*s)) s++;
    const char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) e--;
    size_t len = (size_t)(e - s);
    char* out = (char*)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

/* Returns 1 if the string ends exactly with ".so", else 0. */
static int ends_with_dot_so(const char* s) {
    if (!s) return 0;
    size_t n = strlen(s);
    return (n >= 3 && s[n-3]=='.' && s[n-2]=='s' && s[n-1]=='o') ? 1 : 0;
}


/*
 * Collects plugin names from argv[start_idx..argc-1].
 * Returns 0 on success and fills *out_list (array of duplicated trimmed names) and *out_count.
 * On failure returns non-zero, writes a short error to errbuf (if provided),
 * and ensures *out_list == NULL and *out_count == 0.
 */
int collect_plugin_names(int argc, char** argv, int start_idx, char*** out_list, int* out_count, char* errbuf, size_t errsz)
{
    char** list = NULL;
    int i, count;

    /* Basic parameter validation and output initialization */
    if (!out_list || !out_count) {
        write_err(errbuf, errsz, "internal error: out pointers are NULL");
        return 1;
    }
    *out_list = NULL;
    *out_count = 0;

    if (start_idx >= argc) {
        write_err(errbuf, errsz, "missing plugin names");
        return 1;
    }

    /* Compute count of plugin names */
    count = argc - start_idx;
    if (count <= 0) {
        write_err(errbuf, errsz, "missing plugin names");
        return 1;
    }

    /* Allocate the array of char* */
    list = (char**)malloc((size_t)count * sizeof(char*));
    if (!list) {
        write_err(errbuf, errsz, "out of memory");
        return 1;
    }

    /* Process each name (preserve order, allow duplicates) */
    for (i = 0; i < count; ++i) {
        const char* raw = argv[start_idx + i];
        char* name = NULL;

        if (!raw || raw[0] == '\0') {
            write_err(errbuf, errsz, "invalid plugin name: empty");
            goto fail;
        }

        /* Optional normalization: trim leading/trailing spaces */
        name = trim_and_dup(raw);
        if (!name) {
            write_err(errbuf, errsz, "out of memory");
            goto fail;
        }
        if (name[0] == '\0') {
            /* After trimming it's empty → invalid */
            free(name);
            write_err(errbuf, errsz, "invalid plugin name: empty");
            goto fail;
        }

        /* Enforce "without .so extension" as per spec */
        if (ends_with_dot_so(name)) {
            free(name);
            write_err(errbuf, errsz, "invalid plugin name: should not include .so");
            goto fail;
        }

        list[i] = name; /* take ownership */
    }

    /* Success */
    *out_list = list;
    *out_count = count;
    return 0;

fail:
    /* Cleanup on failure */
    if (list) {
        for (int j = 0; j < i; ++j) {
            free(list[j]);
        }
        free(list);
    }
    *out_list = NULL;
    *out_count = 0;
    return 1;
}

static void print_usage_to_stdout(void) {
    /* Print EXACTLY as specified (stdout) */
    printf(
        "Usage: ./analyzer <queue_size> <plugin1> <plugin2> ... <pluginN>\n"
        "\n"
        "Arguments:\n"
        "  queue_size    Maximum number of items in each plugin's queue\n"
        "  plugin1..N    Names of plugins to load (without .so extension)\n"
        "\n"
        "Available plugins:\n"
        "  logger        - Logs all strings that pass through\n"
        "  typewriter    - Simulates typewriter effect with delays\n"
        "  uppercaser    - Converts strings to uppercase\n"
        "  rotator       - Move every character to the right.  Last character moves to the beginning.\n"
        "  flipper       - Reverses the order of characters\n"
        "  expander      - Expands each character with spaces\n"
        "\n"
        "Example:\n"
        "  ./analyzer 20 uppercaser rotator logger\n"
        "  echo 'hello' | ./analyzer 20 uppercaser rotator logger\n"
        "  echo '<END>' | ./analyzer 20 uppercaser rotator logger\n"
    );
}


/* Print an error (stderr), then the usage (stdout), and exit(1) */
static void fail_and_exit_with_usage(const char* errmsg) {
    if (errmsg && *errmsg) {
        fprintf(stderr, "%s\n", errmsg);
    } else {
        fprintf(stderr, "invalid arguments\n");
    }
    print_usage_to_stdout();
    exit(1);
}

/* Stage 1: parse command-line arguments.
 * On success: writes queue_size, plugin_names, plugin_count and returns 0.
 * On invalid input: prints error + usage and exits(1).
 */
static int stage1_parse_args(int argc, char** argv, int* queue_size_out, char*** plugin_names_out, int* plugin_count_out)
{
    char err[256];

    /* Minimum args: program, queue_size, at least one plugin */
    if (argc < 3) {
        fail_and_exit_with_usage("missing arguments");
        /* no return */
    }

    /* Parse queue_size (argv[1]) */
    if (parse_queue_size(argv[1], queue_size_out, err, sizeof(err)) != 0) {
        fail_and_exit_with_usage(err);
    }

    /* Collect plugin names from argv[2..] (without .so) */
    if (collect_plugin_names(argc, argv, 2,
                             plugin_names_out, plugin_count_out,
                             err, sizeof(err)) != 0) {
        fail_and_exit_with_usage(err);
    }

    /* Success */
    return 0;
}

/* Cleans up after an init() failure: calls fini() on already-initialized plugins
 * (in reverse order), dlcloses all handles, frees names/array, frees plugin_names (if provided),
 * prints any fini() error messages to stderr, and exits(2).
 */
static void cleanup_after_init_failure_and_exit(
        plugin_handle_t* plugins,
        int plugin_count,
        int initialized,           /* how many were successfully init'ed */
        char** plugin_names,       /* may be NULL */
        int plugin_name_count)     /* may be 0 */
{
    /* fini() previously initialized plugins, in reverse order */
    for (int j = initialized - 1; j >= 0; --j) {
        if (plugins[j].fini) {
            const char* ferr = plugins[j].fini();
            if (ferr) {
                fprintf(stderr, "fini error in plugin '%s': %s\n",
                        plugins[j].name ? plugins[j].name : "(unknown)", ferr);
            }
        }
    }

    /* dlclose() all handles and free per-plugin name strings */
    for (int k = 0; k < plugin_count; ++k) {
        if (plugins[k].handle) {
            dlclose(plugins[k].handle);
        }
        if (plugins[k].name) {
            free(plugins[k].name);
        }
    }
    free(plugins);

    /* free the argv plugin names array from Stage 1 (if still owned here) */
    if (plugin_names && plugin_name_count > 0) {
        for (int i = 0; i < plugin_name_count; ++i) {
            free(plugin_names[i]);
        }
        free(plugin_names);
    }

    /* exit with code 2 as required by the spec for init failures */
    exit(2);
}

/* Stage 3: Initialize Plugins.
 * Calls each plugin's init(queue_size). On any failure:
 *  - prints error to stderr,
 *  - performs cleanup (see helper above),
 *  - exits the process with code 2.
 * On success: returns to caller silently.
 */
static void stage3_initialize_plugins(plugin_handle_t* plugins, int plugin_count, int queue_size, char** plugin_names, int plugin_name_count)
{
    if (!plugins || plugin_count <= 0) {
        fprintf(stderr, "internal error: no plugins to initialize\n");
        exit(2);
    }

    int initialized = 0;

    for (int i = 0; i < plugin_count; ++i) {
        if (!plugins[i].init) {
            fprintf(stderr, "init pointer is NULL for plugin index %d\n", i);
            cleanup_after_init_failure_and_exit(plugins, plugin_count,
                                                initialized, plugin_names, plugin_name_count);
        }

        const char* err = plugins[i].init(queue_size);
        if (err != NULL && err[0] != '\0') {
            /* Print error to stderr (no usage here) */
            fprintf(stderr, "init failed in plugin '%s': %s\n",
                    plugins[i].name ? plugins[i].name : "(unknown)", err);

            /* Cleanup everything and exit(2) */
            cleanup_after_init_failure_and_exit(plugins, plugin_count,
                                                initialized, plugin_names, plugin_name_count);
        }

        /* Count how many have been initialized successfully */
        initialized++;
    }
}

/* Stage 4 helpers */

/* Cleanup used when Stage 4 detects an internal error.
 * Assumes all plugins were already initialized successfully (Stage 3 passed).
 * Calls fini() for all plugins (reverse order), dlclose() all handles,
 * frees names/array, frees plugin_names (if provided), and exits(2).
 */
static void stage4_cleanup_and_exit(
        plugin_handle_t* plugins,
        int plugin_count,
        char** plugin_names,
        int plugin_name_count)
{
    /* fini() in reverse order (they were all successfully initialized) */
    for (int j = plugin_count - 1; j >= 0; --j) {
        if (plugins[j].fini) {
            const char* ferr = plugins[j].fini();
            if (ferr) {
                fprintf(stderr, "fini error in plugin '%s': %s\n",
                        plugins[j].name ? plugins[j].name : "(unknown)", ferr);
            }
        }
    }

    /* close handles and free per-plugin names */
    for (int k = 0; k < plugin_count; ++k) {
        if (plugins[k].handle) dlclose(plugins[k].handle);
        if (plugins[k].name)   free(plugins[k].name);
    }
    free(plugins);

    /* free argv plugin names from Stage 1 if still owned here */
    if (plugin_names && plugin_name_count > 0) {
        for (int i = 0; i < plugin_name_count; ++i) free(plugin_names[i]);
        free(plugin_names);
    }

    /* Stage 4 internal error -> same exit code family as init errors */
    exit(2);
}

/* Stage 4: Attach plugins into a chain.
 * For each i in [0 .. plugin_count-2], call plugins[i].attach(plugins[i+1].place_work).
 * The last plugin is not attached to anything.
 * On internal error (unexpected NULL pointers / invalid count), cleanup and exit(2).
 */
static void stage4_attach_plugins(plugin_handle_t* plugins, int plugin_count, char** plugin_names, int plugin_name_count)
{
    /* Basic validation (defensive; should not happen after Stage 3) */
    if (!plugins || plugin_count < 0) {
        fprintf(stderr, "internal error: invalid plugin array/count in Stage 4\n");
        stage4_cleanup_and_exit(plugins ? plugins : NULL, plugin_count > 0 ? plugin_count : 0, plugin_names, plugin_name_count);
    }

    if (plugin_count == 0) {
        fprintf(stderr, "internal error: no plugins to attach\n");
        stage4_cleanup_and_exit(plugins, 0, plugin_names, plugin_name_count);
    }

    /* Single-plugin chain: nothing to attach; this plugin is terminal. */
    if (plugin_count == 1) return;

    /* Attach i -> (i+1) for all but the last plugin */
    for (int i = 0; i < plugin_count - 1; ++i) {
        /* Defensive pointer checks; Stage 2 guaranteed these, but we guard anyway */
        if (!plugins[i].attach || !plugins[i + 1].place_work) {
            fprintf(stderr,
                    "internal error: missing attach/place_work at index %d during Stage 4\n", i);
            stage4_cleanup_and_exit(plugins, plugin_count, plugin_names, plugin_name_count);
        }

        /* The actual linkage: current plugin forwards to next plugin's place_work */
        plugins[i].attach(plugins[i + 1].place_work);
    }
}

/* Stage 5 helpers*/

#define INPUT_BUF_SZ 1026  /* 1024 chars + optional '\n' + terminating NUL */

/* Remove trailing '\n' and optional '\r' (for CRLF) from a line buffer. */
static void strip_newline_cr(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    if (n > 0 && s[n - 1] == '\n') { s[--n] = '\0'; }
    if (n > 0 && s[n - 1] == '\r') { s[--n] = '\0'; }
}

/* Stage 5: Read input lines from stdin and feed them into the first plugin.
 * - Uses fgets() with a fixed-size buffer (INPUT_BUF_SZ).
 * - Strips trailing newline (and CR if present).
 * - Sends each line to plugins[0].place_work.
 * - If line is exactly "<END>", sends it and breaks the loop.
 * - On place_work error: print to stderr and continue (no exit, no usage).
 * - On internal errors (no plugins / NULL function pointers): cleanup + exit(2).
 */
static void stage5_read_and_feed(plugin_handle_t* plugins, int plugin_count, char** plugin_names, int plugin_name_count)
{
    /* Validate readiness */
    if (!plugins || plugin_count <= 0) {
        fprintf(stderr, "internal error: no plugins available in Stage 5\n");
        /* Reuse Stage 4 cleanup (all plugins are initialized by now) */
        stage4_cleanup_and_exit(plugins ? plugins : NULL,
                                plugin_count > 0 ? plugin_count : 0,
                                plugin_names, plugin_name_count);
    }

    if (!plugins[0].place_work) {
        fprintf(stderr, "internal error: first plugin has NULL place_work\n");
        stage4_cleanup_and_exit(plugins, plugin_count, plugin_names, plugin_name_count);
    }

    char buf[INPUT_BUF_SZ];

    /* Read lines from stdin */
    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        strip_newline_cr(buf);

        /* END sentinel */
        if (strcmp(buf, "<END>") == 0) {
            const char* perr = plugins[0].place_work("<END>");
            if (perr) {
                fprintf(stderr, "place_work error in first plugin '%s': %s\n",
                        plugins[0].name ? plugins[0].name : "(unknown)", perr);
            }
            break; /* stop reading after sending <END> */
        }

        /* Regular line */
        const char* perr = plugins[0].place_work(buf);
        if (perr) {
            /* Do not exit; the pipeline should keep flowing. */
            fprintf(stderr, "place_work error in first plugin '%s': %s\n",
                    plugins[0].name ? plugins[0].name : "(unknown)", perr);
        }
    }
}


/* Waits for each plugin to finish, in ascending order (0..N-1).
 * No stdout prints here; errors/warnings go to stderr only.
 * Does not exit on failures; cleanup will happen in Step 7.
 *
 * Note: if your plugin API defines wait_finished() returning `const char*`
 * (NULL on success), we report non-NULL as an error. If your API returns `void`,
 * just remove the error-capturing lines.
 */
static void stage6_wait_for_plugins(plugin_handle_t* plugins, int plugin_count) {
    if (!plugins || plugin_count <= 0) {
        fprintf(stderr, "internal error: no plugins to wait for in Stage 6\n");
        return; /* proceed to Step 7; nothing to wait for */
    }

    for (int i = 0; i < plugin_count; ++i) {
        if (!plugins[i].wait_finished) {
            fprintf(stderr,
                    "internal error: wait_finished is NULL for plugin index %d ('%s')\n",
                    i, plugins[i].name ? plugins[i].name : "(unknown)");
            continue;
        }

        /* If wait_finished returns const char*: NULL = success, otherwise error text */
        const char* werr = plugins[i].wait_finished();
        if (werr) {
            fprintf(stderr, "wait_finished error in plugin '%s': %s\n",
                    plugins[i].name ? plugins[i].name : "(unknown)", werr);
        }
    }
}

/*
 * - fini() for all plugins in reverse order
 * - dlclose() each handle and free per-plugin name strings
 * - free the plugins array
 * - free argv plugin names if still owned here
 * Notes:
 *   * prints to stderr only (no stdout)
 *   * tolerant/idempotent: checks NULL before freeing/closing
 *   * does NOT exit; caller proceeds to Step 8
 */
static void stage7_cleanup_all(
        plugin_handle_t* plugins,
        int plugin_count,
        char** plugin_names,
        int plugin_name_count)
{
    /* Logical shutdown: call fini() in reverse order */
    if (plugins && plugin_count > 0) {
        for (int i = plugin_count - 1; i >= 0; --i) {
            if (plugins[i].fini) {
                const char* ferr = plugins[i].fini();   /* NULL on success */
                if (ferr) {
                    fprintf(stderr, "fini error in plugin '%s': %s\n",
                            plugins[i].name ? plugins[i].name : "(unknown)", ferr);
                }
            } else {
                /* Defensive: should not happen after Stage 2 */
                fprintf(stderr, "internal warning: fini is NULL for plugin index %d\n", i);
            }
        }

        /* Technical unload: dlclose() + free per-plugin name strings */
        for (int i = 0; i < plugin_count; ++i) {
            if (plugins[i].handle) {
                if (dlclose(plugins[i].handle) != 0) {
                    const char* e = dlerror();
                    fprintf(stderr, "dlclose error for plugin '%s': %s\n",
                            plugins[i].name ? plugins[i].name : "(unknown)",
                            e ? e : "(unknown)");
                }
            }
            if (plugins[i].name) {
                free(plugins[i].name);
            }
        }

        /* Free the plugins array */
        free(plugins);
    }

    /* Free argv plugin names from Stage 1 (if still owned here) */
    if (plugin_names && plugin_name_count > 0) {
        for (int i = 0; i < plugin_name_count; ++i) {
            free(plugin_names[i]);
        }
        free(plugin_names);
    }
}

/* Prints the required final message to stdout and returns to caller.
 * Caller should return 0 from main to indicate success.
 */
static void stage8_finalize(void) {
    /* Exactly as specified: print to stdout */
    puts("Pipeline shutdown complete");
}

/**
 * Main entry point for the pipeline analyzer
 */
int main(int argc, char** argv) 
{
    int queue_size = 0;
    char** plugin_names = NULL;
    int plugin_count = 0;

    /* Step 1: Parse Command-Line Arguments */
    stage1_parse_args(argc, argv, &queue_size, &plugin_names, &plugin_count);
    
    /* Step 2: Load Plugin Shared Objects */
    plugin_handle_t* plugins = NULL;
    stage2_load_plugins(plugin_names, plugin_count, &plugins, print_usage_to_stdout);

    /* Step 3: Initialize Plugins */
    stage3_initialize_plugins(plugins, plugin_count, queue_size, plugin_names, plugin_count);

    /* Step 4: Attach Plugins Together */
    stage4_attach_plugins(plugins, plugin_count, plugin_names, plugin_count);

    /* Step 5: Read input from STDIN and feed the first plugin */
    stage5_read_and_feed(plugins, plugin_count, plugin_names, plugin_count);

    /* Step 6: Wait for Plugins to Finish */
    stage6_wait_for_plugins(plugins, plugin_count);

    /* Step 7: Clean up and unload all plugins */
    stage7_cleanup_all(plugins, plugin_count, plugin_names, plugin_count);

    plugins = NULL;
    plugin_names = NULL;
    plugin_count = 0;

    /* Step 8: Finalize */
    stage8_finalize();

    /* successssssss wowwwwwwww */
    return 0;

}
