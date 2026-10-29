// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Core owns the AUX-RTN delay page's four values: the desk sends detents, Core steps, remembers,
// tells the engine and announces what the desk shows. On the package's truth (B1's has no such words).
#include <gtest/gtest.h>

#include "CoreRig.h"

using namespace corerig;

namespace
{
CoreRig theRig() { return CoreRig (CoreRig::WithOptions {}, onThePackage()); }

void turn (CoreRig& rig, const char* page, int clicks)
{
    rig.osc (rig.desk, std::string ("/aux-return/delay/") + page + "/turn", { pyInt (clicks) });
}

void tempo (CoreRig& rig, double bpm)
{
    rig.osc (rig.deckA, "/stemdeck/1/tempo", { pyFloat (bpm), pyInt (1), pyInt (1) });
}

std::vector<std::string> only (const std::vector<std::string>& lines, const std::string& prefix) { return startingWith (lines, prefix); }
}

TEST (CoreDelay, ABeatTurnStepsAndIsAnnouncedToTheDeskAndTheEngine)
{
    auto rig = theRig();
    rig.engineHello ("boot-1");
    rig.clear();
    turn (rig, "beat", 1);
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay"), (std::vector<std::string> { "/aux-return/delay/beat f:0.5" }));
    EXPECT_EQ (only (rig.to ("engine"), "/aux-return/delay"), (std::vector<std::string> { "/aux-return/delay/beat f:0.5" }));
    EXPECT_EQ (rig.unknownRows(), 0u);
}

TEST (CoreDelay, EachPageStepsInItsOwnUnit)
{
    auto rig = theRig();
    rig.clear();
    turn (rig, "feedback", 1);
    turn (rig, "color", -3);
    turn (rig, "spread", 2);
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay"),
               (std::vector<std::string> { "/aux-return/delay/feedback f:0.75", "/aux-return/delay/color f:-0.3",
                                           "/aux-return/delay/spread f:90" }));
}

TEST (CoreDelay, ATurnAtTheEndStaysThereAndIsAnnouncedAgain)
{
    auto rig = theRig();
    turn (rig, "spread", 99);
    rig.clear();
    turn (rig, "spread", 1);
    turn (rig, "spread", 1);
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay"),
               (std::vector<std::string> { "/aux-return/delay/spread f:180", "/aux-return/delay/spread f:180" }));
}

TEST (CoreDelay, ColorStopsOnTheCentreAndGoesOnWithTheNextTurn)
{
    auto rig = theRig();
    turn (rig, "color", 2);
    rig.clear();
    turn (rig, "color", -5);
    turn (rig, "color", -1);
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay"),
               (std::vector<std::string> { "/aux-return/delay/color f:0", "/aux-return/delay/color f:-0.1" }));
}

