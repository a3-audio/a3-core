# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later

# The app's pure parts (host, OSC vocabulary, hello): always built, the tests link them.
add_library(a3-engine-app-lib STATIC EngineHost.cpp OscVocabulary.cpp HelloSender.cpp)
target_include_directories(a3-engine-app-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(a3-engine-app-lib PUBLIC a3-engine a3-engine-bench-lib)
target_compile_options(a3-engine-app-lib PRIVATE -Wall -Wextra -Werror=switch)
