
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
static char socket_client_fname[SIZEOF_SOCKADDR_UN_SUN_PATH+1];
// Accept remote trace/debugging commands (requires the IPC through the
// Unix-domain sockets):
static int accept_remote_commands = ACCEPT_REMOTE_COMMANDS;


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
static void close_datagram_unix_socket(int unix_socket, const char* socket_path) {
    close(unix_socket);
    // remove the entry in the filespace
    unlink(socket_path);
}

__attribute__((no_instrument_function))
static void load_ipc_unix_socket(void) {
    char* unix_socket_path = getenv(ENV_EVENT_UNIX_SOCKET);
    if (!unix_socket_path) {
        notif_thru_unix_sockets = 0;
        return;
    }

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
        close_datagram_unix_socket(unix_sockets_fd, socket_client_fname);
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
        close_datagram_unix_socket(unix_sockets_fd, socket_client_fname);
        unix_sockets_fd = -1;
        return;
    }

    // We need that sending the IPC messages to the server be non-blocking
    int so_flags;
    if ((so_flags = fcntl(unix_sockets_fd, F_GETFL)) == -1) {
        perror("Unix-Socket error: fcntl(F_GETFL)");
        close_datagram_unix_socket(unix_sockets_fd, socket_client_fname);
        unix_sockets_fd = -1;
        return;
    }
    if (fcntl(unix_sockets_fd, F_SETFL, so_flags | FNDELAY | FASYNC) == -1) {
        perror("Unix-Socket error: fcntl(F_SETFL)");
        close_datagram_unix_socket(unix_sockets_fd, socket_client_fname);
        unix_sockets_fd = -1;
        return;
    }

    /* We don't need to read from the IPC Unix-domain socket.
     * Note: We would need to read from it if we accepted, e.g., some
     * tracing-debugging commands for this running program, like
     * argument locations in the stack for some functions in this programs,
     * or global data locations, whose values could be written as well
     * together with the notification sent through this Unix socket. */
    if (accept_remote_commands == 0) {
        if (shutdown(unix_sockets_fd, SHUT_RD) == -1) {
            perror("Unix-Socket error: shutdown(SHUT_RD)");
            close_datagram_unix_socket(unix_sockets_fd, socket_client_fname);
            unix_sockets_fd = -1;
            return;
        }
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
        close_datagram_unix_socket(unix_sockets_fd, socket_client_fname);
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
    char field_buffer[SOCKET_FIELD_BUFFER_MAX_SIZE],
         msg_buffer[SOCKET_ENTIRE_MSG_MAX_SIZE];
    int field_len, available_in_msg_buf;

    // assert(SOCKET_FIELD_BUFFER_MAX_SIZE < SOCKET_ENTIRE_MSG_MAX_SIZE);
    assert(sizeof field_buffer < sizeof msg_buffer);

    field_len = snprintf(field_buffer, sizeof field_buffer,
                         "Function ENTRY %p from %p [stack frames sampled %d]\n",
                         entered_func, call_site, frames_stack);
    if (field_len >= sizeof field_buffer)
        fprintf(stderr,
                "WARN: at %s:%d: field buffer truncated [max %ld: req %d]\n",
                __func__, __LINE__, sizeof field_buffer, field_len);

    strcpy(msg_buffer, field_buffer);
    available_in_msg_buf = sizeof msg_buffer - 1 - strlen(msg_buffer);

    for (int i=0; i<frames_stack; i++) {
        field_len = snprintf(field_buffer, sizeof field_buffer,
                             "  Stack frame %d: %s\n", i, stack_locs[i]);

        if (field_len >= sizeof field_buffer)
            fprintf(stderr,
                    "WARN: at %s:%d: field bufr truncated [max %ld: req %d]\n",
                    __func__, __LINE__, sizeof field_buffer, field_len);

        strncat(msg_buffer, field_buffer, available_in_msg_buf);
        if (field_len < available_in_msg_buf) {
            // We were able to copy all the field into the message buffer
            available_in_msg_buf -= field_len;
        } else {
            // We couldn't copy all the field into the message buffer
            fprintf(stderr,
                    "WARN: at %s:%d: messg bufr truncated [max %ld: req %d]\n",
                    __func__, __LINE__, sizeof field_buffer, field_len);
            break;   // break this for-loop, since the msg-buf is full
        }
    }

    // send IPC message at the end
    socket_send(msg_buffer, strlen(msg_buffer), MSG_NOSIGNAL);
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
    char msg_buffer[SOCKET_ENTIRE_MSG_MAX_SIZE];

    result = snprintf(msg_buffer, sizeof msg_buffer,
                      "Function EXIT %p to %p\n",
                      exited_func, call_site);
    if (result >= sizeof msg_buffer)
        fprintf(stderr,
                "WARN: at %s:%d: buffer truncated [max %ld: req %d]\n",
                __func__, __LINE__, sizeof msg_buffer, result);

    socket_send(msg_buffer, result, MSG_NOSIGNAL);
}


