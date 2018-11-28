
#TODO: convert to a Makefile

gcc -finstrument-functions test-program.c instrument.c -rdynamic -Wall -ldl \
    -o instrumented-prg

