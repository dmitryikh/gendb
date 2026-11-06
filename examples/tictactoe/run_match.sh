#!/usr/bin/env bash
# Plays a match between two processes, and checks that they agree about every game.
#   run_match.sh <path to tictactoe> [flags for both players, such as --n=100000]
set -euo pipefail
binary="$1"
shift

output=$(mktemp -d)
trap 'kill $(jobs -p) 2>/dev/null || true; rm -rf "$output"' EXIT

"$binary" --player=o "$@" > "$output/o.txt" &
o=$!
"$binary" --player=x "$@" > "$output/x.txt" &
x=$!
wait "$x"
wait "$o"

grep -h '^player . *:.*moves/s' "$output/x.txt" "$output/o.txt"
x_result=$(grep '^result ' "$output/x.txt")
o_result=$(grep '^result ' "$output/o.txt")
if [ "$x_result" != "$o_result" ]; then
  printf 'The players disagree:\n  x: %s\n  o: %s\n' "$x_result" "$o_result" >&2
  exit 1
fi
echo "Both players agree: $x_result"
