#!/usr/bin/env bash

set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="${ROOT_DIR}/server"
CLIENT_DIR="${ROOT_DIR}/client_cpp"
ENV_FILE="${ROOT_DIR}/run_session.local.env"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PORT="8674"
TARGET_LEVEL="8"
MAX_SECONDS="${ZAPPY_MAX_SECONDS:-900}"
TEAM_ONE_CLIENTS="${ZAPPY_TEAM_ONE_CLIENTS:-12}"
TEAM_TWO_CLIENTS="${ZAPPY_TEAM_TWO_CLIENTS:-12}"
TIME_UNIT="${ZAPPY_TIME_UNIT:-7}"
ENABLE_FORK="1"
EASY_ASCENSION="${ZAPPY_EASY_ASCENSION:-0}"
LOG_DIR="${ROOT_DIR}/logs/full_probe_$(date +%Y%m%d_%H%M%S)"
STOP_REQUESTED=0

if [ -f "${ENV_FILE}" ]; then
    # shellcheck disable=SC1090
    source "${ENV_FILE}"
    MAX_SECONDS="${ZAPPY_MAX_SECONDS:-$MAX_SECONDS}"
    TEAM_ONE_CLIENTS="${ZAPPY_TEAM_ONE_CLIENTS:-$TEAM_ONE_CLIENTS}"
    TEAM_TWO_CLIENTS="${ZAPPY_TEAM_TWO_CLIENTS:-$TEAM_TWO_CLIENTS}"
    EASY_ASCENSION="${ZAPPY_EASY_ASCENSION:-$EASY_ASCENSION}"
fi

usage() {
    cat <<EOF
Usage: ./run_full_game_probe.sh [options]

Options:
  --port N                 Server port (default: 8674)
  --max-seconds N          Max session duration (default: 900)
  --team1-clients N        Team1 client count (default: 12)
  --team2-clients N        Team2 client count (default: 12)
  --target-level N         Client target level (default: 8)
  --time-unit N            Export ZAPPY_TIME_UNIT for server (default: 7)
    --easy-ascension         Export ZAPPY_EASY_ASCENSION=1 for server
  --no-fork                Disable --fork mode on clients
  --help                   Show this help

Notes:
  - Winner condition on server is 6 players at level 8 on the same team.
  - This script launches enough clients to make a true winner-ending run feasible.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port)
            PORT="$2"
            shift 2
            ;;
        --max-seconds)
            MAX_SECONDS="$2"
            shift 2
            ;;
        --team1-clients)
            TEAM_ONE_CLIENTS="$2"
            shift 2
            ;;
        --team2-clients)
            TEAM_TWO_CLIENTS="$2"
            shift 2
            ;;
        --target-level)
            TARGET_LEVEL="$2"
            shift 2
            ;;
        --time-unit)
            TIME_UNIT="$2"
            shift 2
            ;;
        --easy-ascension)
            EASY_ASCENSION="1"
            shift
            ;;
        --no-fork)
            ENABLE_FORK="0"
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            usage
            exit 1
            ;;
    esac
done

if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
    echo -e "${RED}Invalid --port value: ${PORT}${NC}"
    exit 1
fi

for n in "$MAX_SECONDS" "$TEAM_ONE_CLIENTS" "$TEAM_TWO_CLIENTS" "$TARGET_LEVEL" "$TIME_UNIT" "$EASY_ASCENSION"; do
    if ! [[ "$n" =~ ^[0-9]+$ ]]; then
        echo -e "${RED}Expected numeric value, got: ${n}${NC}"
        exit 1
    fi
done

mkdir -p "$LOG_DIR"
SERVER_LOG="${LOG_DIR}/server.log"
SERVER_GAME_LOG="${SERVER_DIR}/log.txt"
RUN_LOG="${LOG_DIR}/runner.log"
PIDS_FILE="${LOG_DIR}/client_pids.txt"

echo "" > "$PIDS_FILE"

cleanup() {
    set +e
    echo -e "\n${YELLOW}Cleaning up processes...${NC}" | tee -a "$RUN_LOG"
    if [ -f "$PIDS_FILE" ]; then
        while read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                kill "$pid" 2>/dev/null || true
            fi
        done < "$PIDS_FILE"
    fi

    pkill -f "${SERVER_DIR}/zappy ${PORT}" 2>/dev/null || true
    pkill -f "${CLIENT_DIR}/client localhost ${PORT}" 2>/dev/null || true
}

on_signal() {
    STOP_REQUESTED=1
    echo -e "\n${YELLOW}Signal received, stopping probe...${NC}" | tee -a "$RUN_LOG"
}

trap cleanup EXIT
trap on_signal INT TERM

echo -e "${BLUE}=== Zappy Full Winner Probe ===${NC}" | tee -a "$RUN_LOG"
echo "Log directory: ${LOG_DIR}" | tee -a "$RUN_LOG"
echo "Port=${PORT} Team1=${TEAM_ONE_CLIENTS} Team2=${TEAM_TWO_CLIENTS} TargetLevel=${TARGET_LEVEL} MaxSeconds=${MAX_SECONDS}" | tee -a "$RUN_LOG"
echo "TimeUnit=${TIME_UNIT} EasyAscension=${EASY_ASCENSION} Fork=${ENABLE_FORK}" | tee -a "$RUN_LOG"

