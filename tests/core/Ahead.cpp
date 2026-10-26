// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of the module cases of a3-core's tools/tests/test_core_ahead.py (B0 f3345dc7feeb):
// StemDeck's preview of the music reaches Motion through Core. The CoreIsWired cases read
// a3-core.py's source; the one that survives as behaviour is a later task's (CorePeers).
#include <gtest/gtest.h>

#include <set>

#include "Ahead.h"
#include "B0Truth.h"

using namespace a3core;

namespace
{
using Args = std::vector<OscArg>;

OscArg none { 'N', std::monostate {} };

Args preview (const std::string& section, const std::string& next, long long bars, double energy)
{
    return { pyStr (section), pyStr (next), pyInt (bars), pyFloat (energy) };
}
}

// TheWord

TEST (Ahead, ItIsInTheTruth)
{
    const auto& truth = b0::truth();
    EXPECT_EQ (truth.address ("stemdeck.ahead"), "/stemdeck/ahead");
    const auto entry = truth.data()["addresses"]["stemdeck.ahead"];
    EXPECT_EQ (entry["args"].toString().toStdString(), "ssif");
    EXPECT_EQ (b0::strings (entry["from"]), std::vector<std::string> { "stemdeck" });
    EXPECT_EQ (b0::strings (entry["to"]), std::vector<std::string> { "core" });
    EXPECT_EQ (b0::strings (entry["relayed_to"]), std::vector<std::string> { "motion" });
}

TEST (Ahead, CoreTakesItApartByItsName)
{
    const auto found = b0::truth().match ("/stemdeck/ahead");
    ASSERT_TRUE (found.has_value());
    EXPECT_EQ (found->first, "stemdeck.ahead");
    EXPECT_TRUE (found->second.empty());
}

TEST (Ahead, TheRoutesItTravelsExist)
{
    std::set<std::pair<std::string, std::string>> routes;
    for (const auto& route : b0::truth().routes())
        routes.insert ({ route["from"].toString().toStdString(), route["to"].toString().toStdString() });
    EXPECT_TRUE (routes.count ({ "stemdeck", "core.osc" }));
    EXPECT_TRUE (routes.count ({ "core", "motion.osc" }));
}

// WhatGoesOn

TEST (Ahead, APreviewGoesOnAsItCame)
{
    EXPECT_EQ (aheadArguments (preview ("build", "drop", 4, 0.5)), preview ("build", "drop", 4, 0.5));
}

TEST (Ahead, NoneGoesOnToo)
{
    EXPECT_EQ (aheadArguments (preview ("none", "none", 0, 0.0)), preview ("none", "none", 0, 0.0));
}

TEST (Ahead, AWholeNumberEnergyGoesOnAsAFloat)
{
    const Args arrived { pyStr ("drop"), pyStr ("end"), pyInt (8), pyInt (1) };
    const auto arguments = aheadArguments (arrived);
    ASSERT_TRUE (arguments.has_value());
    EXPECT_EQ (*arguments, preview ("drop", "end", 8, 1.0));
    EXPECT_EQ ((*arguments)[3].tag, 'f');
}

TEST (Ahead, AnythingElseIsNotPassedOn)
{
    const std::vector<Args> wrong {
        {},
        { pyStr ("build"), pyStr ("drop"), pyInt (4) },
        { pyStr ("build"), pyStr ("drop"), pyInt (4), pyFloat (0.5), pyInt (1) },
        { pyInt (1), pyStr ("drop"), pyInt (4), pyFloat (0.5) },
        { pyStr ("build"), none, pyInt (4), pyFloat (0.5) },
        { pyStr ("build"), pyStr ("drop"), pyFloat (4.5), pyFloat (0.5) },
        { pyStr ("build"), pyStr ("drop"), pyBool (true), pyFloat (0.5) },
        { pyStr ("build"), pyStr ("drop"), pyInt (4), pyStr ("loud") },
        { pyStr ("build"), pyStr ("drop"), pyInt (4), pyBool (false) },
    };
    for (const auto& arguments : wrong)
        EXPECT_FALSE (aheadArguments (arguments).has_value()) << arguments.size() << " arguments";
}

// WhoMaySayIt

TEST (Ahead, TheStemdeckCoreFollows)
{
    EXPECT_TRUE (fromTheFollowed ("192.168.8.20", std::string ("192.168.8.20")));
}

TEST (Ahead, NotAnotherOne)
{
    EXPECT_FALSE (fromTheFollowed ("127.0.0.1", std::string ("192.168.8.20")));
}

TEST (Ahead, NotWhileCoreFollowsNone)
{
    EXPECT_FALSE (fromTheFollowed ("192.168.8.20", std::nullopt));
}
