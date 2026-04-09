#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$ROOT_DIR/server"
CLIENT_DIR="$ROOT_DIR/client_cpp"

SESSION_ENV_FILE="${ZAPPY_SESSION_ENV_FILE:-$ROOT_DIR/run_session.local.env}"
if [[ -f "$SESSION_ENV_FILE" ]]; then
	set -a
	# shellcheck disable=SC1090
	source "$SESSION_ENV_FILE"
	set +a
fi

PORT="${ZAPPY_PORT:-8674}"
HOST="${ZAPPY_HOST:-localhost}"
TEAM_ONE_NAME="${ZAPPY_TEAM_ONE_NAME:-team1}"
TEAM_TWO_NAME="${ZAPPY_TEAM_TWO_NAME:-team2}"
TEAM_ONE_CLIENTS="${ZAPPY_TEAM_ONE_CLIENTS:-10}"
TEAM_TWO_CLIENTS="${ZAPPY_TEAM_TWO_CLIENTS:-10}"
SERVER_TOTAL_CLIENTS="${ZAPPY_SERVER_TOTAL_CLIENTS:-}"
RESUME_DELAY_SECONDS="${ZAPPY_RESUME_DELAY_SECONDS:-2}"
POLL_INTERVAL_SECONDS="${ZAPPY_POLL_INTERVAL_SECONDS:-2}"
MAX_SECONDS="${ZAPPY_MAX_SECONDS:-0}"
BUILD_BEFORE_RUN="${ZAPPY_BUILD_BEFORE_RUN:-1}"
LOG_DIR="${ZAPPY_LOG_DIR:-/tmp/zappy_session_$(date +%Y%m%d_%H%M%S)}"
LIVE_STATUS="${ZAPPY_LIVE_STATUS:-1}"
KILL_PREVIOUS="${ZAPPY_KILL_PREVIOUS:-1}"
EASY_ASCENSION="${ZAPPY_EASY_ASCENSION:-1}"
GAME_TICK_RATE="${ZAPPY_GAME_TICK_RATE:-20}"
DEEP_TRACE="${ZAPPY_DEEP_TRACE:-0}"

if [[ -z "$SERVER_TOTAL_CLIENTS" && -f "$SERVER_DIR/config" ]]; then
	SERVER_TOTAL_CLIENTS="$(grep -E '^NUMBER_OF_CLIENTS=' "$SERVER_DIR/config" | tail -n 1 | cut -d '=' -f 2 | tr -d '[:space:]')"
fi

if [[ -n "$SERVER_TOTAL_CLIENTS" && "$SERVER_TOTAL_CLIENTS" =~ ^[0-9]+$ && "$SERVER_TOTAL_CLIENTS" -gt 0 ]]; then
	per_team_capacity=$((SERVER_TOTAL_CLIENTS / 2))
	if (( per_team_capacity < 1 )); then
		per_team_capacity=1
	fi

	if (( TEAM_ONE_CLIENTS > per_team_capacity )); then
		echo "Requested $TEAM_ONE_CLIENTS clients for $TEAM_ONE_NAME but per-team capacity is $per_team_capacity; clamping"
		TEAM_ONE_CLIENTS="$per_team_capacity"
	fi

	if (( TEAM_TWO_CLIENTS > per_team_capacity )); then
		echo "Requested $TEAM_TWO_CLIENTS clients for $TEAM_TWO_NAME but per-team capacity is $per_team_capacity; clamping"
		TEAM_TWO_CLIENTS="$per_team_capacity"
	fi
fi

SERVER_LOG="$LOG_DIR/server.log"
RUNSH_LOG="$LOG_DIR/runsh.log"
SERVER_APP_LOG_REL=""
SERVER_APP_LOG=""

if [[ -f "$SERVER_DIR/config" ]]; then
	SERVER_APP_LOG_REL="$(grep -E '^LOG_FILE_PATH=' "$SERVER_DIR/config" | tail -n 1 | cut -d '=' -f 2- | tr -d '[:space:]')"
fi
if [[ -z "$SERVER_APP_LOG_REL" ]]; then
	SERVER_APP_LOG_REL="log.txt"
