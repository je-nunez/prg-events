
#include <assert.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>


#include "instrument.h"


static int events_enabled = 0;
static void * shared_libr_handle = NULL;
static TEntryNotificationSubr dyn_notify_entry = NULL;
static TExitNotificationSubr  dyn_notify_exit  = NULL;

static int notif_thru_unix_sockets = 0;
static int unix_sockets_fd = -1;


__attribute__((no_instrument_function))
static void * get_funct_in_so_libr(const char * funct_name, int * error_flag) {
    // We need that input argument "error_flag" be non-NULL, because the
    // return value may be NULL, since dlsym() can normally return NULL
    // if so is the symbol in the shared object.
    assert(error_flag != NULL);

    void (*dyn_funct)(void) = NULL;
    char * err_msg = NULL;

    dlerror();
    dyn_funct = dlsym(shared_libr_handle, funct_name);
    if ((err_msg = dlerror()) != NULL) {
        fprintf(stderr, "ERROR: Finding symbol '%s': %s\n", NOTIFY_ENTRY_FUNC,
                err_msg);
        *error_flag = 1;
        return NULL;
    } else {
        *error_flag = 0;
        return dyn_funct;
    }
}


__attribute__((no_instrument_function))
static void load_events_library(void) {
    int need_to_rollback = 0;    // if we need to rollback this function
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


    dyn_notify_entry =
        (TEntryNotificationSubr) get_funct_in_so_libr(NOTIFY_ENTRY_FUNC,
                &need_to_rollback);
    if (need_to_rollback == 0)
        dyn_notify_exit =
            (TExitNotificationSubr) get_funct_in_so_libr(NOTIFY_EXIT_FUNC,
                    &need_to_rollback);

    if (need_to_rollback == 0)
        return;

    // Something failed getting the addresses of the event-receiving functions
    // in the shared library. We need to rollback to a safe state (an alternative
    // would be to die; error-messages were printed to stderr by
    // "get_funct_in_so_libr()" itself when it found the error).

    dyn_notify_entry = NULL;
    dyn_notify_exit = NULL;
    if (shared_libr_handle)
        dlclose(shared_libr_handle);
    shared_libr_handle = NULL;
}


__attribute__((no_instrument_function))
static void load_ipc_unix_socket(void) {
    char* unix_socket_path = getenv(ENV_EVENT_UNIX_SOCKET);
    if (!unix_socket_path) {
        notif_thru_unix_sockets = 0;
        return;
    }

    if ((unix_sockets_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1 ) {
        perror("Unix-Socket error");
        return;
    }

    // TODO: Remaining of this code
}


void instrument_constructor(void) {
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

    // Try to load the Unix-socket through which to write the IPC of the
    // notification to send, if requested by its environment variable.
    load_ipc_unix_socket();
}


void instrument_destructor(void) {
    if (!events_enabled)
        return;

    fprintf(stderr, "In Event Final Destructor.\n");
    if (shared_libr_handle)
        dlclose(shared_libr_handle);
}


__attribute__((no_instrument_function))
static void ipc_send_notification_entry(void *entered_func, void *call_site,
                                        int frames_stack, char ** stack_locs) {
    if (notif_thru_unix_sockets == 0 ||
            unix_sockets_fd == -1)
        return;
    // TODO: send the function-call entry notification
}


__attribute__((no_instrument_function))
static void ipc_send_notification_exit(void *exited_func, void *call_site) {
    if (notif_thru_unix_sockets == 0 ||
            unix_sockets_fd == -1)
        return;
    // TODO: send the function-call exit notification
}


void __cyg_profile_func_enter(void *func, void *call_site) {
    if (!events_enabled)
        return;

    void * return_addresses[MAX_RETURN_ADDRESSES_IN_STACK];

    fprintf(stderr, "Function entry: %p %p\n   Stack:\n", func, call_site);

    int n = backtrace(return_addresses, MAX_RETURN_ADDRESSES_IN_STACK);
    char ** function_locs = backtrace_symbols(return_addresses, n);

    // Try to send notifications. First, by SO object, if set
    if (dyn_notify_entry)
        // The -1 and +1 is to ignore the frame at the top of the stack:
        // this function
        (*dyn_notify_entry)(func, call_site, n-1, function_locs+1);

    // Send IPC notification through the Unix socket, if requested
    ipc_send_notification_entry(func, call_site, n-1, function_locs+1);

    if (function_locs)
        free(function_locs);
}


void __cyg_profile_func_exit(void *func, void *call_site) {
    if (!events_enabled)
        return;

    // Try to send notifications. First, by SO object, if set
    if (dyn_notify_exit)
        (*dyn_notify_exit)(func, call_site);

    // Send IPC notification through the Unix socket, if requested
    ipc_send_notification_exit(func, call_site);
}