echo -e "${YELLOW}Building server and client...${NC}" | tee -a "$RUN_LOG"
if ! (cd "$SERVER_DIR" && make >/dev/null); then
    echo -e "${RED}Server build failed.${NC}" | tee -a "$RUN_LOG"
    exit 2
fi
if ! (cd "$CLIENT_DIR" && make >/dev/null); then
    echo -e "${RED}Client build failed.${NC}" | tee -a "$RUN_LOG"
    exit 2
fi

pkill zappy 2>/dev/null || true
sleep 1

echo -e "${YELLOW}Starting server on port ${PORT}...${NC}" | tee -a "$RUN_LOG"
(
    cd "$SERVER_DIR" || exit 1
    export ZAPPY_TIME_UNIT="$TIME_UNIT"
    export ZAPPY_EASY_ASCENSION="$EASY_ASCENSION"
    ./zappy "$PORT" > "$SERVER_LOG" 2>&1
) &
SERVER_PID=$!

sleep 1
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo -e "${RED}Server failed to start. See ${SERVER_LOG}${NC}" | tee -a "$RUN_LOG"
    exit 3
fi

echo -e "${YELLOW}Resuming server time API...${NC}" | tee -a "$RUN_LOG"
if ! (cd "$SERVER_DIR" && ./run.sh >> "$RUN_LOG" 2>&1); then
    echo -e "${RED}Failed to resume server time API via server/run.sh${NC}" | tee -a "$RUN_LOG"
    exit 4
fi

start_client() {
    local team="$1"
    local idx="$2"
    local logfile="${LOG_DIR}/client_${team}_${idx}.log"
    local args=("localhost" "$PORT" "$team" "--target-level" "$TARGET_LEVEL")
    if [ "$ENABLE_FORK" = "1" ]; then
        args+=("--fork")
    fi

    (
        cd "$CLIENT_DIR" || exit 1
        export ZAPPY_EASY_ASCENSION="$EASY_ASCENSION"
        ./client "${args[@]}" > "$logfile" 2>&1
    ) &
    echo "$!" >> "$PIDS_FILE"
}

echo -e "${YELLOW}Launching clients...${NC}" | tee -a "$RUN_LOG"
for i in $(seq 1 "$TEAM_ONE_CLIENTS"); do
    start_client "team1" "$i"
    sleep 0.1
done
for i in $(seq 1 "$TEAM_TWO_CLIENTS"); do
    start_client "team2" "$i"
    sleep 0.1
done

echo -e "${GREEN}All clients launched. Monitoring winner condition...${NC}" | tee -a "$RUN_LOG"
START_TS=$(date +%s)
WINNER_LINE=""
HEARTBEAT_INTERVAL=30
NEXT_HEARTBEAT=$HEARTBEAT_INTERVAL

while true; do
    NOW_TS=$(date +%s)
    ELAPSED=$((NOW_TS - START_TS))

    if [ "$STOP_REQUESTED" -eq 1 ]; then
        echo -e "${YELLOW}Probe interrupted by user.${NC}" | tee -a "$RUN_LOG"
        exit 130
    fi

    if grep -q "Winner condition reached" "$SERVER_LOG" 2>/dev/null || grep -q "Winner condition reached" "$SERVER_GAME_LOG" 2>/dev/null; then
        WINNER_LINE=$( (grep "Winner condition reached" "$SERVER_GAME_LOG" 2>/dev/null; grep "Winner condition reached" "$SERVER_LOG" 2>/dev/null) | tail -1 )
        echo -e "${GREEN}Winner detected after ${ELAPSED}s${NC}" | tee -a "$RUN_LOG"
        echo "$WINNER_LINE" | tee -a "$RUN_LOG"
        exit 0
    fi

    if [ "$ELAPSED" -ge "$MAX_SECONDS" ]; then
        echo -e "${RED}Timeout after ${ELAPSED}s without winner condition.${NC}" | tee -a "$RUN_LOG"
        echo "Last server output lines:" | tee -a "$RUN_LOG"
        tail -40 "$SERVER_LOG" | tee -a "$RUN_LOG"
        if [ -f "$SERVER_GAME_LOG" ]; then
            echo "Last server game-log lines:" | tee -a "$RUN_LOG"
            tail -40 "$SERVER_GAME_LOG" | tee -a "$RUN_LOG"
        fi
        exit 5
    fi

    if [ "$ELAPSED" -ge "$NEXT_HEARTBEAT" ]; then
        ALIVE_CLIENTS=0
        while read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                ALIVE_CLIENTS=$((ALIVE_CLIENTS + 1))
            fi
        done < "$PIDS_FILE"
        echo "[${ELAPSED}s] probing... configured clients=${ALIVE_CLIENTS}" | tee -a "$RUN_LOG"
        NEXT_HEARTBEAT=$((NEXT_HEARTBEAT + HEARTBEAT_INTERVAL))

        if [ "$ALIVE_CLIENTS" -eq 0 ]; then
            echo -e "${RED}All clients have exited before winner condition.${NC}" | tee -a "$RUN_LOG"
            exit 6
        fi
    fi

    sleep 2
done
