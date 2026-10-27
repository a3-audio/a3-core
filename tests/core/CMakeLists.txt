# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
find_package(GTest REQUIRED)

file(GLOB CORE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_executable(a3-core-tests ${CORE_TEST_SOURCES})
target_include_directories(a3-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(a3-core-tests PRIVATE a3-core-lib GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(a3-core-tests)
target_compile_definitions(a3-core-tests PRIVATE
    A3_CORE_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