TEST (CoreDelay, ThePushStillTogglesTheCue)
{
    auto rig = theRig();
    rig.clear();
    rig.osc (rig.desk, "/aux-return/stem/push", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/cue/led f:1"));
}

TEST (CoreDelay, ATurnThatIsNoWholeNumberIsUnknown)
{
    auto rig = theRig();
    rig.clear();
    rig.osc (rig.desk, "/aux-return/delay/beat/turn", { pyFloat (1.0) });
    EXPECT_TRUE (only (rig.to ("mixer"), "/aux-return/delay").empty());
    EXPECT_EQ (rig.unknownCount ("/aux-return/delay/beat/turn"), 1);
}

TEST (CoreDelay, AProdjWithLiveOffTurnsNothing)
{
    auto rig = theRig();
    const Endpoint prodj { "192.168.8.60", 7773 };
    rig.hello (prodj, "prodj");
    rig.clear();
    rig.osc (prodj, "/aux-return/delay/beat/turn", { pyInt (1) });
    EXPECT_TRUE (only (rig.to ("mixer"), "/aux-return/delay").empty());
    turn (rig, "beat", 0);   // a turn of nothing says the value: it is still the default
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay"), (std::vector<std::string> { "/aux-return/delay/beat f:0.25" }));   // still the default
}

TEST (CoreDelay, TheValuesAreRememberedAcrossARestart)
{
    auto rig = theRig();
    turn (rig, "beat", -1);
    turn (rig, "feedback", -4);
    turn (rig, "color", 5);
    turn (rig, "spread", 3);
    rig.stop();
    const auto written = rig.json ("state.json");
    EXPECT_EQ (static_cast<double> (written["delay_beat"]), 0.125);
    EXPECT_EQ (static_cast<double> (written["delay_feedback"]), 0.5);
    EXPECT_EQ (static_cast<double> (written["delay_color"]), 0.5);
    EXPECT_EQ (static_cast<double> (written["delay_spread"]), 105.0);
}

TEST (CoreDelay, AStartAfterThatRecallsThemToTheDeskAndTheEngine)
{
    const std::string state = R"({"delay_beat": 0.5, "delay_feedback": 0.3, "delay_color": -0.2, "delay_spread": 30})";
    auto options = onThePackage();
    options.files = { { "state.json", state } };
    CoreRig rig (CoreRig::WithOptions {}, options);
    rig.engineHello ("boot-1");
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/delay/beat f:0.5"));
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/delay/feedback f:0.3"));
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/delay/color f:-0.2"));
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/delay/spread f:30"));
    for (const char* word : { "/aux-return/delay/beat f:0.5", "/aux-return/delay/feedback f:0.3",
                              "/aux-return/delay/color f:-0.2", "/aux-return/delay/spread f:30" })
        EXPECT_TRUE (has (rig.to ("engine"), word)) << word;
}

TEST (CoreDelay, ARecallAnswersWithTheValuesToo)
{
    auto rig = theRig();
    turn (rig, "spread", 1);
    rig.clear();
    rig.osc (rig.desk, "/state/recall", { pyInt (1) });
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/delay/spread f:75"));
    EXPECT_TRUE (has (rig.to ("mixer"), "/aux-return/delay/beat f:0.25"));
    EXPECT_EQ (count (rig.to ("mixer"), "/aux-return/delay/spread f:75"), 1);
}

// R16: Core keeps the selection and tells the desk the beat that plays.
TEST (CoreDelay, TwoBeatsBelowNinetyTwoBpmIsAnnouncedAsOne)
{
    auto rig = theRig();
    rig.engineHello ("boot-1");
    tempo (rig, 90.0);
    turn (rig, "beat", 4);   // 0.25 -> 0.5 -> 0.75 -> 1 -> 2
    rig.clear();
    turn (rig, "beat", 1);   // at the end already
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay/beat"), (std::vector<std::string> { "/aux-return/delay/beat f:1" }));
    EXPECT_EQ (only (rig.to ("engine"), "/aux-return/delay/beat"), (std::vector<std::string> { "/aux-return/delay/beat f:2" }));
}

TEST (CoreDelay, WhenTheTempoRisesPastTheLimitTheBeatIsAnnouncedAsTwoAgain)
{
    auto rig = theRig();
    rig.engineHello ("boot-1");
    tempo (rig, 90.0);
    turn (rig, "beat", 4);
    rig.clear();
    tempo (rig, 91.0);   // still below
    EXPECT_TRUE (only (rig.to ("mixer"), "/aux-return/delay/beat").empty());
    tempo (rig, 126.0);
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay/beat"), (std::vector<std::string> { "/aux-return/delay/beat f:2" }));
    EXPECT_EQ (only (rig.to ("engine"), "/aux-return/delay/beat"), std::vector<std::string> {});   // the engine fits it itself
    tempo (rig, 130.0);   // plays the same: not announced again
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay/beat").size(), 1u);
}

TEST (CoreDelay, WhenTheTempoFallsBelowTheLimitTheBeatIsAnnouncedAsOne)
{
    auto rig = theRig();
    rig.engineHello ("boot-1");
    tempo (rig, 126.0);
    turn (rig, "beat", 4);
    rig.clear();
    tempo (rig, 80.0);
    EXPECT_EQ (only (rig.to ("mixer"), "/aux-return/delay/beat"), (std::vector<std::string> { "/aux-return/delay/beat f:1" }));
}

TEST (CoreDelay, ATempoChangeThatMovesNothingAnnouncesNothing)
{
    auto rig = theRig();
    rig.engineHello ("boot-1");
    turn (rig, "beat", 1);
    rig.clear();
    tempo (rig, 100.0);
    tempo (rig, 110.0);
    EXPECT_TRUE (only (rig.to ("mixer"), "/aux-return/delay").empty());
}

TEST (CoreDelay, TheSelectionNotTheBeatThatPlaysIsWhatIsRemembered)
{
    // Total Recall restores 2; only the announcement says 1 while the tempo is low.
    auto rig = theRig();
    rig.engineHello ("boot-1");
    tempo (rig, 90.0);
    turn (rig, "beat", 4);
    rig.stop();
    const auto written = rig.json ("state.json");
    EXPECT_EQ (static_cast<double> (written["delay_beat"]), 2.0);
}
