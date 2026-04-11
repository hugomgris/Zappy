#!/usr/bin/env bash
# probe.sh — Zappy full-game probe
#
# Launches server + clients, monitors progress, writes per-client logs
# and a single shareable session summary when the session ends.
#
# Usage:  ./probe.sh [options]
#   --port N              Server port              (default: 8674)
#   --team1 N             Team1 concurrent clients (default: 12)
#   --team2 N             Team2 concurrent clients (default: 12)
#   --slots N             Server player slots      (default: auto)
#   --time-unit N         Server time unit         (default: 100)
#   --max-seconds N       Session timeout          (default: 900)
#   --easy-ascension      Enable ZAPPY_EASY_ASCENSION=1 on server
#   --no-fork             Disable client forking   (default: forking ON)
#   --no-respawn          Don't replace dead clients
#   --debug               Pass --debug to clients (verbose logs)
#   --help

set -uo pipefail

# ── Paths ─────────────────────────────────────────────────────────────────────
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="${ROOT_DIR}/server"
CLIENT_DIR="${ROOT_DIR}/client"

# ── Defaults ──────────────────────────────────────────────────────────────────
PORT=8674
TEAM1_N=12
TEAM2_N=12
SLOTS=""           # auto-calculated below
TIME_UNIT=100
MAX_SECONDS=900
EASY_ASCENSION=0
FORK_ENABLED=1
RESPAWN=1
DEBUG_CLIENTS=0

# ── Colours ───────────────────────────────────────────────────────────────────
C_GREEN='\033[0;32m'
C_RED='\033[0;31m'
C_YELLOW='\033[1;33m'
C_BLUE='\033[0;34m'
C_CYAN='\033[0;36m'
C_BOLD='\033[1m'
C_NC='\033[0m'

# ── Argument parsing ──────────────────────────────────────────────────────────
usage() {
    cat <<EOF
Usage: ./probe.sh [options]

  --port N              Server port              (default: 8674)
  --team1 N             Team1 concurrent clients (default: 12)
  --team2 N             Team2 concurrent clients (default: 12)
  --slots N             Server player slots      (default: auto)
  --time-unit N         Server time unit         (default: 100)
  --max-seconds N       Session timeout in s     (default: 900)
  --easy-ascension      Enable ZAPPY_EASY_ASCENSION=1 on server
  --no-fork             Disable client forking   (default: forking ON)
  --no-respawn          Do not replace dead clients after initial wave
  --debug               Pass --debug flag to every client (very verbose)
  --help                Show this help

Examples:
  ./probe.sh                                        # standard 12v12 game
  ./probe.sh --easy-ascension --max-seconds 120     # fast sanity check
  ./probe.sh --team1 6 --team2 6 --slots 24 --no-fork
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --port)           PORT="$2";         shift 2 ;;
        --team1)          TEAM1_N="$2";      shift 2 ;;
        --team2)          TEAM2_N="$2";      shift 2 ;;
        --slots)          SLOTS="$2";        shift 2 ;;
        --time-unit)      TIME_UNIT="$2";    shift 2 ;;
        --max-seconds)    MAX_SECONDS="$2";  shift 2 ;;
        --easy-ascension) EASY_ASCENSION=1;  shift   ;;
        --no-fork)        FORK_ENABLED=0;    shift   ;;
        --no-respawn)     RESPAWN=0;         shift   ;;
        --debug)          DEBUG_CLIENTS=1;   shift   ;;
        --help|-h)        usage; exit 0      ;;
        *) echo -e "${C_RED}Unknown option: $1${C_NC}"; usage; exit 1 ;;
    esac
done

# Auto-calculate server slots:
#   base = team1 + team2
#   if forking: multiply by 3 so forked children have room
#   minimum 24
if [[ -z "$SLOTS" ]]; then
    BASE=$(( TEAM1_N + TEAM2_N ))
    if [[ "$FORK_ENABLED" -eq 1 ]]; then
        SLOTS=$(( BASE * 3 ))
    else
        SLOTS=$BASE
    fi
    [[ "$SLOTS" -lt 24 ]] && SLOTS=24
fi

