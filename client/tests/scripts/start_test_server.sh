#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SERVER_PATH="$PROJECT_ROOT/../server"

# Kill any existing server
pkill zappy 2>/dev/null || true
sleep 1

# Start server
cd "$SERVER_PATH"
./zappy 8674 > /tmp/zappy_test_server.log 2>&1 &
SERVER_PID=$!

# Wait for server to be ready
sleep 2

# Server requires SIGUSR1 to resume its time API
./run.sh >/tmp/zappy_test_server_resume.log 2>&1 || true

# Check if server is running
if kill -0 $SERVER_PID 2>/dev/null; then
    echo "Server started with PID: $SERVER_PID"
    echo $SERVER_PID > /tmp/zappy_test_server.pid
    exit 0
else
    echo "Failed to start server"
    exit 1
fi