#!/bin/bash
# Wait up to 60 seconds for the Wayland compositor to accept connections.
for i in $(seq 1 60); do
    if python3 -c "
import socket, os
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(os.environ['XDG_RUNTIME_DIR'] + '/wayland-0')
s.close()
" 2>/dev/null; then
        exit 0
    fi
    sleep 1
done
exit 1
