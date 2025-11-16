#!/usr/bin/env bash

# Runs dspPlayground executable

# CMake Build Directory
BUILD_DIR="build"

echo "Running dspPlayground.."
( if cd "$BUILD_DIR"; then ./dspPlayground; fi )
