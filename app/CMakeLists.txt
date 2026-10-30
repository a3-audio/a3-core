# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later

# The app's pure parts (host, OSC vocabulary, hello, config file, port names, I/O map, connection plan,
# the program's command line, its roles and the supervisor): always built, the tests link them.
add_library(a3-engine-app-lib STATIC EngineHost.cpp OscVocabulary.cpp HelloSender.cpp DeckAudioReport.cpp
    ConfigFile.cpp EnginePorts.cpp IoMap.cpp ConnectionPlan.cpp DeviceBackend.cpp AppArgs.cpp ConfigEdits.cpp SessionRestart.cpp
    ProgramArgs.cpp Roles.cpp ClockRole.cpp Supervisor.cpp Subcommands.cpp LinksOverview.cpp EngineControlApi.cpp WindowProcess.cpp ui/RemoteHostView.cpp ui/LoopbackHttp.cpp ui/CoreClient.cpp ui/MonitorTable.cpp
    ui/MonitorTrafficPage.cpp ui/MonitorTruthPage.cpp ui/MonitorUnknownPage.cpp ui/MonitorRegisterPage.cpp ui/MonitorBenchPage.cpp
    ui/AudioPage.cpp ui/StatusPage.cpp ui/MainWindow.cpp
    ui/ChannelsPage.cpp ui/ZonesPage.cpp ui/SpeakersPage.cpp ui/IoPage.cpp ui/CalibrationPage.cpp ui/FilterPage.cpp ui/SaveBar.cpp
    ui/KeyboardPolicy.cpp ui/OnboardLink.cpp ui/OnScreenKeyboard.cpp)
target_include_directories(a3-engine-app-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(a3-engine-app-lib PUBLIC a3-engine a3-links a3-engine-bench-lib a3-core-lib a3-clock)
target_compile_options(a3-engine-app-lib PRIVATE -Wall -Wextra -Werror=switch)

# The program: one binary, a3-core, with every role (core, engine, window, screencast-fanout, i3-bar)
# and the supervisor (--headless). The engine's JACK backend needs libjack from pkg-config
# (pipewire-jack on the rig). The target is a3-core-app (a3-core-lib is the Core library); its file is a3-core.
option(A3_ENGINE_APP "Build a3-core, the program (needs libjack)" ON)
if(A3_ENGINE_APP)
    add_executable(a3-core-app Main.cpp EngineRole.cpp JackBackend.cpp LinksCommand.cpp)
    set_target_properties(a3-core-app PROPERTIES OUTPUT_NAME a3-core)
    target_link_libraries(a3-core-app PRIVATE a3-engine-app-lib PkgConfig::JACK)
    target_compile_options(a3-core-app PRIVATE -Wall -Wextra -Werror=switch)
endif()