fi
if [[ "$SERVER_APP_LOG_REL" = /* ]]; then
	SERVER_APP_LOG="$SERVER_APP_LOG_REL"
else
	SERVER_APP_LOG="$SERVER_DIR/$SERVER_APP_LOG_REL"
fi

mkdir -p "$LOG_DIR"
shopt -s nullglob

server_pid=""
client_pids=()
client_logs=()
winner_log=""
all_clients_dead=0

cleanup() {
	set +e
	for pid in "${client_pids[@]:-}"; do
		kill "$pid" 2>/dev/null || true
	done
	if [[ -n "$server_pid" ]]; then
		kill "$server_pid" 2>/dev/null || true
	fi
	wait 2>/dev/null || true
}

trap cleanup EXIT INT TERM

build_binaries() {
	if [[ "$BUILD_BEFORE_RUN" != "1" ]]; then
		return
	fi

	echo "Building server and client binaries..."
	make -C "$SERVER_DIR" >/tmp/zappy_session_server_build.log 2>&1
	make -C "$CLIENT_DIR" >/tmp/zappy_session_client_build.log 2>&1
}

spawn_client() {
	local team_name="$1"
	local client_index="$2"
	local log_file="$LOG_DIR/${team_name}_${client_index}.log"

	(
		cd "$CLIENT_DIR"
		ZAPPY_EASY_ASCENSION="$EASY_ASCENSION" ZAPPY_DEEP_TRACE="$DEEP_TRACE" ZAPPY_TIME_UNIT="$GAME_TICK_RATE" ./client -n "$team_name" -p "$PORT" -h "$HOST" -c 1 --insecure true --loop
	) >"$log_file" 2>&1 &
	client_pids+=("$!")
	client_logs+=("$log_file")
	echo "Spawned $team_name client #$client_index -> $log_file"
}

detect_winner() {
	if [[ -f "$SERVER_APP_LOG" ]] && grep -q "Winner condition reached" "$SERVER_APP_LOG" 2>/dev/null; then
		winner_log="$SERVER_APP_LOG"
		return 0
	fi
	return 1
}

count_matches() {
	local pattern="$1"
	local total=0
	local count=0
	local file
	for file in "${client_logs[@]}"; do
		if [[ -f "$file" ]]; then
			count=$(grep -ci "$pattern" "$file" 2>/dev/null || true)
			total=$((total + count))
		fi
	done
	echo "$total"
}

count_server_matches() {
	local pattern="$1"
	local count
	if [[ -f "$SERVER_APP_LOG" ]]; then
		count=$(grep -Eci "$pattern" "$SERVER_APP_LOG" 2>/dev/null || true)
	elif [[ -f "$SERVER_LOG" ]]; then
		count=$(grep -Eci "$pattern" "$SERVER_LOG" 2>/dev/null || true)
	else
		echo "0"
		return
	fi
	if [[ -z "$count" ]]; then
		count=0
	fi
	echo "$count"
}

count_clients_with_pattern() {
	local pattern="$1"
	local total=0
	local file
	for file in "${client_logs[@]}"; do
		if [[ -f "$file" ]] && grep -qi "$pattern" "$file" 2>/dev/null; then
			total=$((total + 1))
		fi
	done
	echo "$total"
}

count_alive_clients() {
	local total=0
	local pid
	for pid in "${client_pids[@]}"; do
		if kill -0 "$pid" 2>/dev/null; then
			total=$((total + 1))
		fi
	done
	echo "$total"
}

all_clients_exited() {
	local pid
	for pid in "${client_pids[@]}"; do
		if kill -0 "$pid" 2>/dev/null; then
			return 1
		fi
	done
	return 0
}

print_status() {
	local elapsed="$1"
	local connected alive failed died malformed levelups fork_requests fork_acks egg_spawns egg_claims
	connected=$(count_clients_with_pattern "Login reply frame")
	alive=$(count_alive_clients)
	failed=$(count_clients_with_pattern "Bootstrap failed")
	died=$(count_matches "Server reported player death")
	malformed=$(count_matches "malformed reply")
	levelups=$(count_matches "player level advanced")
	fork_requests=$(count_matches "requesting fork to grow team population")
	fork_acks=$(count_matches "fork acknowledged by server")
	egg_spawns=$(count_server_matches "Egg [0-9]+ (created player|creating player)")
	egg_claims=$(count_server_matches "Claiming pending egg player|Claim success")
	echo "[status t=${elapsed}s] connected=$connected alive=$alive failed=$failed levelups=$levelups deaths=$died malformed=$malformed fork_req=$fork_requests fork_ok=$fork_acks egg_spawn=$egg_spawns egg_claim=$egg_claims"
}

stop_clients_and_server() {
	for pid in "${client_pids[@]}"; do
		kill "$pid" 2>/dev/null || true
	done
	if [[ -n "$server_pid" ]]; then
		kill "$server_pid" 2>/dev/null || true
	fi
}

build_binaries

echo "Session logs: $LOG_DIR"
if [[ "$LIVE_STATUS" == "1" ]]; then
	echo "Live view commands:"
	echo "  tail -f $SERVER_LOG"
	echo "  tail -f $SERVER_APP_LOG"
	echo "  tail -f $LOG_DIR/${TEAM_ONE_NAME}_1.log"
fi

echo "Starting server on port $PORT"
echo "Game tick rate (time unit): $GAME_TICK_RATE"
if (( TEAM_ONE_CLIENTS < 6 && TEAM_TWO_CLIENTS < 6 )); then
	echo "Warning: official server winner requires 6 level-8 players on the same team; current per-team clients are below 6"
fi
if [[ "$EASY_ASCENSION" == "1" ]]; then
	echo "Easy ascension mode: enabled"
else
	echo "Easy ascension mode: disabled"
fi
if [[ "$KILL_PREVIOUS" == "1" ]]; then
	pkill -f "./zappy $PORT" 2>/dev/null || true
	pkill -f "/zappy $PORT" 2>/dev/null || true
	sleep 1
fi
(
	cd "$SERVER_DIR"
	ZAPPY_EASY_ASCENSION="$EASY_ASCENSION" ZAPPY_TIME_UNIT="$GAME_TICK_RATE" ./zappy "$PORT"
) >"$SERVER_LOG" 2>&1 &
server_pid=$!

sleep 2
if ! kill -0 "$server_pid" 2>/dev/null; then
	echo "Server exited early. Last server log lines:"
	tail -n 40 "$SERVER_LOG" 2>/dev/null || true
	exit 1
fi

team_index=1
while (( team_index <= TEAM_ONE_CLIENTS )); do
	spawn_client "$TEAM_ONE_NAME" "$team_index"
	team_index=$((team_index + 1))
done

team_index=1
while (( team_index <= TEAM_TWO_CLIENTS )); do
	spawn_client "$TEAM_TWO_NAME" "$team_index"
	team_index=$((team_index + 1))
done

sleep "$RESUME_DELAY_SECONDS"
echo "Resuming server time API via run.sh"
(
	cd "$SERVER_DIR"
	./run.sh
) >"$RUNSH_LOG" 2>&1 || true

start_epoch=$(date +%s)
while kill -0 "$server_pid" 2>/dev/null; do
	now_epoch=$(date +%s)
	elapsed=$((now_epoch - start_epoch))

	if detect_winner; then
		echo "Winner detected in log: $winner_log"
		stop_clients_and_server
		break
	fi

	if all_clients_exited; then
		all_clients_dead=1
		echo "All clients have exited; stopping session early"
		stop_clients_and_server
		break
	fi

	if [[ "$LIVE_STATUS" == "1" ]]; then
		print_status "$elapsed"
	fi

	if [[ "$MAX_SECONDS" != "0" ]]; then
		if (( elapsed >= MAX_SECONDS )); then
			echo "Max session time reached after ${elapsed}s"
			stop_clients_and_server
			break
		fi
	fi

	sleep "$POLL_INTERVAL_SECONDS"
done

wait "$server_pid" 2>/dev/null || true

if [[ -n "$winner_log" ]]; then
	if [[ "$winner_log" == "$SERVER_APP_LOG" ]]; then
		winner_team="$(grep -E "Winner condition reached" "$SERVER_APP_LOG" | tail -n 1 | sed -n "s/.*team '\([^']*\)'.*/\1/p")"
		if [[ -n "$winner_team" ]]; then
			echo "Winning team: $winner_team (detected by server)"
		else
			echo "Winning team detected by server"
		fi
	else
		winner_file_name="$(basename "$winner_log")"
		winner_team="${winner_file_name%%_*}"
		winner_client="${winner_file_name#*_}"
		winner_client="${winner_client%.log}"
		echo "Winning team: $winner_team (client $winner_client)"
	fi
elif [[ "$all_clients_dead" == "1" ]]; then
	echo "No winner: all clients exited/died"
else
	echo "No max-level winner was detected before shutdown"
fi

echo "Logs saved in: $LOG_DIR"