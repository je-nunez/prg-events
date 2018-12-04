
#define ENV_EVENTS_ENABLED   "EVENTS_ENABLED"

#define ENV_EVENT_LIB_NAME  "EVENT_LIB_NAME"
#define NOTIFY_ENTRY_FUNC   "receive_notification_entry"
#define NOTIFY_EXIT_FUNC    "receive_notification_exit"

#define ENV_EVENT_UNIX_SOCKET  "EVENT_UNIX_SOCKET"
// From /usr/include/net-snmp/net-snmp-config-x86_64.h: max length of the
// pathname of a Unix socket in Linux (we don't include
// "net-snmp-config-x86_64.h" here in order not to have a dependency to
// this header file. See as well this number, 108, in definition of the
// "struct sockaddr_un" in Linux: "man 7 unix".)
#define SIZEOF_SOCKADDR_UN_SUN_PATH  108
#define TEMPLATE_FNAME_SOCKADDR      "/tmp/trace_socket_XXXXXX"

#define MAX_RETURN_ADDRESSES_IN_STACK 30

typedef void (*TEntryNotificationSubr)(void *entered_func, void *call_site,
                                       int frames_stack, char ** stack_locs);

typedef void (*TExitNotificationSubr)(void *exited_func, void *call_site);


void instrument_constructor(void)
__attribute__((constructor, no_instrument_function));


void instrument_destructor(void)
__attribute__((destructor, no_instrument_function));


void __cyg_profile_func_enter (void *func, void *call_site)
__attribute__((no_instrument_function));


void __cyg_profile_func_exit (void *func, void *call_site)
__attribute__((no_instrument_function));

