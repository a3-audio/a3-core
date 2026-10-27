// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_desks.py (B0 f3345dc7feeb): Core follows every virtual
// desk that says hello, four at most, and names each by its listening socket.
#include <gtest/gtest.h>

#include <algorithm>

#include "Desks.h"
#include "ProdjLive.h"

using namespace a3core;
using Strings = std::vector<std::string>;

namespace
{
constexpr int port = 7773, vu = 7774;

DeskFollowers followers() { return DeskFollowers (port, vu); }

std::string host (int i) { return "192.168.8." + std::to_string (20 + i); }
}

// AHello

TEST (Desks, TheFirstJoins)
{
    auto desks = followers();
    EXPECT_EQ (desks.hello ("192.168.8.20", 0.0), DeskVerdict::joined);
}

TEST (Desks, TheNextFromTheSameHostIsKnown)
{
    auto desks = followers();
    desks.hello ("192.168.8.20", 0.0);
    EXPECT_EQ (desks.hello ("192.168.8.20", 30.0), DeskVerdict::known);
}

TEST (Desks, FourJoinSideBySide)
{
    auto desks = followers();
    for (int i = 0; i < maxDesks; ++i)
        EXPECT_EQ (desks.hello (host (i), 0.0), DeskVerdict::joined);
    EXPECT_EQ ((int) desks.hosts().size(), maxDesks);
}

TEST (Desks, AFifthIsRefusedAndSaidOnce)
{
    auto desks = followers();
    for (int i = 0; i < maxDesks; ++i)
        desks.hello (host (i), 0.0);
    EXPECT_EQ (desks.hello ("192.168.8.99", 1.0), DeskVerdict::refusedFirst);
    EXPECT_EQ (desks.hello ("192.168.8.99", 31.0), DeskVerdict::refused);
    const auto hosts = desks.hosts();
    EXPECT_EQ (std::count (hosts.begin(), hosts.end(), "192.168.8.99"), 0);
}

// Silence

TEST (Desks, AMinuteWithoutHelloForgetsTheDeskOnce)
{
    auto desks = followers();
    desks.hello ("192.168.8.20", 0.0);
    EXPECT_EQ (desks.silence (59.0), Strings {});
    EXPECT_EQ (desks.silence (61.0), Strings { "192.168.8.20" });
    EXPECT_EQ (desks.silence (90.0), Strings {});
    EXPECT_EQ (desks.hosts(), Strings {});
}

TEST (Desks, ADeskThatComesBackJoinsAgain)
{
    auto desks = followers();
    desks.hello ("192.168.8.20", 0.0);
    desks.silence (61.0);
    EXPECT_EQ (desks.hello ("192.168.8.20", 62.0), DeskVerdict::joined);
}

TEST (Desks, ARefusedDeskGetsTheSlotThatFrees)
{
    auto desks = followers();
    for (int i = 0; i < maxDesks; ++i)
        desks.hello (host (i), 0.0);
    desks.hello ("192.168.8.99", 1.0);
    for (int i = 1; i < maxDesks; ++i)
        desks.hello (host (i), 50.0);
    desks.silence (61.0);   // .20 is gone
    EXPECT_EQ (desks.hello ("192.168.8.99", 62.0), DeskVerdict::joined);
}

// Names

TEST (Desks, ADeskIsNamedByItsListeningSocket)
{
    auto desks = followers();
    desks.hello ("192.168.8.20", 0.0);
    EXPECT_EQ (desks.nameOf ("192.168.8.20", port), deskName ("192.168.8.20"));
    EXPECT_EQ (deskName ("192.168.8.20"), "desk@192.168.8.20");
}

TEST (Desks, MotionOnTheSameHostIsNotTheDesk)
{
    // a3nuc2 runs Motion and a desk. Motion sends from an ephemeral port and must stay "motion".
    auto desks = followers();
    desks.hello ("192.168.8.20", 0.0);
    EXPECT_FALSE (desks.nameOf ("192.168.8.20", 51234).has_value());
}

TEST (Desks, AnUnfollowedHostHasNoDeskName)
{
    EXPECT_FALSE (followers().nameOf ("192.168.8.20", port).has_value());
}

// Meters

TEST (Desks, EveryFollowedDeskGetsThemOnItsVuPort)
{
    auto desks = followers();
    desks.hello ("192.168.8.20", 0.0);
    desks.hello ("192.168.8.30", 0.0);
    EXPECT_EQ (desks.vuDestinations(), (std::vector<Endpoint> { { "192.168.8.20", vu }, { "192.168.8.30", vu } }));
}

// Kinds

