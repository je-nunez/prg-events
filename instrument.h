
#define ENV_EVENTS_ENABLED   "EVENTS_ENABLED"

#define ENV_EVENT_LIB_NAME  "EVENT_LIB_NAME"
#define NOTIFY_ENTRY_FUNC   "receive_notification_entry"
#define NOTIFY_EXIT_FUNC    "receive_notification_exit"

#define MAX_RETURN_ADDRESSES_IN_STACK 30

void instrument_constructor(void)
                 __attribute__((constructor, no_instrument_function));


void instrument_destructor(void)
                 __attribute__((destructor, no_instrument_function));


void __cyg_profile_func_enter (void *func, void *call_site)
                __attribute__((no_instrument_function));


void __cyg_profile_func_exit (void *func, void *call_site)
                __attribute__((no_instrument_function));

