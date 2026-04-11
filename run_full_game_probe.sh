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
TIME_UNIT="${ZAPPY_TIME_UNIT:-100}"
ENABLE_FORK="1"
EASY_ASCENSION="${ZAPPY_EASY_ASCENSION:-0}"
SERVER_CLIENT_SLOTS=""
RESPAWN_DEAD="${ZAPPY_RESPAWN_DEAD:-1}"
LOG_DIR="${ROOT_DIR}/logs/full_probe_$(date +%Y%m%d_%H%M%S)"
STOP_REQUESTED=0

if [ -f "${ENV_FILE}" ]; then
    # shellcheck disable=SC1090
    source "${ENV_FILE}"
    MAX_SECONDS="${ZAPPY_MAX_SECONDS:-$MAX_SECONDS}"
    TEAM_ONE_CLIENTS="${ZAPPY_TEAM_ONE_CLIENTS:-$TEAM_ONE_CLIENTS}"
    TEAM_TWO_CLIENTS="${ZAPPY_TEAM_TWO_CLIENTS:-$TEAM_TWO_CLIENTS}"
    EASY_ASCENSION="${ZAPPY_EASY_ASCENSION:-$EASY_ASCENSION}"
    ENABLE_FORK="${ZAPPY_ENABLE_FORK:-$ENABLE_FORK}"
    SERVER_CLIENT_SLOTS="${ZAPPY_SERVER_CLIENT_SLOTS:-$SERVER_CLIENT_SLOTS}"
    RESPAWN_DEAD="${ZAPPY_RESPAWN_DEAD:-$RESPAWN_DEAD}"
fi

usage() {
    cat <<USG
Usage: ./run_full_game_probe.sh [options]

Options:
  --port N                 Server port (default: 8674)
  --max-seconds N          Max session duration (default: 900)
  --team1-clients N        Team1 client count (default: 12)
  --team2-clients N        Team2 client count (default: 12)
  --target-level N         Client target level (default: 8)
  --time-unit N            Export ZAPPY_TIME_UNIT for server (default: 100)
    --server-slots N         Server total player slots (-c N). Auto-calculated by default
  --easy-ascension         Export ZAPPY_EASY_ASCENSION=1 for server
    --fork                   Enable --fork mode on clients
  --no-fork                Disable --fork mode on clients
    --respawn                Keep replacing dead clients (default)
    --no-respawn             Do not replace dead clients after initial wave
  --help                   Show this help

Notes:
  - Winner condition on server is 6 players at level 8 on the same team.
  - This script launches enough clients dynamically to replace dead ones and test a full game session safely.
USG
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --max-seconds) MAX_SECONDS="$2"; shift 2 ;;
        --team1-clients) TEAM_ONE_CLIENTS="$2"; shift 2 ;;
        --team2-clients) TEAM_TWO_CLIENTS="$2"; shift 2 ;;
        --target-level) TARGET_LEVEL="$2"; shift 2 ;;
        --time-unit) TIME_UNIT="$2"; shift 2 ;;
        --server-slots) SERVER_CLIENT_SLOTS="$2"; shift 2 ;;
        --easy-ascension) EASY_ASCENSION="1"; shift ;;
        --fork) ENABLE_FORK="1"; shift ;;
        --no-fork) ENABLE_FORK="0"; shift ;;
        --respawn) RESPAWN_DEAD="1"; shift ;;
        --no-respawn) RESPAWN_DEAD="0"; shift ;;
        --help|-h) usage; exit 0 ;;
        *) echo -e "${RED}Unknown option: $1${NC}"; usage; exit 1 ;;
    esac
done

if ! [[ "$PORT" =~ ^[0-9]+$ ]] || [ "$PORT" -lt 1 ] || [ "$PORT" -gt 65535 ]; then
    echo -e "${RED}Invalid --port value: ${PORT}${NC}"
    exit 1
fi

for n in "$MAX_SECONDS" "$TEAM_ONE_CLIENTS" "$TEAM_TWO_CLIENTS" "$TARGET_LEVEL" "$TIME_UNIT" "$EASY_ASCENSION" "$RESPAWN_DEAD"; do
    if ! [[ "$n" =~ ^[0-9]+$ ]]; then
        echo -e "${RED}Expected numeric value, got: ${n}${NC}"
        exit 1
    fi
