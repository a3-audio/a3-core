#!/bin/bash
# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
# test.sh — build, then run; prints when each runner was built, so a
# green claim can be checked (ctest runs, it does not build).
#   ./test.sh [gtest args]         build all, run the engine runner, then the core runner
#   ./test.sh --core [gtest args]  build and run only a3-core-tests
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD="${A3_ENGINE_BUILD:-$HERE/build}"

run_runner() {   # <target> <path> [gtest args]
    local target="$1" runner="$2"; shift 2
    echo "=== $target built $(date -r "$runner" '+%F %T') ==="
    "$runner" "$@"
}

if [ "$1" = "--core" ]; then
    shift
    A3_TARGET=a3-core-tests "$HERE/build.sh"
    run_runner a3-core-tests "$BUILD/tests/core/a3-core-tests" "$@"
    exit
fi

"$HERE/build.sh"
run_runner a3-engine-tests "$BUILD/tests/engine/a3-engine-tests" "$@"
run_runner a3-core-tests "$BUILD/tests/core/a3-core-tests" "$@"