# ── Log directory ─────────────────────────────────────────────────────────────
SESSION_TS="$(date +%Y%m%d_%H%M%S)"
LOG_DIR="${ROOT_DIR}/logs/probe_${SESSION_TS}"
mkdir -p "$LOG_DIR"

SERVER_LOG="${LOG_DIR}/server.log"
RUNNER_LOG="${LOG_DIR}/runner.log"
SUMMARY_FILE="${LOG_DIR}/SUMMARY.txt"
PIDS_FILE="${LOG_DIR}/.client_pids"
> "$PIDS_FILE"

# ── Logging helpers ───────────────────────────────────────────────────────────
log()  { echo -e "$*" | tee -a "$RUNNER_LOG"; }
info() { log "${C_BLUE}[INFO]${C_NC}  $*"; }
ok()   { log "${C_GREEN}[OK]${C_NC}    $*"; }
warn() { log "${C_YELLOW}[WARN]${C_NC}  $*"; }
err()  { log "${C_RED}[ERR]${C_NC}   $*"; }
hdr()  { log "\n${C_BOLD}${C_CYAN}=== $* ===${C_NC}"; }

# ── Shared state ──────────────────────────────────────────────────────────────
STOP_REQUESTED=0
SERVER_PID=""
START_TS=""

# ── Cleanup ───────────────────────────────────────────────────────────────────
cleanup() {
    set +e
    STOP_REQUESTED=1

    # Kill all tracked client PIDs
    if [[ -f "$PIDS_FILE" ]]; then
        while read -r pid; do
            [[ -n "$pid" ]] && kill "$pid" 2>/dev/null || true
        done < "$PIDS_FILE"
    fi

    # Kill background maintainer subshells spawned by this script
    pkill -P $$ 2>/dev/null || true

    # Kill the server
    if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

    # Belt-and-suspenders: kill any stray processes matching our port
    pkill -f "zappy ${PORT}" 2>/dev/null || true
    pkill -f "client localhost ${PORT}" 2>/dev/null || true
}

on_signal() {
    warn "Signal received — stopping probe."
    write_summary "interrupted"
    cleanup
    exit 130
}

trap cleanup EXIT
trap on_signal INT TERM

# ── Build ─────────────────────────────────────────────────────────────────────
hdr "Build"

info "Building server..."
if ! (cd "$SERVER_DIR" && make 2>&1 | tee -a "$RUNNER_LOG" | tail -3); then
    err "Server build failed. Aborting."
    exit 2
fi
ok "Server built."

info "Building client..."
if ! (cd "$CLIENT_DIR" && make 2>&1 | tee -a "$RUNNER_LOG" | tail -3); then
    err "Client build failed. Aborting."
    exit 2
fi
ok "Client built."

# ── Kill stale processes ──────────────────────────────────────────────────────
pkill -f "zappy ${PORT}" 2>/dev/null || true
sleep 0.5

# ── Start server ──────────────────────────────────────────────────────────────
hdr "Server"

info "Starting server on port ${PORT} (slots=${SLOTS}, time-unit=${TIME_UNIT}, easy=${EASY_ASCENSION})..."

(
    cd "$SERVER_DIR" || exit 1
    export ZAPPY_TIME_UNIT="$TIME_UNIT"
    export ZAPPY_EASY_ASCENSION="$EASY_ASCENSION"
    exec ./zappy "${PORT}" -n team1 team2 -c "${SLOTS}" >> "$SERVER_LOG" 2>&1
) &
SERVER_PID=$!

# Wait up to 3 s for server to start
for i in $(seq 1 30); do
    sleep 0.1
    kill -0 "$SERVER_PID" 2>/dev/null && break
done

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    err "Server exited immediately. Check ${SERVER_LOG}"
    exit 3
fi
ok "Server running (pid=${SERVER_PID})."

# ── Resume server time API ────────────────────────────────────────────────────
info "Waiting for server MAIN_LOOP before resuming time API..."
for i in $(seq 1 50); do
    grep -q "MAIN_LOOP\|Listening\|ready\|started" "$SERVER_LOG" 2>/dev/null && break
    sleep 0.2
done

