# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
# The Core as a pure library (namespace a3core): no audio device, no GUI.
add_library(a3-core-lib STATIC Version.cpp Json.cpp Truth.cpp OscWire.cpp Dispatcher.cpp LatestWins.cpp
    Presence.cpp HelloGate.cpp Devices.cpp Desks.cpp MotionTarget.cpp ProdjLive.cpp
    Buttons.cpp Tempo.cpp Ahead.cpp DeskFirst.cpp SendFailures.cpp Subscribers.cpp Announce.cpp)
target_include_directories(a3-core-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(a3-core-lib PUBLIC a3-engine-juce)
target_compile_options(a3-core-lib PRIVATE -Wall -Wextra -Werror=switch)
