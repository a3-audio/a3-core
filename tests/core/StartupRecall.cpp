// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the StartupRecall cases of a3-core's tools/tests/test_core_gate.py (B1 7a7b2441f74e): the
// one /state/recall Core sends itself after the evening replay.
#include <gtest/gtest.h>

#include "StartupRecall.h"

using namespace a3core;

TEST (StartupRecall, ADevicesRecallIsNotIt)
{
    StartupRecall recall;
    EXPECT_FALSE (recall.isIt ({ pyInt (1) }));
    EXPECT_FALSE (recall.isIt ({ pyInt (0) }));
    EXPECT_FALSE (recall.isIt ({}));
}

TEST (StartupRecall, TheTokenIsNeverWhatADeviceSendsAndFitsAnOscInt)
{
    for (int i = 0; i < 1000; ++i)
    {
        const auto token = StartupRecall().token();
        EXPECT_GT (token, 1);
        EXPECT_LT (token, 1LL << 31);
    }
}

TEST (StartupRecall, CoresOwnRecallIsItOnce)
{
    StartupRecall recall (12345);
    EXPECT_TRUE (recall.isIt ({ pyInt (12345) }));
    EXPECT_FALSE (recall.isIt ({ pyInt (12345) }));
}

TEST (StartupRecall, AWordIsNotIt)
{
    EXPECT_FALSE (StartupRecall (5).isIt ({ pyStr ("five") }));
}

TEST (StartupRecall, RearmingTakesTheNextOneOnly)
{
    StartupRecall recall (7);
    EXPECT_TRUE (recall.isIt ({ pyInt (7) }));
    EXPECT_EQ (recall.rearm (9), 9);
    EXPECT_FALSE (recall.isIt ({ pyInt (7) }));
    EXPECT_TRUE (recall.isIt ({ pyInt (9) }));
    const auto fresh = recall.rearm();
    EXPECT_GT (fresh, 1);
    EXPECT_TRUE (recall.isIt ({ pyInt (fresh) }));
}