done

if [ -z "$SERVER_CLIENT_SLOTS" ]; then
    if [ "$ENABLE_FORK" = "1" ]; then
        SERVER_CLIENT_SLOTS=$(((TEAM_ONE_CLIENTS + TEAM_TWO_CLIENTS) * 2))
    else
        SERVER_CLIENT_SLOTS=$((TEAM_ONE_CLIENTS + TEAM_TWO_CLIENTS))
    fi
fi

if ! [[ "$SERVER_CLIENT_SLOTS" =~ ^[0-9]+$ ]] || [ "$SERVER_CLIENT_SLOTS" -lt 2 ]; then
    echo -e "${RED}Invalid --server-slots value: ${SERVER_CLIENT_SLOTS}${NC}"
    exit 1
fi

mkdir -p "$LOG_DIR"
SERVER_LOG="${LOG_DIR}/server.log"
SERVER_GAME_LOG="${SERVER_DIR}/log.txt"
RUN_LOG="${LOG_DIR}/runner.log"
PIDS_FILE="${LOG_DIR}/client_pids.txt"

echo "" > "$PIDS_FILE"

cleanup() {
    set +e
    STOP_REQUESTED=1
    echo -e "\n${YELLOW}Cleaning up processes...${NC}" | tee -a "$RUN_LOG"
    if [ -f "$PIDS_FILE" ]; then
        while read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                kill "$pid" 2>/dev/null || true
            fi
        done < "$PIDS_FILE"
    fi

    # Kill the background maintainers and specific process trees
    pkill -P $$ 2>/dev/null || true
    pkill -f "${SERVER_DIR}/zappy ${PORT}" 2>/dev/null || true
    pkill -f "${CLIENT_DIR}/client localhost ${PORT}" 2>/dev/null || true
}

on_signal() {
    echo -e "\n${YELLOW}Signal received, stopping probe...${NC}" | tee -a "$RUN_LOG"
    cleanup
    exit 130
}

trap cleanup EXIT
trap on_signal INT TERM

echo -e "${BLUE}=== Zappy Full Winner Probe ===${NC}" | tee -a "$RUN_LOG"
echo "Log directory: ${LOG_DIR}" | tee -a "$RUN_LOG"
echo "Port=${PORT} Team1=${TEAM_ONE_CLIENTS} Team2=${TEAM_TWO_CLIENTS} TargetLevel=${TARGET_LEVEL} MaxSeconds=${MAX_SECONDS}" | tee -a "$RUN_LOG"
echo "TimeUnit=${TIME_UNIT} EasyAscension=${EASY_ASCENSION} Fork=${ENABLE_FORK} ServerSlots=${SERVER_CLIENT_SLOTS} RespawnDead=${RESPAWN_DEAD}" | tee -a "$RUN_LOG"

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
    exec ./zappy "$PORT" -n team1 team2 -c "$SERVER_CLIENT_SLOTS" > "$SERVER_LOG" 2>&1
) &
SERVER_PID=$!

sleep 1
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo -e "${RED}Server failed to start. See ${SERVER_LOG}${NC}" | tee -a "$RUN_LOG"
    exit 3
fi

echo -e "${YELLOW}Resuming server time API...${NC}" | tee -a "$RUN_LOG"
for _ in $(seq 1 40); do
    if grep -q "MAIN_LOOP" "$SERVER_LOG" 2>/dev/null; then
        break
    fi
    sleep 0.1
done
if kill -USR1 "$SERVER_PID" 2>/dev/null; then
    echo "Sent SIGUSR1 to server pid ${SERVER_PID}" | tee -a "$RUN_LOG"
else
    echo -e "${RED}Failed to send SIGUSR1 to server pid ${SERVER_PID}${NC}" | tee -a "$RUN_LOG"
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

maintain_clients_in_bg() {
    local team="$1"
    local max_concurrent="$2"
    local total_spawned=0
    local max_spawn_total=150

    if [ "$RESPAWN_DEAD" = "0" ]; then
        max_spawn_total="$max_concurrent"
    fi

    while [ "$STOP_REQUESTED" -eq 0 ] && [ "$total_spawned" -lt "$max_spawn_total" ]; do
        # Count currently running for this team
        local alive=$(pgrep -f "client localhost ${PORT} ${team}" | wc -l)
        if [ "$alive" -lt "$max_concurrent" ]; then
            total_spawned=$((total_spawned + 1))
            start_client "$team" "$total_spawned"
            sleep 1 # wait a moment to not overload if slot rejected immediately
        else
            sleep 3
        fi
    done
}