__attribute__((no_instrument_function))
void read_remote_trace_sampling_commands(int ipc_socket) {
    // TODO: read remote trace/sampling commands through the
    // IPC Unix-domain socket.
    // Probably this logic of "read_remote_trace_sampling_commands"
    // should be in practice running in a separate thread, for otherwise,
    // a long running function (without calls to other functions in the
    // program -not including library functions) may delay the reading
    // of remote trace commands.
}


void __cyg_profile_func_enter(void *func, void *call_site) {
    if (!events_enabled)
        return;  // nothing to do

    void * return_addresses[MAX_RETURN_ADDRESSES_IN_STACK];
    int has_been_notified_to_plugins = 0;

    int n = backtrace(return_addresses, MAX_RETURN_ADDRESSES_IN_STACK);
    char ** function_locs = backtrace_symbols(return_addresses, n);

    // Try to send notifications. First, by SO object, if set, because it
    // is faster (although more insecure because an SO object can crash)
    if (dyn_notify_entry) {
        // The -1 and +1 is to ignore the frame at the top of the stack:
        // this function
        (*dyn_notify_entry)(func, call_site, n-1, function_locs+1);
        has_been_notified_to_plugins = 1;
    }

    // Next send IPC notification through the Unix socket, if requested,
    // because it is a slower notification (although safer because a crash
    // in the remote IPC receiver should not affect this application)
    if (notif_thru_unix_sockets && unix_sockets_fd != -1) {
        // See if we accept remote commands (that would be through the
        // IPC Unix socket)
        if (accept_remote_commands)
            read_remote_trace_sampling_commands(unix_sockets_fd);

        // notify this event through the IPC Unix socket:
        ipc_send_notification_entry(func, call_site, n-1, function_locs+1);
        has_been_notified_to_plugins = 1;
    }

    if (! has_been_notified_to_plugins) {
        // a default notification (TODO: join all these fprintf(stderr) below
        // in a single fprint(stderr) call)
        fprintf(stderr, "Function entry: %p %p\n", func, call_site);
        if (function_locs) {
            fprintf(stderr, "   Stack:\n");
            for (int i=0; i<n; i++)
                fprintf(stderr, "      %s\n", function_locs[i]);
        }
    }

    if (function_locs)
        free(function_locs);
}


void __cyg_profile_func_exit(void *func, void *call_site) {
    if (!events_enabled)
        return;  // nothing to do

    int has_been_notified_to_plugins = 0;

    // We could call here as well the code to find the stack-frames backtraces
    // as it was done in __cyg_profile_func_enter() above.

    // Try to send notifications. First, by SO object, if set
    if (dyn_notify_exit) {
        (*dyn_notify_exit)(func, call_site);
        has_been_notified_to_plugins = 1;
    }

    // Send IPC notification through the Unix socket, if requested
    if (notif_thru_unix_sockets && unix_sockets_fd != -1) {
        // See if we accept remote commands (that would be through the
        // IPC Unix socket)
        if (accept_remote_commands)
            read_remote_trace_sampling_commands(unix_sockets_fd);

        // notify this event through the IPC Unix socket:
        ipc_send_notification_exit(func, call_site);
        has_been_notified_to_plugins = 1;
    }

    if (! has_been_notified_to_plugins)
        // a default notification
        fprintf(stderr, "Function exit: %p %p\n", func, call_site);
}

