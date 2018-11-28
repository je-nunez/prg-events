
#include <dlfcn.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>


#include "instrument.h"


static int events_enabled = 0;
static void * shared_libr_handle = NULL;
static void (*dyn_notify_entry)(void) = NULL; // TODO: it should receive params
static void (*dyn_notify_exit)(void) = NULL;  // TODO: it should receive params


__attribute__((no_instrument_function))
static void load_events_library(void)
{
    char* error_msg;
    char* event_lib_name = getenv(ENV_EVENT_LIB_NAME);
    if (!event_lib_name) {
        fprintf(stderr, "No Events-Library specified in environment var %s.\n",
                ENV_EVENT_LIB_NAME);
        return;
    }

    shared_libr_handle = dlopen(event_lib_name, RTLD_LAZY | RTLD_LOCAL);
    if (!shared_libr_handle) {
        fprintf(stderr, "ERROR: Couldn't open Events-Library named '%s': %s\n",
                event_lib_name, dlerror());
        return;
    }

    dlerror();
    dyn_notify_entry = dlsym(shared_libr_handle, NOTIFY_ENTRY_FUNC);
    if ((error_msg = dlerror()) != NULL) {
       fprintf(stderr, "ERROR: Finding symbol '%s': %s\n", NOTIFY_ENTRY_FUNC,
               error_msg);
       goto error_loading_known_functions;
    }

    dlerror();
    dyn_notify_exit = dlsym(shared_libr_handle, NOTIFY_EXIT_FUNC);
    if ((error_msg = dlerror()) != NULL) {
       fprintf(stderr, "ERROR: Finding symbol '%s': %s\n", NOTIFY_EXIT_FUNC,
               error_msg);
       goto error_loading_known_functions;
    }

    return;

  error_loading_known_functions:
    dyn_notify_entry = NULL;
    dyn_notify_exit = NULL;
    if (shared_libr_handle)
        dlclose(shared_libr_handle);
    shared_libr_handle = NULL;
}


void instrument_constructor(void)
{
    char* trace_on = getenv(ENV_EVENTS_ENABLED);

    events_enabled = 0;    // not necessary, by global initialization
    shared_libr_handle = NULL;
    if (!trace_on)
        return;

    if (strcasecmp(trace_on, "on") == 0 ||
        strcasecmp(trace_on, "true") == 0 ||
        strcasecmp(trace_on, "1") == 0) {

        events_enabled = 1;
        fprintf(stderr, "In Event Initial Constructor.\n");
    } else
        return;

    // At this point we know that the events are enabled
    // Try to load shared-object libr with notification functions to call, if
    // requested by environment variable.
    load_events_library();
}


void instrument_destructor(void)
{
    if (!events_enabled)
        return;

    fprintf(stderr, "In Event Final Destructor.\n");
    if (shared_libr_handle)
        dlclose(shared_libr_handle);
}


void __cyg_profile_func_enter(void *func, void *call_site)
{
    if (!events_enabled)
        return;

    void * return_addresses[MAX_RETURN_ADDRESSES_IN_STACK];

    fprintf(stderr, "Function entry: %p %p\n   Stack:\n", func, call_site);

    int n = backtrace(return_addresses, MAX_RETURN_ADDRESSES_IN_STACK);
    char ** function_locs = backtrace_symbols(return_addresses, n);
    if (function_locs) {
        for (int i=0; i<n; i++)
            fprintf(stderr, "      %s\n", function_locs[i]);
        free(function_locs);
    }

    if (dyn_notify_entry)
        (*dyn_notify_entry)();    // TODO: should receive parameters
}


void __cyg_profile_func_exit(void *func, void *call_site)
{
    if (!events_enabled)
        return;

    fprintf(stderr, "Function exit: %p %p\n", func, call_site);
    if (dyn_notify_exit)
        (*dyn_notify_exit)();    // TODO: should receive parameters
}

