# prg-events

Notify program-events using GCC instrumentation

# Sample

     ./build.sh
     EVENTS_ENABLED=off ./instrumented-prg
     EVENTS_ENABLED=on  ./instrumented-prg
      
     # TODO: sample of a "my_notification_receiver" shared-library
     export EVENT_LIB_NAME=./my_notification_receiver.so
     EVENTS_ENABLED=on  ./instrumented-prg

# Note:

There are other solutions to get events from a program in Linux without
instrumenting the program via GCC, e.g., like by
[uprobes](https://www.kernel.org/doc/Documentation/trace/uprobetracer.txt);
or via [perf probe](http://man7.org/linux/man-pages/man1/perf-probe.1.html)
  (an example [here](https://github.com/je-nunez/Bash_library_for_Dynamic_Tracing_in_Linux_using_Perf_Events));
SystemTap; GDB tracepoints; etc.)

