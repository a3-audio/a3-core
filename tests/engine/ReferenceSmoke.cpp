// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "TestSignals.h"
#include "reference/AirwindowsReference.h"

// The reference itself must behave as the spike measured: PurestGain at
// A = 0.5 (0 dB), B = 1 passes the signal bit for bit.
TEST (ReferenceSmoke, PurestGainAtUnityPassesTheSignal)
{
    auto ref = a3ref::makePurestGain (44100.0f);
    ref->setParameter (0, 0.5f);
    ref->setParameter (1, 1.0f);
    const auto in = a3test::noise (44100, 0.5f, 1);
    const auto out = a3test::runInBlocks (*ref, in, 128);
    EXPECT_LE (a3test::maxAbsDiff (in, out), 1e-6);
}
