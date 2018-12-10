#!/bin/sh

# Requires the SOCAT utility

IPC_UNIX_SOCKET=/tmp/ipc_receiver_socket

echo "Use EVENT_UNIX_SOCKET=$IPC_UNIX_SOCKET for the instrumentation."

socat -u UNIX-RECV:"$IPC_UNIX_SOCKET"  STDOUT

