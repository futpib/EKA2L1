#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
QT_BIN="$ROOT_DIR/build/bin/eka2l1_qt"
SCREENSHOT="$ROOT_DIR/build-wasm/qt-screenshot.png"
DURATION="${1:-60}"

if [ ! -x "$QT_BIN" ]; then
    echo "Qt build not found at $QT_BIN"
    exit 1
fi

# Start Xvfb
DISPLAY_NUM=42
Xvfb ":$DISPLAY_NUM" -screen 0 800x600x24 &
XVFB_PID=$!
sleep 1
export DISPLAY=":$DISPLAY_NUM"

cleanup() {
    kill "$QT_PID" 2>/dev/null || true
    kill "$XVFB_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Run the Qt emulator
"$QT_BIN" --run Snakes &
QT_PID=$!

echo "Qt emulator running (pid $QT_PID), waiting ${DURATION}s..."

# Take screenshots every 10 seconds
for i in $(seq 10 10 "$DURATION"); do
    sleep 10
    if ! kill -0 "$QT_PID" 2>/dev/null; then
        echo "Qt process exited early"
        break
    fi
    import -window root "$ROOT_DIR/build-wasm/qt-screenshot-${i}s.png" 2>/dev/null && \
        echo "Screenshot at ${i}s saved" || \
        echo "Screenshot at ${i}s failed (import not available?)"
done

# Final screenshot
if kill -0 "$QT_PID" 2>/dev/null; then
    import -window root "$SCREENSHOT" 2>/dev/null && \
        echo "Final screenshot saved to $SCREENSHOT" || \
        echo "Final screenshot failed"
fi

echo "Done"