TEST (Desks, AProdjIsNamedByItsKind)
{
    auto desks = followers();
    desks.hello ("h", 0.0, prodjKind);
    EXPECT_EQ (desks.nameOf ("h", port), "prodj@h");
}

TEST (Desks, TheDefaultKindIsDesk)
{
    auto desks = followers();
    desks.hello ("h", 0.0);
    EXPECT_EQ (desks.nameOf ("h", port), deskName ("h"));
    EXPECT_EQ (deskName ("h"), "desk@h");
}

TEST (Desks, HostsOfKind)
{
    auto desks = followers();
    desks.hello ("a", 0.0);
    desks.hello ("b", 0.0, prodjKind);
    EXPECT_EQ (desks.hostsOfKind (prodjKind), Strings { "b" });
    EXPECT_EQ (desks.hostsOfKind (deskKind), Strings { "a" });
}

TEST (Desks, KindOfAName)
{
    EXPECT_EQ (kindOf ("prodj@h"), "prodj");
    EXPECT_EQ (kindOf ("desk@h"), "desk");
    EXPECT_FALSE (kindOf ("motion").has_value());
}

TEST (Desks, AKnownHostSayingAnotherKindTakesItAsAChange)
{
    // Core drops the old client and joins the new one: its name is its kind.
    auto desks = followers();
    desks.hello ("h", 0.0);
    EXPECT_EQ (desks.hello ("h", 1.0, prodjKind), DeskVerdict::kindChanged);
    EXPECT_EQ (desks.nameOf ("h", port), "prodj@h");
    EXPECT_EQ (desks.hostsOfKind (deskKind), Strings {});
    EXPECT_EQ (desks.hello ("h", 2.0, prodjKind), DeskVerdict::known);
}

TEST (Desks, TheSameKindAgainIsKnown)
{
    auto desks = followers();
    desks.hello ("h", 0.0, prodjKind);
    EXPECT_EQ (desks.hello ("h", 1.0, prodjKind), DeskVerdict::known);
}

// AnUnfollowedProdj

namespace
{
void fillHouse (DeskFollowers& desks)
{
    for (int i = 0; i < maxDesks; ++i)
        desks.hello ("10.0.0." + std::to_string (i), 0.0);
}

std::string gateName (const DeskFollowers& desks, const SaidKinds& said, const std::string& host, int fromPort)
{
    return said.gateName (desks.nameOf (host, fromPort).value_or (host), host, fromPort);
}
}

TEST (Desks, AFifthProdjsVolumeIsRefusedWhileLiveIsOff)
{
    auto desks = followers();
    fillHouse (desks);
    SaidKinds said (port);
    EXPECT_EQ (desks.hello ("10.0.0.9", 0.0, prodjKind), DeskVerdict::refusedFirst);
    said.said ("10.0.0.9", prodjKind);
    const auto who = gateName (desks, said, "10.0.0.9", port);
    EXPECT_EQ (who, "prodj@10.0.0.9");
    EXPECT_FALSE (ProdjLive().admits (who, "channel.volume"));
}

TEST (Desks, AForgottenProdjStaysAProdj)
{
    auto desks = followers();
    SaidKinds said (port);
    desks.hello ("h", 0.0, prodjKind);
    said.said ("h", prodjKind);
    desks.silence (1000.0);
    EXPECT_EQ (gateName (desks, said, "h", port), "prodj@h");
}

TEST (Desks, TheLastDeskHelloCounts)
{
    SaidKinds said (port);
    said.said ("h", prodjKind);
    said.said ("h", deskKind);
    EXPECT_EQ (said.gateName ("h", "h", port), "desk@h");
}

TEST (Desks, OtherHellosDoNotCount)
{
    // a3nuc2 runs Motion beside the app: Motion's hello leaves the kind.
    SaidKinds said (port);
    said.said ("h", prodjKind);
    said.said ("h", "motion");
    EXPECT_EQ (said.gateName ("h", "h", port), "prodj@h");
}

TEST (Desks, AnotherSocketOnThatHostKeepsItsName)
{
    SaidKinds said (port);
    said.said ("h", prodjKind);
    EXPECT_EQ (said.gateName ("motion", "h", 51234), "motion");
}

TEST (Desks, AHostThatNeverSaidADeskHelloKeepsItsName)
{
    EXPECT_EQ (SaidKinds (port).gateName ("mixer", "h", port), "mixer");
}

TEST (Desks, AFollowedDeskKeepsItsName)
{
    SaidKinds said (port);
    said.said ("h", prodjKind);
    EXPECT_EQ (said.gateName ("desk@h", "h", port), "desk@h");
}
