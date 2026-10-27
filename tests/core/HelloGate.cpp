// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_hello_gate.py (B0 f3345dc7feeb).
#include <gtest/gtest.h>

#include "HelloGate.h"

using namespace a3core;

// ValuesOnlyAfterHello

TEST (HelloGate, AHostThatHasNotSaidHelloIsRefused)
{
    EXPECT_EQ (HelloGate().check ("192.168.8.20"), GateVerdict::refuseFirst);
}

TEST (HelloGate, TheRefusalIsNewsOnlyOncePerHost)
{
    HelloGate gate;
    gate.check ("192.168.8.20");
    EXPECT_EQ (gate.check ("192.168.8.20"), GateVerdict::refuse);
    EXPECT_EQ (gate.check ("192.168.8.11"), GateVerdict::refuseFirst);
}

TEST (HelloGate, AHostThatSaidHelloIsAdmitted)
{
    HelloGate gate;
    gate.greet ("192.168.8.20", true);
    EXPECT_EQ (gate.check ("192.168.8.20"), GateVerdict::admit);
}

TEST (HelloGate, AHelloOpensOnlyItsOwnHost)
{
    HelloGate gate;
    gate.greet ("192.168.8.11", true);
    EXPECT_EQ (gate.check ("192.168.8.20"), GateVerdict::refuseFirst);
}

TEST (HelloGate, ANewRunStartsClosed)
{
    HelloGate gate;
    gate.greet ("192.168.8.20", true);
    EXPECT_EQ (HelloGate().check ("192.168.8.20"), GateVerdict::refuseFirst);
}

TEST (HelloGate, AfterAHelloALaterRefusalIsNewsAgainNever)
{
    HelloGate gate;
    gate.check ("a");
    gate.greet ("a", true);
    EXPECT_EQ (gate.check ("a"), GateVerdict::admit);
}

TEST (HelloGate, ATrustedSourceIsAdmittedWithoutOpeningItsHost)
{
    HelloGate gate;
    gate.trust ({ "127.0.0.1", 40000 });
    EXPECT_EQ (gate.check ("127.0.0.1", 40000), GateVerdict::admit);
    EXPECT_EQ (gate.check ("127.0.0.1", 40001), GateVerdict::refuseFirst);
}

// OnlyOnCoresOwnTruth

TEST (HelloGate, AHelloWithAnotherTruthDoesNotOpen)
{
    HelloGate gate;
    gate.greet ("h", false);
    EXPECT_EQ (gate.check ("h"), GateVerdict::refuseDiffersFirst);
    EXPECT_EQ (gate.check ("h"), GateVerdict::refuse);
}

TEST (HelloGate, ALaterMatchingHelloOpens)
{
    HelloGate gate;
    gate.greet ("h", false);
    gate.greet ("h", true);
    EXPECT_EQ (gate.check ("h"), GateVerdict::admit);
}

TEST (HelloGate, ALaterDifferingHelloClosesAgainAndIsSaidAgain)
{
    HelloGate gate;
    gate.greet ("h", true);
    gate.greet ("h", false);
    EXPECT_EQ (gate.check ("h"), GateVerdict::refuseDiffersFirst);
}

// TheDeskIsTrusted

TEST (HelloGate, TheDeskIsAdmittedWithoutAHello)
{
    HelloGate gate;
    gate.trustHost ("192.168.8.11");
    EXPECT_EQ (gate.check ("192.168.8.11", 50000), GateVerdict::admit);
}

TEST (HelloGate, ADifferingHelloDoesNotCloseTheDesk)
{
    HelloGate gate;
    gate.trustHost ("192.168.8.11");
    gate.greet ("192.168.8.11", false);
    EXPECT_EQ (gate.check ("192.168.8.11"), GateVerdict::admit);
}

TEST (HelloGate, EveryOtherHostStillNeedsItsHello)
{
    HelloGate gate;
    gate.trustHost ("192.168.8.11");
    EXPECT_EQ (gate.check ("192.168.8.20"), GateVerdict::refuseFirst);
    gate.greet ("192.168.8.20", false);
    EXPECT_EQ (gate.check ("192.168.8.20"), GateVerdict::refuseDiffersFirst);
}
