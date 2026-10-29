# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later

# The pure parts (generator, OSC mapping, block timer): always built, the tests link them.
add_library(a3-engine-bench-lib STATIC Generator.cpp OscControl.cpp SpatialPorts.cpp CpuBench.cpp)
target_include_directories(a3-engine-bench-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(a3-engine-bench-lib PUBLIC a3-engine)
target_compile_options(a3-engine-bench-lib PRIVATE -Wall -Wextra -Werror=switch)

# The whole engine, offline, with a given deck count: its CPU as a share of the block period.
add_executable(a3-engine-cpu-bench CpuBenchMain.cpp)
target_link_libraries(a3-engine-cpu-bench PRIVATE a3-engine-bench-lib)
target_compile_options(a3-engine-cpu-bench PRIVATE -Wall -Wextra -Werror=switch)

# The JACK host: libjack from pkg-config (pipewire-jack on the rig).
option(A3_ENGINE_BENCH "Build a3-engine-bench, the headless JACK host" ON)
if(A3_ENGINE_BENCH)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
    add_executable(a3-engine-bench BenchMain.cpp)
    target_link_libraries(a3-engine-bench PRIVATE a3-engine-bench-lib PkgConfig::JACK)
    target_compile_options(a3-engine-bench PRIVATE -Wall -Wextra -Werror=switch)
endif()
