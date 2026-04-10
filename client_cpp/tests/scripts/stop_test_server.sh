#!/bin/bash

if [ -f /tmp/zappy_test_server.pid ]; then
    PID=$(cat /tmp/zappy_test_server.pid)
    if kill -0 $PID 2>/dev/null; then
        kill $PID
        echo "Server stopped (PID: $PID)"
    fi
    rm /tmp/zappy_test_server.pid
fi

# Cleanup any remaining servers
pkill zappy 2>/dev/null || true