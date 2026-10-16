#!/bin/bash
# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
# test.sh — build, then run; prints when the runner was built, so a
# green claim can be checked (ctest runs, it does not build).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
"$HERE/build.sh"
BUILD="${A3_ENGINE_BUILD:-$HERE/build}"
echo "=== runner built $(date -r "$BUILD/tests/engine/a3-engine-tests" '+%F %T') ==="
"$BUILD/tests/engine/a3-engine-tests" "$@"
