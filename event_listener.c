
#include <stdio.h>

// This example of a trace plugin residing in a shared object does nothing
// different than what the default, built-in notification does: ie., it
// receives the trace notification in this shared object, and doesn't change
// the use of the trace data.

// Note: the "no_instrument_function" attrib is not strictly necessary here,
// since this file is compiled for a shared library, apart from the compilation
// of the instrumented program.

__attribute__((no_instrument_function))
void receive_notification_entry(void *entered_func, void *call_site,
                                int frames_stack, char ** stack_locs) {

    fprintf(stderr, "Function entry: %p %p\n", entered_func, call_site);
    if (stack_locs) {
        fprintf(stderr, "   Stack:\n");

        for (int i=0; i<frames_stack; i++)
            fprintf(stderr, "      %s\n", stack_locs[i]);
    }
}

__attribute__((no_instrument_function))
void receive_notification_exit(void *exited_func, void *call_site) {
    fprintf(stderr, "Function exit: %p %p\n", exited_func, call_site);
}

