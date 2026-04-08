#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
QT_BIN="$ROOT_DIR/build/bin/eka2l1_qt"
OUT_DIR="$ROOT_DIR/build-wasm/frames-qt"
PID_FILE="$OUT_DIR/qt-frames.pid"
CACHE_DIR="/tmp/eka2l1-serve"
# On Linux, the Qt binary changes CWD to XDG data dir
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/EKA2L1"

ROM_CID="bafybeicj2jkrjfirzdz5jezz6hjbx2ylyv343kaecnhytl3g6yjy3mwmqm"
RPKG_CID="bafybeihjy4vjxb5cy7zxca4kedg5ncxf5xrbj5basirfefemqwru73aipu"
SIS_CID="bafybeicuomcc2zhzi3vwfb5xihnlkikz3biaa43g4d22z2wptcmhvmp3di"

if [ ! -x "$QT_BIN" ]; then
    echo "Qt build not found at $QT_BIN"
    exit 1
fi

mkdir -p "$OUT_DIR" "$CACHE_DIR"

# Download test data via IPFS if not cached
fetch_cid() {
    local cid="$1" dest="$2" label="$3"
    if [ -f "$dest" ]; then
        echo "$label: cached at $dest"
    else
        echo "$label: downloading..."
        node -e "
            const {fetchCid} = require('$SCRIPT_DIR/wasm/node_modules/@futpib/fetch-cid');
            const fs = require('fs');
            const {Writable} = require('stream');
            (async () => {
                const stream = await fetchCid('$cid');
                const out = fs.createWriteStream('$dest');
                for await (const chunk of stream) {
                    out.write(chunk);
                }
                out.end();
                await new Promise(r => out.on('finish', r));
            })();
        "
        echo "$label: saved to $dest"
    fi
}

fetch_cid "$ROM_CID" "$CACHE_DIR/SYM.ROM" "ROM"
fetch_cid "$RPKG_CID" "$CACHE_DIR/SYM.RPKG" "RPKG"
fetch_cid "$SIS_CID" "$CACHE_DIR/Snakes.sis" "SIS"

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

# Start Xvfb on a free display
DISPLAY_NUM=42
# Kill any stale Xvfb on this display
if [ -f "/tmp/.X${DISPLAY_NUM}-lock" ]; then
    STALE_PID=$(cat "/tmp/.X${DISPLAY_NUM}-lock" 2>/dev/null | tr -d ' ')
    if [ -n "$STALE_PID" ] && kill -0 "$STALE_PID" 2>/dev/null; then
        echo "Killing stale Xvfb (pid $STALE_PID) on display :$DISPLAY_NUM"
        kill "$STALE_PID" 2>/dev/null || true
        sleep 1
    fi
    rm -f "/tmp/.X${DISPLAY_NUM}-lock"
fi
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

# Install device if needed
if [ ! -f "$DATA_DIR/data/devices.yml" ]; then
    echo "Installing device..."
    rm -rf "$DATA_DIR/data"
    "$QT_BIN" --installdevice "$CACHE_DIR/SYM.ROM" "$CACHE_DIR/SYM.RPKG"
fi

"$QT_BIN" $EKA2L1_QT_EXTRA_ARGS --install "$CACHE_DIR/Snakes.sis" --run Snakes --dump-frames "$OUT_DIR" &
QT_PID=$!

echo "Qt emulator running (pid $QT_PID), dumping frames to $OUT_DIR"
echo "Waiting for frame dump to complete..."

wait "$QT_PID" || true

echo ""
echo "Frames captured:"
ls -la "$OUT_DIR"/frame-*.png 2>/dev/null || echo "(none)"
echo "Done"
