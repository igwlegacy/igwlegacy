#!/usr/bin/env bash
# Start the Iron Grip: Warlord Legacy dedicated server under wine.
#
# Runs the patched dedicated engine (igwarlord.exe) with the legacy game
# module and pk3 in base/, the same game code and scripts the clients
# run. Gameplay and netcode settings live in base/server.cfg, the map
# cycle in base/maprotation.txt.
#
# Settings (environment variables):
#   PORT=27960          UDP game port, the only port the server uses
#   MAP=<name>          first map, defaults to the first entry of
#                       base/maprotation.txt
#   RCON_PASSWORD=...   enables rcon with this password
#   DEDICATED=1         1: LAN clients are not rate limited (sv_lanForceRate)
#                       2: every client uses its own "rate"
#   HUNK_MEGS=128       hunk memory, the stock dedicated default is 56
#   HEADLESS=1          0 shows the engine's console window (needs a display)
#   WINEPREFIX=...      defaults to a prefix of its own, created on first use;
#                       everything in it is killed when the server stops
#   WINE=wine           wine binary
#   WINESERVER=wineserver
#
# Extra arguments are passed to the engine, e.g.:
#   ./start-server.sh +set sv_hostname "My Server" +set sv_difficulty 2
set -euo pipefail

# cd to parent dir of current script
cd "$(dirname "${BASH_SOURCE[0]}")"

PORT="${PORT:-27960}"
RCON_PASSWORD="${RCON_PASSWORD:-}"
DEDICATED="${DEDICATED:-1}"
HUNK_MEGS="${HUNK_MEGS:-128}"
HEADLESS="${HEADLESS:-1}"
WINE="${WINE:-wine}"
WINESERVER="${WINESERVER:-wineserver}"
export WINEPREFIX="${WINEPREFIX:-${XDG_DATA_HOME:-$HOME/.local/share}/igwl-dedicated-server/wineprefix}"
export WINEDEBUG="${WINEDEBUG:--all}"
# Don't offer to install wine-mono/wine-gecko when the prefix is created,
# the server needs neither.
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"

if [[ "$DEDICATED" != 1 && "$DEDICATED" != 2 ]]; then
	echo "ERROR: DEDICATED must be 1 or 2, not '$DEDICATED'" >&2
	exit 1
fi

# The stock server's own gamex86.dll is an older build than the client's
# and lacks the legacy patches and scripts, so refuse to run without the
# legacy files instead of silently running different game code.
required_files=(
	"igwarlord.exe"
	"base/gamex86.dll"
	"base/zzzigwlegacy.pk3"
	"base/server.cfg"
	"base/maprotation.txt"
)
for path in "${required_files[@]}"; do
	if [[ ! -f "$path" ]]; then
		echo "ERROR: '$PWD/$path' does not exist" >&2
		exit 1
	fi
done

# --- First map ---------------------------------------------------------------
#
# The engine only advances the rotation from the map that is running, so
# the first one has to be given on the command line.
if [[ -z "${MAP:-}" ]]; then
	# first whitespace separated token, the file has CRLF line endings
	MAP="$(tr -d '\r' < base/maprotation.txt | awk 'NF { print $1; exit }')"
fi
if [[ -z "$MAP" ]]; then
	echo "ERROR: no MAP given and base/maprotation.txt is empty" >&2
	exit 1
fi

# --- Wine prefix -------------------------------------------------------------
if [[ ! -f "$WINEPREFIX/system.reg" ]]; then
	echo "Creating wine prefix $WINEPREFIX"
	mkdir -p "$WINEPREFIX"
	"$WINE" wineboot --init >/dev/null 2>&1
fi

# Without a display wine falls back to its null driver, so the engine's
# console window isn't created and no X server is needed.
wine_env=()
if [[ "$HEADLESS" == 1 ]]; then
	wine_env=(env -u DISPLAY -u WAYLAND_DISPLAY)
fi

# --- Console output ----------------------------------------------------------
#
# The dedicated engine prints to its own console window only, so follow
# the unbuffered log (logfile 2) instead. It is truncated at every start.
rm -f base/qconsole.log
tail -n +1 -F base/qconsole.log 2>/dev/null &
tail_pid=$!

server_pid=""
cleanup() {
	if [[ -n "$server_pid" ]]; then
		# With rcon, quit properly so clients are told the server is gone
		# instead of timing out.
		if [[ -n "$RCON_PASSWORD" ]]; then
			printf '\xff\xff\xff\xffrcon %s quit' "$RCON_PASSWORD" \
				> "/dev/udp/127.0.0.1/$PORT" 2>/dev/null || true
			for _ in {1..20}; do
				kill -0 "$server_pid" 2>/dev/null || break
				sleep 0.25
			done
		fi
		# Killing the wine launcher leaves the Windows process running in
		# the wineserver, so stop everything in the (server only) prefix.
		"$WINESERVER" -k 2>/dev/null || true
		wait "$server_pid" 2>/dev/null || true
	fi
	kill "$tail_pid" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo "Starting IGW Legacy dedicated server: map $MAP, UDP port $PORT"

# Latched cvars (dedicated, net_port, com_hunkMegs) must be set on the
# command line, everything else is in base/server.cfg, which the engine
# runs before these arguments.
args=(
	+set dedicated "$DEDICATED"
	+set net_port "$PORT"
	+set com_hunkMegs "$HUNK_MEGS"
	+set logfile 2
)
if [[ -n "$RCON_PASSWORD" ]]; then
	args+=(+set rconPassword "$RCON_PASSWORD")
fi
args+=("$@" +map "$MAP")

# Run in the background and wait, so the traps above can stop it.
"${wine_env[@]}" "$WINE" ./igwarlord.exe "${args[@]}" &
server_pid=$!
status=0
wait "$server_pid" || status=$?
server_pid=""
exit "$status"
