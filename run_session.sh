#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$ROOT_DIR/server"
CLIENT_DIR="$ROOT_DIR/client_cpp"

PORT="${ZAPPY_PORT:-8674}"
HOST="${ZAPPY_HOST:-localhost}"
TEAM_ONE_NAME="${ZAPPY_TEAM_ONE_NAME:-team1}"
TEAM_TWO_NAME="${ZAPPY_TEAM_TWO_NAME:-team2}"
TEAM_ONE_CLIENTS="${ZAPPY_TEAM_ONE_CLIENTS:-10}"
TEAM_TWO_CLIENTS="${ZAPPY_TEAM_TWO_CLIENTS:-10}"
RESUME_DELAY_SECONDS="${ZAPPY_RESUME_DELAY_SECONDS:-2}"
POLL_INTERVAL_SECONDS="${ZAPPY_POLL_INTERVAL_SECONDS:-2}"
MAX_SECONDS="${ZAPPY_MAX_SECONDS:-0}"
BUILD_BEFORE_RUN="${ZAPPY_BUILD_BEFORE_RUN:-1}"
LOG_DIR="${ZAPPY_LOG_DIR:-/tmp/zappy_session_$(date +%Y%m%d_%H%M%S)}"
LIVE_STATUS="${ZAPPY_LIVE_STATUS:-1}"
KILL_PREVIOUS="${ZAPPY_KILL_PREVIOUS:-1}"

SERVER_LOG="$LOG_DIR/server.log"
RUNSH_LOG="$LOG_DIR/runsh.log"

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
		./client -n "$team_name" -p "$PORT" -h "$HOST" -c 1 --insecure true --loop
	) >"$log_file" 2>&1 &
	client_pids+=("$!")
	client_logs+=("$log_file")
	echo "Spawned $team_name client #$client_index -> $log_file"
}

detect_winner() {
	local log_file
	for log_file in "${client_logs[@]}"; do
		if grep -q "victory threshold reached" "$log_file" 2>/dev/null; then
			winner_log="$log_file"
			return 0
		fi
		if grep -q "player level advanced to 8" "$log_file" 2>/dev/null; then
			winner_log="$log_file"
			return 0
		fi
	done
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
	local connected alive failed died malformed levelups
	connected=$(count_clients_with_pattern "Login reply frame")
	alive=$(count_alive_clients)
	failed=$(count_clients_with_pattern "Bootstrap failed")
	died=$(count_matches "Server reported player death")
	malformed=$(count_matches "malformed reply")
	levelups=$(count_matches "player level advanced")
	echo "[status t=${elapsed}s] connected=$connected alive=$alive failed=$failed levelups=$levelups deaths=$died malformed=$malformed"
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
	echo "  tail -f $LOG_DIR/${TEAM_ONE_NAME}_1.log"
fi

echo "Starting server on port $PORT"
if [[ "$KILL_PREVIOUS" == "1" ]]; then
	pkill -f "./zappy $PORT" 2>/dev/null || true
	pkill -f "/zappy $PORT" 2>/dev/null || true
	sleep 1
fi
(
	cd "$SERVER_DIR"
	./zappy "$PORT"
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
	winner_file_name="$(basename "$winner_log")"
	winner_team="${winner_file_name%%_*}"
	winner_client="${winner_file_name#*_}"
	winner_client="${winner_client%.log}"
	echo "Winning team: $winner_team (client $winner_client)"
elif [[ "$all_clients_dead" == "1" ]]; then
	echo "No winner: all clients exited/died"
else
	echo "No max-level winner was detected before shutdown"
fi

echo "Logs saved in: $LOG_DIR"