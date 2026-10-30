# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
# The Core as a pure library (namespace a3core): no audio device, no GUI.
add_library(a3-core-lib STATIC Version.cpp Json.cpp Truth.cpp OscWire.cpp Dispatcher.cpp LatestWins.cpp
    Presence.cpp DelayValues.cpp HelloGate.cpp Devices.cpp Desks.cpp MotionTarget.cpp ProdjLive.cpp
    Buttons.cpp Tempo.cpp TempoSource.cpp Ahead.cpp DeskFirst.cpp SendFailures.cpp Subscribers.cpp Departments.cpp Announce.cpp
    StateFile.cpp CoreState.cpp Recall.cpp Evening.cpp
    Stems.cpp StemsWords.cpp Decks.cpp EngineWords.cpp StartupRecall.cpp
    Traffic.cpp SeenFile.cpp Register.cpp Render.cpp VuRelay.cpp
    ScreencastFanout.cpp I3Bar.cpp
    HttpServer.cpp CoreApi.cpp
    Core.cpp CoreView.cpp CoreValues.cpp CorePython.cpp CorePeers.cpp CoreRecall.cpp
    UdpNet.cpp CoreService.cpp PlatformLinux.cpp)
target_include_directories(a3-core-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The version Core reports in /api/status: the package's, which a3-system's installer/package.py hands
# the recipe (A3_PACKAGE_VERSION -> packaging/app/stage -> -DA3_CORE_VERSION); empty for a build no
# package made. Only Version.cpp sees it, so a new version recompiles one file.
set(A3_CORE_VERSION "" CACHE STRING "a3-core's version as Core reports it; empty: a development build")
set_source_files_properties(Version.cpp PROPERTIES COMPILE_DEFINITIONS "A3_CORE_VERSION=\"${A3_CORE_VERSION}\"")
target_link_libraries(a3-core-lib PUBLIC a3-engine-juce a3-delay)
target_compile_options(a3-core-lib PRIVATE -Wall -Wextra -Werror=switch)
