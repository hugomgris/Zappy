#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${YELLOW}=== Running Integration Tests ===${NC}"

# Start test server
echo "Starting test server..."
bash "$SCRIPT_DIR/start_test_server.sh"

# Run integration tests
echo "Running integration tests..."
cd "$PROJECT_ROOT"

# Run only integration tests
./run_tests_debug --gtest_filter="*Integration*:*AI*"
TEST_RESULT=$?

# Stop server
echo "Stopping test server..."
bash "$SCRIPT_DIR/stop_test_server.sh"

if [ $TEST_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ All integration tests passed${NC}"
else
    echo -e "${RED}✗ Some integration tests failed${NC}"
fi

exit $TEST_RESULT