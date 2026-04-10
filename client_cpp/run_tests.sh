#!/bin/bash

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}╔══════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║        Zappy Client Test Suite Runner        ║${NC}"
echo -e "${BLUE}╚══════════════════════════════════════════════╝${NC}"

# Parse arguments
PROFILE="debug"
RUN_INTEGRATION=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            PROFILE="release"
            shift
            ;;
        --integration)
            RUN_INTEGRATION=true
            shift
            ;;
        --all)
            RUN_INTEGRATION=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--release] [--integration] [--all]"
            exit 1
            ;;
    esac
done

# Build tests
echo -e "\n${BLUE}[1/3] Building tests [${PROFILE}]...${NC}"
make PROFILE=$PROFILE test

# Run unit tests
echo -e "\n${BLUE}[2/3] Running unit tests...${NC}"
./run_tests_$PROFILE --gtest_filter="*Unit*:*Protocol*:*CommandSender*:*WorldState*"
UNIT_RESULT=$?

INTEGRATION_RESULT=0
if [ "$RUN_INTEGRATION" = true ]; then
    echo -e "\n${BLUE}[3/3] Running integration tests...${NC}"
    ./tests/scripts/run_integration_tests.sh
    INTEGRATION_RESULT=$?
else
    echo -e "\n${YELLOW}[3/3] Skipping integration tests (use --integration to run)${NC}"
fi

# Summary
echo -e "\n${BLUE}══════════════════════════════════════════════${NC}"
echo -e "${BLUE}              Test Summary                     ${NC}"
echo -e "${BLUE}══════════════════════════════════════════════${NC}"

if [ $UNIT_RESULT -eq 0 ]; then
    echo -e "${GREEN}✓ Unit tests passed${NC}"
else
    echo -e "${RED}✗ Unit tests failed${NC}"
fi

if [ "$RUN_INTEGRATION" = true ]; then
    if [ $INTEGRATION_RESULT -eq 0 ]; then
        echo -e "${GREEN}✓ Integration tests passed${NC}"
    else
        echo -e "${RED}✗ Integration tests failed${NC}"
    fi
fi

if [ $UNIT_RESULT -eq 0 ] && [ $INTEGRATION_RESULT -eq 0 ]; then
    echo -e "\n${GREEN}✅ All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}❌ Some tests failed${NC}"
    exit 1
fi