
#include <assert.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>


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
static int get_temporary_fname(char * in_out_template) {
    int temp_f = -1;
    if ((temp_f = mkstemp(in_out_template)) == -1) {
        perror("mkstemp");
        return -1;    // error
    }
    close(temp_f);
    unlink(in_out_template);
    return 0;
}


__attribute__((no_instrument_function))
static void load_ipc_unix_socket(void) {
    char* unix_socket_path = getenv(ENV_EVENT_UNIX_SOCKET);
    if (!unix_socket_path) {
        notif_thru_unix_sockets = 0;
        return;
    }

    char socket_client_fname[SIZEOF_SOCKADDR_UN_SUN_PATH+1];
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sun_family = AF_UNIX;

    strncpy(socket_client_fname, TEMPLATE_FNAME_SOCKADDR,
            SIZEOF_SOCKADDR_UN_SUN_PATH);
    if (get_temporary_fname(socket_client_fname) == -1) {
        unix_sockets_fd = -1;
        return;
    }

    strncpy(client_addr.sun_path, socket_client_fname, SIZEOF_SOCKADDR_UN_SUN_PATH);

    if ((unix_sockets_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1 ) {
        perror("Unix-Socket error: socket()");
        return;
    }

    if (bind(unix_sockets_fd, (struct sockaddr *) &client_addr,
             sizeof(client_addr)) == -1) {
        perror("Unix-Socket error: bind()");
        close(unix_sockets_fd);
        unix_sockets_fd = -1;
        return;
    }

    // connect to the IPC server (which will receive our notifications)
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, unix_socket_path,
            SIZEOF_SOCKADDR_UN_SUN_PATH);

    if (connect(unix_sockets_fd, (struct sockaddr *) &server_addr,
                sizeof(server_addr)) == -1) {
        perror("Unix-Socket error: connect()");
        close(unix_sockets_fd);
        unix_sockets_fd = -1;
        return;
    }

    // We need that sending the IPC messages to the server be non-blocking
    int so_flags;
    if ((so_flags = fcntl(unix_sockets_fd, F_GETFL)) == -1) {
        perror("Unix-Socket error: fcntl(F_GETFL)");
        close(unix_sockets_fd);
        unix_sockets_fd = -1;
        return;
    }
    if (fcntl(unix_sockets_fd, F_SETFL, so_flags | FNDELAY | FASYNC) == -1) {
        perror("Unix-Socket error: fcntl(F_SETFL)");
        close(unix_sockets_fd);
        unix_sockets_fd = -1;
        return;
    }

    // LAST:
    notif_thru_unix_sockets = 1;
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
    if (notif_thru_unix_sockets && unix_sockets_fd != -1)
        close(unix_sockets_fd);
}

__attribute__((no_instrument_function))
int socket_send(const void *buffer, size_t length, int flags) {
    ssize_t sent;

    sent = send(unix_sockets_fd, buffer, length, flags);
    if (sent == -1) {
        perror("Unix-Socket error: send()");
        return 0;
    } else if (sent != length) {
        fprintf(stderr, "Unix-Socket error: send(): couldn't send all buffer:"
                "size: %ld sent: %ld", length, sent);
        return 0;
    } else
        return 1;
}

__attribute__((no_instrument_function))
static void ipc_send_notification_entry(void *entered_func, void *call_site,
                                        int frames_stack, char ** stack_locs) {
    if (notif_thru_unix_sockets == 0 ||
            unix_sockets_fd == -1)
        return;

    // TODO: below there are several "snprintf(buffer,...)", each followed by
    //       its respective "socket_send(buffer,...)", whose line-oriented
    //       functionality matches the demo IPC receiver given to accept these
    //       messages, using "socat ... STDOUT". But a more proper way for
    //       this "ipc_send_notification_entry(params)" function to notify its
    //       params is to codify its "params" as a [possibly binary] structure,
    //       (or JSON string), and then send this structure with a single call
    //       to "socket_send(codified_structure,...)". This, as well, will
    //       increase the speed performance of this IPC trace-notification.
    int result;
    char buffer[SOCKET_BUFFER_SIZE];

    result = snprintf(buffer, sizeof buffer,
                      "Function ENTRY %p from %p [stack frames sampled %d]\n",
                      entered_func, call_site, frames_stack);
    if (result >= sizeof buffer)
        fprintf(stderr,
                "WARN: at %s:%d: buffer truncated [max %ld: req %d]\n",
                __func__, __LINE__, sizeof buffer, result);

    result = socket_send(buffer, result, MSG_NOSIGNAL);
    if (! result)
        return;

    for (int i=0; i<frames_stack; i++) {
        result = snprintf(buffer, sizeof buffer,
                          "  Stack frame %d: %s\n", i, stack_locs[i]);

        if (result >= sizeof buffer)
            fprintf(stderr,
                    "WARN: at %s:%d: buffer truncated [max %ld: req %d]\n",
                    __func__, __LINE__, sizeof buffer, result);

        result = socket_send(buffer, result, MSG_NOSIGNAL);
        if (! result)
            return;
    }
}


__attribute__((no_instrument_function))
static void ipc_send_notification_exit(void *exited_func, void *call_site) {
    if (notif_thru_unix_sockets == 0 ||
            unix_sockets_fd == -1)
        return;

    // TODO: below there are several "snprintf(buffer,...)", each followed by
    //       its respective "socket_send(buffer,...)", whose line-oriented
    //       functionality matches the demo IPC receiver given to accept these
    //       messages, using "socat ... STDOUT". But a more proper way for
    //       this "ipc_send_notification_exit(params)" function to notify its
    //       params is to codify its "params" as a [possibly binary] structure,
    //       (or JSON string), and then send this structure with a single call
    //       to "socket_send(codified_structure,...)". This, as well, will
    //       increase the speed performance of this IPC trace-notification.
    int result;
    char buffer[SOCKET_BUFFER_SIZE];

    result = snprintf(buffer, sizeof buffer, "Function EXIT %p to %p\n",
                      exited_func, call_site);
    if (result >= sizeof buffer)
        fprintf(stderr,
                "WARN: at %s:%d: buffer truncated [max %ld: req %d]\n",
                __func__, __LINE__, sizeof buffer, result);

    socket_send(buffer, result, MSG_NOSIGNAL);
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
    if (notif_thru_unix_sockets && unix_sockets_fd != -1)
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