echo -e "${YELLOW}Launching clients dynamically in the background...${NC}" | tee -a "$RUN_LOG"
maintain_clients_in_bg "team1" "$TEAM_ONE_CLIENTS" &
maintain_clients_in_bg "team2" "$TEAM_TWO_CLIENTS" &

echo -e "${GREEN}Clients launcher active. Monitoring winner condition...${NC}" | tee -a "$RUN_LOG"
START_TS=$(date +%s)
WINNER_LINE=""
HEARTBEAT_INTERVAL=10
NEXT_HEARTBEAT=10

count_level_players() {
    local level="$1"
    grep -l "LEVEL UP .*${level}" "$LOG_DIR"/client_*.log 2>/dev/null | wc -l
}

format_level_counts() {
    local out=""
    local level
    for level in 2 3 4 5 6 7 8; do
        local count
        count=$(count_level_players "$level")
        out+=" lvl${level}_players=${count}"
    done
    echo "$out"
}

while true; do
    NOW_TS=$(date +%s)
    ELAPSED=$((NOW_TS - START_TS))

    if grep -q "Winner condition reached" "$SERVER_LOG" 2>/dev/null || grep -q "Winner condition reached" "$SERVER_GAME_LOG" 2>/dev/null; then
        WINNER_LINE=$( (grep "Winner condition reached" "$SERVER_GAME_LOG" 2>/dev/null; grep "Winner condition reached" "$SERVER_LOG" 2>/dev/null) | tail -1 )
        echo -e "${GREEN}Winner detected after ${ELAPSED}s${NC}" | tee -a "$RUN_LOG"
        echo "$WINNER_LINE" | tee -a "$RUN_LOG"
        exit 0
    fi

    if [ "$ELAPSED" -ge "$MAX_SECONDS" ]; then
        echo -e "${RED}Timeout after ${ELAPSED}s without winner condition.${NC}" | tee -a "$RUN_LOG"
        LEVEL_UP_EVENTS=$(grep -h "LEVEL UP" "$LOG_DIR"/client_*.log 2>/dev/null | wc -l)
        LEVEL_COUNTS=$(format_level_counts)
        DEATH_EVENTS=$(grep -h "Player died" "$LOG_DIR"/client_*.log 2>/dev/null | wc -l)
        INCANT_KO_EVENTS=$(grep -h "incantation failed" "$LOG_DIR"/client_*.log 2>/dev/null | wc -l)
        echo "Probe summary: level_up_events=${LEVEL_UP_EVENTS}${LEVEL_COUNTS} death_events=${DEATH_EVENTS} incantation_ko=${INCANT_KO_EVENTS}" | tee -a "$RUN_LOG"
        echo "Last server game-log lines:" | tee -a "$RUN_LOG"
        tail -40 "$SERVER_GAME_LOG" 2>/dev/null | tee -a "$RUN_LOG"
        exit 5
    fi

    if [ "$ELAPSED" -ge "$NEXT_HEARTBEAT" ]; then
        ALIVE_TOTAL=$(pgrep -f "client localhost ${PORT}" | wc -l)
        LEVEL_COUNTS=$(format_level_counts)
        DEATH_EVENTS=$(grep -h "Player died" "$LOG_DIR"/client_*.log 2>/dev/null | wc -l)
        echo "[${ELAPSED}s] probing... alive=${ALIVE_TOTAL}${LEVEL_COUNTS} deaths=${DEATH_EVENTS}" | tee -a "$RUN_LOG"
        NEXT_HEARTBEAT=$((ELAPSED + HEARTBEAT_INTERVAL))

        if [ "$ALIVE_TOTAL" -eq 0 ]; then
            # Delay check in case maintainer is just booting them
            sleep 2
            if [ "$(pgrep -f "client localhost ${PORT}" | wc -l)" -eq 0 ]; then
                echo -e "${RED}All clients have exited heavily before winner condition.${NC}" | tee -a "$RUN_LOG"
            fi
        fi
    fi

    sleep 1
done
