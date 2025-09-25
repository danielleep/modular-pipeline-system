#include <stdio.h>     // fprintf
#include <stdlib.h>    // malloc, free, exit
#include <string.h>    // strlen, strcpy, strcat, strdup
#include <dlfcn.h>     // dlopen, dlsym, dlerror, dlclose
#include "loader.h"

/* ---- Symbol names expected from each plugin (as per spec) ---- */
#define SYM_PLUGIN_INIT          "plugin_init"
#define SYM_PLUGIN_FINI          "plugin_fini"
#define SYM_PLUGIN_PLACE_WORK    "plugin_place_work"
#define SYM_PLUGIN_ATTACH        "plugin_attach"
#define SYM_PLUGIN_WAIT_FINISHED "plugin_wait_finished"

/* --------- Small helper: build "<name>.so" --------- */
char* build_so_filename(const char* name) 
{
    if (!name) return NULL;
    size_t n = strlen(name);
    const char* ext = ".so";
    size_t m = strlen(ext);
    char* out = (char*)malloc(n + m + 1);
    if (!out) return NULL;
    memcpy(out, name, n);
    memcpy(out + n, ext, m + 1); /* include NUL */
    return out;
}

/* --------- Internal helpers (file-local) --------- */
static void cleanup_loaded_prefix(plugin_handle_t* arr, int filled) 
{
    if (!arr || filled <= 0) return;
    for (int i = 0; i < filled; ++i) {
        if (arr[i].handle) dlclose(arr[i].handle);
        if (arr[i].name) free(arr[i].name);
    }
}

static void fail_stage2_cleanup_and_exit(const char* msg, plugin_handle_t* arr, int filled_count, void (*print_usage_to_stdout)(void)) 
{
    if (msg && *msg) {
        fprintf(stderr, "%s\n", msg);
    } else {
        fprintf(stderr, "failed loading plugins\n");
    }
    /* free what we loaded so far */
    cleanup_loaded_prefix(arr, filled_count);
    free(arr);

    /* print spec-usage and exit(1) */
    if (print_usage_to_stdout) print_usage_to_stdout();
    exit(1);
}

static void* must_dlopen(const char* so_path, void (*print_usage_to_stdout)(void), plugin_handle_t* arr, int filled_count) 
{
    void* h = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = dlerror();
        char buf[512];
        snprintf(buf, sizeof(buf), "dlopen failed for '%s': %s",
                 so_path ? so_path : "(null)", e ? e : "(no error)");
        fail_stage2_cleanup_and_exit(buf, arr, filled_count, print_usage_to_stdout);
        /* never returns */
    }
    return h;
}

static void* must_dlsym(void* h, const char* sym, const char* so_name,
                        void (*print_usage_to_stdout)(void),
                        plugin_handle_t* arr, int filled_count) {
    /* clear any stale error first */
    (void)dlerror();
    void* p = dlsym(h, sym);
    const char* e = dlerror();
    if (e != NULL) {
        char buf[512];
        snprintf(buf, sizeof(buf), "dlsym failed for '%s' in %s: %s",
                 sym ? sym : "(null)", so_name ? so_name : "(unknown so)", e);
        fail_stage2_cleanup_and_exit(buf, arr, filled_count, print_usage_to_stdout);
    }
    return p;
}

/* ------------------ Public entrypoint for Stage 2 ------------------ */
void stage2_load_plugins(char** plugin_names,
                         int plugin_count,
                         plugin_handle_t** out_arr,
                         void (*print_usage_to_stdout)(void))
{
    if (!plugin_names || plugin_count <= 0 || !out_arr) {
        fprintf(stderr, "internal error: invalid args to stage2_load_plugins\n");
        if (print_usage_to_stdout) print_usage_to_stdout();
        exit(1);
    }

    /* allocate and zero an array for all plugins */
    plugin_handle_t* arr = (plugin_handle_t*)calloc((size_t)plugin_count, sizeof(plugin_handle_t));
    if (!arr) {
        fprintf(stderr, "out of memory\n");
        if (print_usage_to_stdout) print_usage_to_stdout();
        exit(1);
    }

    for (int i = 0; i < plugin_count; ++i) {
        char* sofile = build_so_filename(plugin_names[i]);
        if (!sofile) {
            fail_stage2_cleanup_and_exit("out of memory (building .so filename)",
                                         arr, i, print_usage_to_stdout);
        }

        /* 1) dlopen */
        void* h = must_dlopen(sofile, print_usage_to_stdout, arr, i);

        /* 2) resolve required symbols; check dlerror after each */
        plugin_init_func_t          init =
            (plugin_init_func_t)         must_dlsym(h, SYM_PLUGIN_INIT,          sofile, print_usage_to_stdout, arr, i);
        plugin_fini_func_t          fini =
            (plugin_fini_func_t)         must_dlsym(h, SYM_PLUGIN_FINI,          sofile, print_usage_to_stdout, arr, i);
        plugin_place_work_func_t    place_work =
            (plugin_place_work_func_t)   must_dlsym(h, SYM_PLUGIN_PLACE_WORK,    sofile, print_usage_to_stdout, arr, i);
        plugin_attach_func_t        attach =
            (plugin_attach_func_t)       must_dlsym(h, SYM_PLUGIN_ATTACH,        sofile, print_usage_to_stdout, arr, i);
        plugin_wait_finished_func_t wait_finished =
            (plugin_wait_finished_func_t)must_dlsym(h, SYM_PLUGIN_WAIT_FINISHED, sofile, print_usage_to_stdout, arr, i);

        /* 3) store into handle slot */
        arr[i].init          = init;
        arr[i].fini          = fini;
        arr[i].place_work    = place_work;
        arr[i].attach        = attach;
        arr[i].wait_finished = wait_finished;
        arr[i].handle        = h;

        arr[i].name = strdup(plugin_names[i]); /* without .so */
        if (!arr[i].name) {
            free(sofile);
            fail_stage2_cleanup_and_exit("out of memory (saving plugin name)",
                                         arr, i + 1, print_usage_to_stdout);
        }

        /* done with temp string */
        free(sofile);
    }

    /* success */
    *out_arr = arr;
}
