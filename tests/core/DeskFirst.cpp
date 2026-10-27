// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_desk_first.py (B0 f3345dc7feeb): the desk beats Core's
// own evening replay.
#include <gtest/gtest.h>

#include <map>

#include "DeskFirst.h"

using namespace a3core;

namespace
{
const std::string gain = "/channel/1/gain";
const std::string eq = "/channel/1/eq/low";
const std::string volume = "/master/volume";
const DeskFirst::Pairs evening { { gain, 0.2 }, { eq, 0.3 } };

std::map<std::string, double> asMap (const DeskFirst::Pairs& pairs) { return { pairs.begin(), pairs.end() }; }
}

// WithoutTheDesk

TEST (DeskFirst, TheEveningIsReplayedAsItWas)
{
    EXPECT_EQ (DeskFirst().over (evening), evening);
}

TEST (DeskFirst, AMotionTouchAloneDoesNotChangeTheReplay)
{
    DeskFirst deskFirst;
    deskFirst.heard (gain, 0.9, false);
    EXPECT_EQ (deskFirst.over (evening), evening);
}

// TheDeskSentFirst

TEST (DeskFirst, TheDesksValueReplacesTheEvenings)
{
    DeskFirst deskFirst;
    deskFirst.heard (gain, 0.8, true);
    EXPECT_EQ (asMap (deskFirst.over (evening)), (std::map<std::string, double> { { gain, 0.8 }, { eq, 0.3 } }));
}

TEST (DeskFirst, TheDesksLatestValueCounts)
{
    DeskFirst deskFirst;
    deskFirst.heard (gain, 0.8, true);
    deskFirst.heard (gain, 0.6, true);
    EXPECT_EQ (asMap (deskFirst.over (evening)).at (gain), 0.6);
}

TEST (DeskFirst, AnAddressTheEveningLacksIsReplayedToo)
{
    DeskFirst deskFirst;
    deskFirst.heard (volume, 0.7, true);
    EXPECT_EQ (asMap (deskFirst.over (evening)).at (volume), 0.7);
}

TEST (DeskFirst, ALaterTouchElsewhereOnADeskAddressWins)
{
    DeskFirst deskFirst;
    deskFirst.heard (gain, 0.8, true);
    deskFirst.heard (gain, 0.4, false);
    EXPECT_EQ (asMap (deskFirst.over (evening)).at (gain), 0.4);
}

TEST (DeskFirst, ItHoldsForEveryReplayOfTheRun)
{
    DeskFirst deskFirst;
    deskFirst.heard (gain, 0.8, true);
    deskFirst.over (evening);
    EXPECT_EQ (asMap (deskFirst.over (evening)).at (gain), 0.8);
}

// TheDeskSentAfter

TEST (DeskFirst, AReplayAfterALateDumpCarriesTheDesk)
{
    DeskFirst deskFirst;
    EXPECT_EQ (deskFirst.over (evening), evening);
    deskFirst.heard (gain, 0.8, true);
    EXPECT_EQ (asMap (deskFirst.over (evening)).at (gain), 0.8);
}

// Not pinned by a Python test, but dict's order is what the replay follows: the evening's
// addresses in the evening's order, then the desk's new ones in the order the desk sent them.
TEST (DeskFirst, TheReplayKeepsPythonsDictOrder)
{
    DeskFirst deskFirst;
    deskFirst.heard (volume, 0.7, true);
    deskFirst.heard (gain, 0.8, true);
    EXPECT_EQ (deskFirst.over (evening), (DeskFirst::Pairs { { gain, 0.8 }, { eq, 0.3 }, { volume, 0.7 } }));
}
