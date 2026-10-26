#!/bin/bash
# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
# build.sh — configure once, build with nice -n 19 (a build on a rig is load).
# A3_TARGET=<target> builds only that target.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${A3_ENGINE_BUILD:-$HERE/build}"
cmake -S "$HERE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="${JUCE_PREFIX_PATH:-$HOME/local/juce}" >/dev/null
nice -n 19 cmake --build "$BUILD" -j"${JOBS:-4}" ${A3_TARGET:+--target "$A3_TARGET"}
