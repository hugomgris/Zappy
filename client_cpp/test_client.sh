#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}=== Zappy Client Test Script ===${NC}"

# Check if client exists
if [ ! -f "./client" ]; then
    echo -e "${RED}Error: Client binary not found. Run 'make' first.${NC}"
    exit 1
fi

# Default values
HOST="localhost"
PORT="8674"
TEAM="team1"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --host) HOST="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --team) TEAM="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

echo -e "${GREEN}Testing connection to $HOST:$PORT with team '$TEAM'${NC}"

# Test 1: Basic connection
echo -e "\n${YELLOW}Test 1: Basic connection${NC}"
timeout 5s ./client "$HOST" "$PORT" "$TEAM" --debug 2>&1 | head -20

if [ ${PIPESTATUS[0]} -eq 124 ]; then
    echo -e "${GREEN}✓ Connection successful (timed out as expected)${NC}"
else
    echo -e "${RED}✗ Connection failed${NC}"
fi

# Test 2: Full run with fork enabled
echo -e "\n${YELLOW}Test 2: Full run with fork enabled (30 seconds)${NC}"
timeout 30s ./client "$HOST" "$PORT" "$TEAM" --fork --target-level 2 2>&1 | grep -E "(LEVEL UP|Fork|Status:|VICTORY)" || true

echo -e "\n${GREEN}Tests complete!${NC}"