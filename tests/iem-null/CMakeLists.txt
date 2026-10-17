# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
find_package(GTest REQUIRED)

# Links only the -vst3host variant: a3-engine would pull a3-iem in as well, and
# both define the same IEM symbols.
add_executable(a3-engine-iem-nulltests IemNull.cpp)
target_link_libraries(a3-engine-iem-nulltests PRIVATE a3-iem-vst3host a3-engine-juce-vst3host
    GTest::gtest GTest::gtest_main)
target_compile_definitions(a3-engine-iem-nulltests PRIVATE
    A3_SPATIAL_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../../data/spatial"
    A3_SPATIAL_REAPER_BOOTH_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../../data/spatial-reaper-booth-6.1")
target_compile_options(a3-engine-iem-nulltests PRIVATE -Wall -Wextra)
