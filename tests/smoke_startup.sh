#!/bin/bash
# Starts QtTerminalExample on Qt's offscreen platform with an empty HOME and
# passes if it is still running after a few seconds and wrote the sample
# connections file. Usage: smoke_startup.sh <path to QtTerminalExample>
set -u
app="$1"
home=$(mktemp -d)
trap 'rm -rf "$home"' EXIT

HOME="$home" XDG_CONFIG_HOME="$home/.config" QT_QPA_PLATFORM=offscreen \
    timeout 5 "$app" >"$home/out.log" 2>&1
code=$?

if [ "$code" -ne 124 ]; then
    echo "The app exited early with code $code (expected it to keep running):"
    cat "$home/out.log"
    exit 1
fi

conn="$home/.config/QtTerminalExample/connections.json"
if [ ! -s "$conn" ]; then
    echo "The app did not write $conn on first start"
    cat "$home/out.log"
    exit 1
fi

echo "Started and stayed up for 5 seconds; wrote the sample connections."
