#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
QT_BIN="$ROOT_DIR/build/bin/eka2l1_qt"
OUT_DIR="$ROOT_DIR/build-wasm/frames-qt"
PID_FILE="$OUT_DIR/qt-frames.pid"

if [ ! -x "$QT_BIN" ]; then
    echo "Qt build not found at $QT_BIN"
    exit 1
fi

mkdir -p "$OUT_DIR"

# Pid file lock
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE")
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo "FAIL: Another qt-frames test is already running (pid $OLD_PID). Remove $PID_FILE if stale."
        exit 1
    fi
    echo "Removing stale pid file (pid $OLD_PID)"
    rm -f "$PID_FILE"
fi
echo $$ > "$PID_FILE"

# Clean old frames
rm -f "$OUT_DIR"/frame-*.png

# Start Xvfb
DISPLAY_NUM=42
Xvfb ":$DISPLAY_NUM" -screen 0 800x600x24 &
XVFB_PID=$!
sleep 1
export DISPLAY=":$DISPLAY_NUM"

cleanup() {
    kill "$QT_PID" 2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
    rm -f "$PID_FILE"
}
trap cleanup EXIT

"$QT_BIN" --run Snakes --dump-frames "$OUT_DIR" &
QT_PID=$!

echo "Qt emulator running (pid $QT_PID), dumping frames to $OUT_DIR"
echo "Waiting for frame dump to complete..."

wait "$QT_PID" || true

echo ""
echo "Frames captured:"
ls -la "$OUT_DIR"/frame-*.png 2>/dev/null || echo "(none)"
echo "Done"