# Try run.sh first (your server's documented way), fall back to SIGUSR1
if [[ -x "${SERVER_DIR}/run.sh" ]]; then
    info "Calling server/run.sh..."
    (cd "$SERVER_DIR" && bash run.sh) >> "$RUNNER_LOG" 2>&1 || true
else
    # Only send SIGUSR1 if run.sh doesn't exist
    if kill -USR1 "$SERVER_PID" 2>/dev/null; then
        ok "Sent SIGUSR1 to server (pid=${SERVER_PID}) — time API resumed."
    else
        warn "SIGUSR1 delivery failed — server may start time automatically."
    fi
fi

sleep 0.5

# ── Client launcher ───────────────────────────────────────────────────────────
hdr "Clients"

# Build client arg list matching the new main.cpp interface:
#   ./client <host> <port> <team> [--no-fork] [--debug]
build_client_args() {
    local team="$1"
    local args=("localhost" "$PORT" "$team")
    [[ "$FORK_ENABLED" -eq 0 ]] && args+=("--no-fork")
    [[ "$DEBUG_CLIENTS" -eq 1 ]] && args+=("--debug")
    echo "${args[@]}"
}

CLIENT_COUNTER=0  # global across both teams for unique log names

start_client() {
    local team="$1"
    CLIENT_COUNTER=$(( CLIENT_COUNTER + 1 ))
    local idx="$CLIENT_COUNTER"
    local logfile="${LOG_DIR}/client_${team}_$(printf '%04d' "$idx").log"

    # shellcheck disable=SC2046
    (
        cd "$CLIENT_DIR" || exit 1
        export ZAPPY_EASY_ASCENSION="$EASY_ASCENSION"
        # shellcheck disable=SC2086
        exec ./client $(build_client_args "$team") >> "$logfile" 2>&1
    ) &
    local pid=$!
    echo "$pid" >> "$PIDS_FILE"
    echo "$pid"
}

# Maintain up to $max_concurrent live clients for a team.
# Respawns when one dies (up to a lifetime cap so we don't loop forever).
maintain_team() {
    local team="$1"
    local max_concurrent="$2"
    local spawned=0
    # lifetime cap: if no-respawn, cap = initial wave; else generous ceiling
    local cap
    if [[ "$RESPAWN" -eq 0 ]]; then
        cap="$max_concurrent"
    else
        cap=$(( max_concurrent * 20 ))   # generous: 20 full rotations
    fi

    while [[ "$STOP_REQUESTED" -eq 0 ]] && [[ "$spawned" -lt "$cap" ]]; do
        local alive
        alive=$(pgrep -f "client localhost ${PORT} ${team}" 2>/dev/null | wc -l)
        local need=$(( max_concurrent - alive ))
        if [[ "$need" -gt 0 ]]; then
            local i
            for (( i=0; i<need; i++ )); do
                [[ "$spawned" -ge "$cap" ]] && break
                start_client "$team" > /dev/null
                spawned=$(( spawned + 1 ))
                sleep 0.3   # slight stagger so they don't all hit the server simultaneously
            done
        fi
        sleep 3
    done
}

info "Launching team1 (${TEAM1_N} concurrent)..."
maintain_team "team1" "$TEAM1_N" &

info "Launching team2 (${TEAM2_N} concurrent)..."
maintain_team "team2" "$TEAM2_N" &

ok "Client maintainers running. Monitoring..."

# ── Monitor loop ──────────────────────────────────────────────────────────────
hdr "Monitor"

START_TS=$(date +%s)
LAST_HEARTBEAT=0
HEARTBEAT_EVERY=15     # seconds between heartbeat lines
OUTCOME="timeout"

