#!/usr/bin/env bash

# Runs dspPlayground executable and watches dsp.cpp for changes
    # rebuilds the dsp shared library when changed
# Supports macOS (BSD) and linux (GNU)

# File to monitor
DSP_FILE="dsp.cpp"

# CMake Build Directory
BUILD_DIR="build"

# check for changes every X seconds
SLEEP_TIME=0.2

echo "Watching $DSP_FILE for changes.."
echo "Press Ctrl+C to stop"

# Store the last modification timestamp for detecting changes
    # macOS
last_mod_time=$(stat -f "%m" "$DSP_FILE" 2>/dev/null)
    # linux
if [ -z "$last_mod_time" ]; then
    last_mod_time=$(stat -c "%Y" "$DSP_FILE")
fi

# Repeatedly check file's timestamp
while true; do

    # macOS
    current_mod_time=$(stat -f "%m" "$DSP_FILE" 2>/dev/null)

    # linux
    if [ -z "$current_mod_time" ]; then
        current_mod_time=$(stat -c "%Y" "$DSP_FILE")
    fi

    # compare timestamps
    if [ "$current_mod_time" != "$last_mod_time" ]; then
        echo "[REBUILD] Detected change — rebuilding dsp..."
        last_mod_time=$current_mod_time

        # Rebuild just the DSP code via subshell
        (
            cd "$BUILD_DIR" || exit 1
            make dsp && echo "[SUCCESS] Build complete!"
        )
    fi

    sleep $SLEEP_TIME
done

