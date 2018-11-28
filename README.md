# prg-events

Notify program-events using GCC instrumentation

# Sample

     ./build.sh
     EVENTS_ENABLED=off ./instrumented-prg
     EVENTS_ENABLED=on  ./instrumented-prg
      
     # TODO: sample of a "my_notification_receiver" shared-library
     export EVENT_LIB_NAME=./my_notification_receiver.so
     EVENTS_ENABLED=on  ./instrumented-prg

