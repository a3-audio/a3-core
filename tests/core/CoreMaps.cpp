// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Core's dispatcher maps: every handler of B1's main port reachable at the truth's addresses, the
// catch-all set, and the device hello kept and compared. Ports of B1's (a3-core 7a7b2441f74e)
// test_core_handlers_are_mapped (the router half) and test_core_devices (CoreListensForIt,
// CoreComparesTheFingerprint), driven through a3core::Core on a fake network.
#include <gtest/gtest.h>

#include "CoreRig.h"

using namespace corerig;

namespace
{
const Endpoint prodj { "192.168.8.60", 7773 };
}

TEST (CoreMaps, EveryDefinedHandlerIsMapped)
{
    // One address per handler a3-core.py defines for the main port (osc_handler_*): each arrives,
    // is seen, and none falls through to the catch-all. REAPER's feedback handler is REAPER's (O8).
    CoreRig rig;
    rig.hello (prodj, "prodj");
    const std::vector<std::pair<std::string, std::vector<OscArg>>> arrivals {
        { "/channel/1/gain", { pyStr ("0.5") } },               // osc_handler_channel
        { "/master/volume", { pyStr ("0.5") } },                // osc_handler_master
        { "/filter/frequency", { pyStr ("0.5") } },             // osc_handler_filter
        { "/aux-return/stem/turn", { pyStr ("1") } },           // osc_handler_aux_return
        { "/stemdeck/1/1/buses", { pyInt (1) } },               // osc_handler_stemdeck
        { "/state/recall", {} },                                // osc_handler_recall
        { "/beat", { pyInt (1), pyInt (1), pyFloat (120.0) } }, // osc_handler_beat
        { "/engine/hello", { pyStr ("boot-1") } },              // osc_handler_engine_hello
        { "/stemdeck/1/hello", { pyStr ("a") } },               // osc_handler_deck_hello
        { "/stemdeck/4/hello", { pyStr ("d") } },
        { "/prodj/live", { pyInt (0) } },                       // osc_handler_prodj_live
        { "/stemdeck/pointer", { pyInt (0), pyFloat (0.5), pyFloat (0.5) } },   // osc_handler_pointer
        { "/motion/pointer", { pyInt (0), pyFloat (0.5), pyFloat (0.5) } },
    };
    for (const auto& [address, args] : arrivals)
        rig.osc (address.rfind ("/channel", 0) == 0 || address.rfind ("/master", 0) == 0 || address.rfind ("/filter", 0) == 0
                         || address.rfind ("/aux-return", 0) == 0
                     ? rig.desk
                     : prodj,
                 address, args);
    for (const auto& [address, args] : arrivals)
        EXPECT_TRUE (rig.seenIn (address)) << address;
    EXPECT_TRUE (rig.seenIn ("/device/hello"));   // osc_handler_device_hello
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CoreMaps, TheDefaultHandlerIsTheUnrouted)
{
    // What no map takes is written down, not dropped: the dispatcher's catch-all is unrouted_handler.
    CoreRig rig;
    rig.osc (rig.desk, "/nothing/here", { pyFloat (1.0) });
    rig.osc (rig.desk, "/nothing/here", { pyFloat (2.0) });
    rig.osc (rig.desk, "/beat/extra", {});
    EXPECT_EQ (rig.unknownCount ("/nothing/here"), 2);
    EXPECT_EQ (rig.unknownCount ("/beat/extra"), 1);
    EXPECT_FALSE (rig.seenIn ("/nothing/here"));
    EXPECT_EQ (rig.sends(), 82u);   // only start-up's remembered state: the stranger moved nothing
}

TEST (CoreMaps, TheWordIsMapped)
{
    CoreRig rig;
    rig.hello (rig.stranger, "radla");
    EXPECT_TRUE (rig.seenIn ("/device/hello"));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CoreMaps, WhatItHearsIsKeptAndServed)
{
    CoreRig rig;
    rig.hello (rig.desk, "mixer", otherHash);
    rig.at (2.0);
    rig.hello ({ "127.0.0.2", 7000 }, "stemdeck");
    const auto devices = rig.core->devices().snapshot (rig.clock.monotonic());
    ASSERT_EQ (devices.size(), 2);
    EXPECT_EQ (devices[0]["device"].toString(), "mixer");
    EXPECT_EQ (devices[0]["hash"].toString(), juce::String (otherHash));
    EXPECT_FALSE (static_cast<bool> (devices[0]["matches"]));
    EXPECT_EQ (devices[1]["device"].toString(), "stemdeck");
    EXPECT_TRUE (static_cast<bool> (devices[1]["matches"]));
}

TEST (CoreMaps, CoresHelloComparesTheFingerprint)
{
    CoreRig rig;
    rig.hello (rig.desk, "mixer", otherHash);
    rig.hello (rig.desk, "mixer", otherHash);   // not news: said once
    rig.hello (rig.desk, "mixer");              // changed its mind: said again
    rig.osc (rig.stranger, "/device/hello", { pyStr ("radla") });   // no hash: only seen
    EXPECT_EQ (rig.journal(), (std::vector<std::string> {
                                  "startup: spoke the remembered state, 19 messages to 2 subscribers",
                                  "0 addresses remembered from " + rig.file ("seen.json").getFullPathName().toStdString(),
                                  "mixer: its a3-osc.json DIFFERS from Core's (000000000000)",
                                  "mixer: its a3-osc.json is Core's (" + ownHash().substr (0, 12) + ")" }));
}
