# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
find_package(GTest REQUIRED)

file(GLOB REFERENCE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/airwindows/*/*.cpp)
add_library(a3-engine-reference STATIC
    reference/PurestGainReference.cpp reference/SmoothEQ3Reference.cpp
    reference/Isolator3Reference.cpp ${REFERENCE_SOURCES})
target_include_directories(a3-engine-reference PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/reference
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/airwindows
    ${CMAKE_CURRENT_SOURCE_DIR}/reference/shim)
target_compile_options(a3-engine-reference PRIVATE -w)   # upstream code, unchanged

add_executable(a3-engine-tests ReferenceSmoke.cpp Laws.cpp PurestGain.cpp)
target_include_directories(a3-engine-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(a3-engine-tests PRIVATE a3-engine a3-engine-reference
    GTest::gtest GTest::gtest_main)
target_compile_definitions(a3-engine-tests PRIVATE
    A3_CURVES_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/data/curves-golden.json")
include(GoogleTest)
gtest_discover_tests(a3-engine-tests)
