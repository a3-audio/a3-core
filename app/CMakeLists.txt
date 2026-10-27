# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later

# The app's pure parts (host, OSC vocabulary, hello, config file, port names, I/O map, connection plan): always built, the tests link them.
add_library(a3-engine-app-lib STATIC EngineHost.cpp OscVocabulary.cpp HelloSender.cpp DeckAudioReport.cpp
    ConfigFile.cpp EnginePorts.cpp IoMap.cpp ConnectionPlan.cpp DeviceBackend.cpp AppArgs.cpp ConfigEdits.cpp SessionRestart.cpp
    ui/AudioPage.cpp ui/StatusPage.cpp ui/MainWindow.cpp
    ui/ChannelsPage.cpp ui/ZonesPage.cpp ui/IoPage.cpp ui/CalibrationPage.cpp ui/FilterPage.cpp ui/SaveBar.cpp)
target_include_directories(a3-engine-app-lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(a3-engine-app-lib PUBLIC a3-engine a3-links a3-engine-bench-lib)
target_compile_options(a3-engine-app-lib PRIVATE -Wall -Wextra -Werror=switch)

# The app: the JACK backend needs libjack from pkg-config (pipewire-jack on the rig).
option(A3_ENGINE_APP "Build a3-engine, the engine app (needs libjack)" ON)
if(A3_ENGINE_APP)
    # The target name a3-engine is the signal-path library's; the binary is still a3-engine.
    add_executable(a3-engine-app Main.cpp JackBackend.cpp)
    set_target_properties(a3-engine-app PROPERTIES OUTPUT_NAME a3-engine)
    target_link_libraries(a3-engine-app PRIVATE a3-engine-app-lib PkgConfig::JACK)
    target_compile_options(a3-engine-app PRIVATE -Wall -Wextra -Werror=switch)
endif()
