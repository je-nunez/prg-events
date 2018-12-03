
#include <stdio.h>

// Note: the "no_instrument_function" attrib is not strictly necessary here,
// since this file is compiled for a shared library, apart from the compilation
// of the instrumented program.

__attribute__((no_instrument_function))
void receive_notification_entry(void *entered_func, void *call_site,
                                int frames_stack, char ** stack_locs) {
    for (int i=0; i<frames_stack; i++)
        fprintf(stderr, "      %s\n", stack_locs[i]);
}

__attribute__((no_instrument_function))
void receive_notification_exit(void *exited_func, void *call_site) {
    fprintf(stderr, "Function exit: %p %p\n", exited_func, call_site);
}

