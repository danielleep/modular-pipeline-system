#ifndef LOADER_H
#define LOADER_H

#include <stddef.h>   

/* -------- Plugin interface function types (as per the spec) -------- */
typedef const char* (*plugin_init_func_t)(int queue_size);
typedef const char* (*plugin_fini_func_t)(void);
typedef const char* (*plugin_place_work_func_t)(const char* s);
typedef void        (*plugin_attach_func_t)(const char* (*next_place_work)(const char*));
typedef const char* (*plugin_wait_finished_func_t)(void);

/* -------- Handle we keep per loaded plugin -------- */
typedef struct {
    plugin_init_func_t          init;
    plugin_fini_func_t          fini;
    plugin_place_work_func_t    place_work;
    plugin_attach_func_t        attach;
    plugin_wait_finished_func_t wait_finished;
    char*                       name;    /* plugin name (without .so), owned by us */
    void*                       handle;  /* dlopen handle */
} plugin_handle_t;

/* Public API: loads all plugins and resolves required symbols.
 * On ANY failure:
 *  - prints the error to stderr
 *  - prints usage (via the callback) to stdout
 *  - performs partial cleanup and exit(1).
 * On success: *out_arr points to a heap-allocated array of plugin_handle_t
 *             with length == plugin_count (caller frees later).
 */
void stage2_load_plugins(char** plugin_names, int plugin_count, plugin_handle_t** out_arr, void (*print_usage_to_stdout)(void));

/* (Optional) helpers exposed for unit-testing; can be left unused by callers. */
char* build_so_filename(const char* name); /* returns "<name>.so" (heap-allocated, caller frees) */

#endif /* LOADER_H */