while true; do
    NOW_TS=$(date +%s)
    ELAPSED=$(( NOW_TS - START_TS ))

    # ── Winner check ──────────────────────────────────────────────────────────
    if grep -q "Winner condition reached" "$SERVER_LOG" 2>/dev/null; then
        WINNER_LINE=$(grep "Winner condition reached" "$SERVER_LOG" | tail -1)
        ok "🏆 WINNER DETECTED after ${ELAPSED}s"
        ok "$WINNER_LINE"
        OUTCOME="winner"
        break
    fi

    # ── Timeout ───────────────────────────────────────────────────────────────
    if [[ "$ELAPSED" -ge "$MAX_SECONDS" ]]; then
        warn "Timeout reached (${ELAPSED}s). No winner yet."
        OUTCOME="timeout"
        break
    fi

    # ── Heartbeat ─────────────────────────────────────────────────────────────
    if [[ "$ELAPSED" -ge $(( LAST_HEARTBEAT + HEARTBEAT_EVERY )) ]]; then
        LAST_HEARTBEAT=$ELAPSED

        ALIVE=$(pgrep -f "client localhost ${PORT}" 2>/dev/null | wc -l)

        # Per-level counts: grep all client logs for "LEVEL UP → N" style lines
        # (AI logs: "WorldState: LEVEL UP → N")
        LVL2=$(grep -rh "LEVEL UP" "${LOG_DIR}"/client_*.log 2>/dev/null | grep -c "→ 2\|level 2\| 2$" || true)
        LVL3=$(grep -rh "LEVEL UP" "${LOG_DIR}"/client_*.log 2>/dev/null | grep -c "→ 3\|level 3\| 3$" || true)
        LVL4=$(grep -rh "LEVEL UP" "${LOG_DIR}"/client_*.log 2>/dev/null | grep -c "→ 4\|level 4\| 4$" || true)
        LVL5=$(grep -rh "LEVEL UP" "${LOG_DIR}"/client_*.log 2>/dev/null | grep -c "→ 5\|level 5\| 5$" || true)
        LVL6=$(grep -rh "LEVEL UP" "${LOG_DIR}"/client_*.log 2>/dev/null | grep -c "→ 6\|level 6\| 6$" || true)
        LVL7=$(grep -rh "LEVEL UP" "${LOG_DIR}"/client_*.log 2>/dev/null | grep -c "→ 7\|level 7\| 7$" || true)
        LVL8=$(grep -rh "LEVEL UP" "${LOG_DIR}"/client_*.log 2>/dev/null | grep -c "→ 8\|level 8\| 8$" || true)

        DEATHS=$(grep -rh "Player died\|died!" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || true)
        INCANT_OK=$(grep -rh "INCANTATION SUCCESS\|incantation.*ok" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || true)
        INCANT_KO=$(grep -rh "incantation failed\|incantation.*ko" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || true)
        RALLIES=$(grep -rh "RALLY\|rally" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || true)
        TOTAL_LOGS=$(ls "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || true)

        log "${C_CYAN}[${ELAPSED}s]${C_NC} alive=${ALIVE} logs=${TOTAL_LOGS} | lvl2=${LVL2} lvl3=${LVL3} lvl4=${LVL4} lvl5=${LVL5} lvl6=${LVL6} lvl7=${LVL7} lvl8=${LVL8} | deaths=${DEATHS} incant_ok=${INCANT_OK} incant_ko=${INCANT_KO} rallies=${RALLIES}"

        # Sanity: if zero alive and we're past the warm-up window, something is wrong
        if [[ "$ELAPSED" -gt 30 ]] && [[ "$ALIVE" -eq 0 ]]; then
            warn "No clients alive after ${ELAPSED}s — server may have rejected all connections."
            warn "Check ${SERVER_LOG} for clues."
        fi
    fi

    sleep 1
done

# ── Write summary ─────────────────────────────────────────────────────────────
write_summary() {
    local outcome="$1"
    local elapsed="${ELAPSED:-0}"

    {
        echo "════════════════════════════════════════════════════════════"
        echo "  ZAPPY PROBE SESSION SUMMARY"
        echo "  $(date)"
        echo "════════════════════════════════════════════════════════════"
        echo ""
        echo "OUTCOME       : ${outcome}"
        echo "ELAPSED       : ${elapsed}s / ${MAX_SECONDS}s"
        echo ""
        echo "── Configuration ────────────────────────────────────────────"
        echo "  Port        : ${PORT}"
        echo "  Team1       : ${TEAM1_N} concurrent clients"
        echo "  Team2       : ${TEAM2_N} concurrent clients"
        echo "  Server slots: ${SLOTS}"
        echo "  Time unit   : ${TIME_UNIT}"
        echo "  Forking     : $( [[ "$FORK_ENABLED" -eq 1 ]] && echo enabled || echo disabled )"
        echo "  Easy ascend : $( [[ "$EASY_ASCENSION" -eq 1 ]] && echo YES || echo no )"
        echo "  Respawn     : $( [[ "$RESPAWN" -eq 1 ]] && echo yes || echo no )"
        echo ""
        echo "── Client statistics ────────────────────────────────────────"

        local total_logs deaths incant_ok incant_ko
        total_logs=$(ls "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l)
        deaths=$(grep -rh "Player died\|died!" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || echo 0)
        incant_ok=$(grep -rh "INCANTATION SUCCESS\|incantation.*ok" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || echo 0)
        incant_ko=$(grep -rh "incantation failed\|incantation.*ko" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || echo 0)
        local forks
        forks=$(grep -rh "fork succeeded\|fork ok" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || echo 0)
        local rallies_sent
        rallies_sent=$(grep -rh "broadcast.*RALLY\|RALLY:" "${LOG_DIR}"/client_*.log 2>/dev/null | wc -l || echo 0)

        echo "  Client log files : ${total_logs}"
        echo "  Total deaths     : ${deaths}"
        echo "  Incantations OK  : ${incant_ok}"
        echo "  Incantations KO  : ${incant_ko}"
        echo "  Forks succeeded  : ${forks}"
        echo "  RALLY broadcasts : ${rallies_sent}"
        echo ""
        echo "── Level-up events per level ─────────────────────────────────"
        for lvl in 2 3 4 5 6 7 8; do
            local n
            n=$(grep -rh "LEVEL UP" "${LOG_DIR}"/client_*.log 2>/dev/null | grep -c "→ ${lvl}\|level ${lvl}\| ${lvl}$" || echo 0)
            printf "  Level %-2s reached  : %s times\n" "$lvl" "$n"
        done
        echo ""
        echo "── Highest level reached per team ───────────────────────────"
        for team in team1 team2; do
            local best=1
            for lvl in 8 7 6 5 4 3 2; do
                if grep -rh "LEVEL UP" "${LOG_DIR}"/client_${team}_*.log 2>/dev/null | grep -q "→ ${lvl}\|level ${lvl}\| ${lvl}$"; then
                    best=$lvl
                    break
                fi
            done
            echo "  ${team}: level ${best}"
        done
        echo ""
        echo "── Server winner line ───────────────────────────────────────"
        local winner_line
        winner_line=$(grep "Winner condition reached" "$SERVER_LOG" 2>/dev/null | tail -1)
        if [[ -n "$winner_line" ]]; then
            echo "  ${winner_line}"
        else
            echo "  (no winner condition reached)"
        fi
        echo ""
        echo "── Last 20 server log lines ─────────────────────────────────"
        tail -20 "$SERVER_LOG" 2>/dev/null | sed 's/^/  /'
        echo ""
        echo "── Files ────────────────────────────────────────────────────"
        echo "  Log directory  : ${LOG_DIR}"
        echo "  Server log     : ${SERVER_LOG}"
        echo "  Runner log     : ${RUNNER_LOG}"
        echo "  Client logs    : ${LOG_DIR}/client_<team>_<N>.log"
        echo ""
        echo "  Useful one-liners for post-game analysis:"
        echo "    # All incantation failures with context:"
        echo "    grep -rh 'incantation' ${LOG_DIR}/client_*.log | grep -i 'fail\|ko'"
        echo ""
        echo "    # Timeline of level-ups across all clients:"
        echo "    grep -rh 'LEVEL UP' ${LOG_DIR}/client_*.log | sort"
        echo ""
        echo "    # Clients that reached level 4+:"
        echo "    grep -rl 'LEVEL UP.*[4-8]' ${LOG_DIR}/client_*.log"
        echo ""
        echo "════════════════════════════════════════════════════════════"
    } | tee "$SUMMARY_FILE"
}

write_summary "$OUTCOME"

# ── Exit code ─────────────────────────────────────────────────────────────────
if [[ "$OUTCOME" == "winner" ]]; then
    exit 0
else
    exit 5
fi
