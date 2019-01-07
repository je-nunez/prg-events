# prg-events

Notify program-events using GCC instrumentation

# Note:

The code of the GCC instrumentation is in `instrument.[ch]` files:
`test-program.c` is just a sample program to which GCC adds the
instrumentation, and `event_listener.c` is a demo of shared-library
to receive events from the instrumentation. (The other communication
mechanism, without using shared-library, is to notify events through
an IPC Unix datagram socket: this is done in `instrument.c`.)

There are other solutions to get events from a program in Linux without
instrumenting the program via GCC, e.g., like by
[uprobes](https://www.kernel.org/doc/Documentation/trace/uprobetracer.txt);
or via [perf probe](http://man7.org/linux/man-pages/man1/perf-probe.1.html)
  (an example [here](https://github.com/je-nunez/Bash_library_for_Dynamic_Tracing_in_Linux_using_Perf_Events));
SystemTap; GDB tracepoints; etc.)

Another approach to instrumentation via GCC is the
[Aspect-oriented instrumentation with GCC](https://dl.acm.org/citation.cfm?id=1939433)
(a later version of this document is available
[here](http://www.fsl.cs.stonybrook.edu/docs/interaspect-fmsd12/interaspect-fmsd12.pdf).)

# Sample

     ./build.sh
     EVENTS_ENABLED=off ./instrumented-prg
     EVENTS_ENABLED=on  ./instrumented-prg
      
     # This is the shared-library that received the trace notification events
     export EVENT_LIB_NAME=./event_listener.so
     # Run the program now:
     EVENTS_ENABLED=on  ./instrumented-prg
      
     # To receive through IPC (Unix sockets, datagrams) the trace notification
     # events:
     # In one terminal, run the demo IPC server (needs "socat" installed, and
     # opens a Unix socket at "/tmp/ipc_receiver_socket"):
     sh ipc_receiver_demo.sh
     # In another terminal:
     export EVENT_UNIX_SOCKET=/tmp/ipc_receiver_socket
     EVENTS_ENABLED=on  ./instrumented-prg

