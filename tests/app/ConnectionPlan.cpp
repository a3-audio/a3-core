// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "ConnectionPlan.h"

TEST (JackBackend, RefusesBesideReaperUnlessNoConnect)
{
    const std::vector<std::string> withReaper { "system", "REAPER", "beat-analyzer" };
    const auto refused = a3app::refuseBesideReaper (withReaper, true);
    EXPECT_TRUE (refused.failed());
    EXPECT_EQ (refused.getErrorMessage(), "REAPER is running: start with --no-connect");

    EXPECT_TRUE (a3app::refuseBesideReaper (withReaper, false).wasOk());
    EXPECT_TRUE (a3app::refuseBesideReaper ({ "system", "StemDeck-A", "REAPER-bridge" }, true).wasOk());
    EXPECT_TRUE (a3app::refuseBesideReaper ({}, true).wasOk());
}
