
#TODO: convert to a Makefile

gcc -finstrument-functions test-program.c instrument.c -rdynamic -Wall -ldl \
    -o instrumented-prg

# Build the shared library to receive the notification events:

gcc -shared -fPIC -o event_listener.so event_listener.c -Wall -g

