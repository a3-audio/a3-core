// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_buttons.py (B0 f3345dc7feeb): two senders press the same
// button and do not mean the same thing -- the mixer's text is an edge, Motion's number a state.
#include <gtest/gtest.h>

#include <type_traits>

#include "Buttons.h"

using namespace a3core;

namespace
{
const OscArg none { 'N', std::monostate {} };

bool isNoChange (const std::variant<NoChange, bool>& wanted) { return std::holds_alternative<NoChange> (wanted); }

// assertIs (wanted_toggle (...), True/False): the answer is that state, not NO_CHANGE.
::testing::AssertionResult isState (const std::variant<NoChange, bool>& wanted, bool expected)
{
    if (const auto* state = std::get_if<bool> (&wanted))
        return *state == expected ? ::testing::AssertionSuccess()
                                  : ::testing::AssertionFailure() << "wanted " << *state << ", expected " << expected;
    return ::testing::AssertionFailure() << "NO_CHANGE, expected " << expected;
}

// state = wanted_toggle (value, current=state), where the answer is always a state.
bool next (const OscArg& value, bool current)
{
    const auto wanted = wantedToggle (value, current);
    return std::get<bool> (wanted);
}
}

// TheMixersMomentaryButton: text arrives, and it is an edge

TEST (Buttons, APressFlipsWhateverTheToggleWas)
{
    EXPECT_TRUE (isState (wantedToggle (pyStr ("1"), false), true));
    EXPECT_TRUE (isState (wantedToggle (pyStr ("1"), true), false));
}

TEST (Buttons, TheReleaseAsksForNothing)
{
    EXPECT_TRUE (isNoChange (wantedToggle (pyStr ("0"), false)));
    EXPECT_TRUE (isNoChange (wantedToggle (pyStr ("0"), true)));
}

TEST (Buttons, TwoPressesComeBackToWhereTheyStarted)
{
    bool state = false;
    for (int i = 0; i < 2; ++i)
        state = next (pyStr ("1"), state);
    EXPECT_FALSE (state);
}

TEST (Buttons, AnythingElseItCouldSayIsNotAPress)
{
    for (const char* said : { "", "2", "true", "on" })
        EXPECT_TRUE (isNoChange (wantedToggle (pyStr (said), true))) << said;
}

// MotionsKeyOnAScreen: a number arrives, and it is the state that is wanted

TEST (Buttons, OneTurnsItOnAndSaysSoTwiceWithoutFlippingBack)
{
    EXPECT_TRUE (isState (wantedToggle (pyFloat (1.0), false), true));
    EXPECT_TRUE (isState (wantedToggle (pyFloat (1.0), true), true));
}

TEST (Buttons, ZeroTurnsItOffAndStaysOff)
{
    EXPECT_TRUE (isState (wantedToggle (pyFloat (0.0), true), false));
    EXPECT_TRUE (isState (wantedToggle (pyFloat (0.0), false), false));
}

TEST (Buttons, AnIntIsANumberToo)
{
    EXPECT_TRUE (isState (wantedToggle (pyInt (1), false), true));
    EXPECT_TRUE (isState (wantedToggle (pyInt (0), true), false));
}

TEST (Buttons, TheBoundaryIsWhereMotionPutsIt)
{
    // MixerState::channelToggle is `value > 0.5f`, strictly: exactly 0.5 reads as off.
    EXPECT_TRUE (isState (wantedToggle (pyFloat (0.5), false), false));
    EXPECT_TRUE (isState (wantedToggle (pyFloat (0.51), false), true));
}

TEST (Buttons, ALostDatagramCannotLeaveTheTwoDisagreeing)
{
    bool state = false;
    for (const double wanted : { 1.0, 1.0, 0.0, 0.0, 1.0 })
        state = next (pyFloat (wanted), state);
    EXPECT_TRUE (state);
}

// TheFilterMode: same two senders, one address, /fx/mode

TEST (Buttons, TheMixerNamesTheModeItWants)
{
    EXPECT_EQ (wantedFxMode (pyStr ("high_pass"), "low_pass"), "high_pass");
    EXPECT_EQ (wantedFxMode (pyStr ("low_pass"), "high_pass"), "low_pass");
}

TEST (Buttons, NamingTheModeItIsAlreadyInChangesNothing)
{
    EXPECT_EQ (wantedFxMode (pyStr ("high_pass"), "high_pass"), "high_pass");
}

TEST (Buttons, MotionSendsTheNumberItsOwnKeyReads)
{
    // MixerState::filterIsHighPass is `> 0.5f`: 1 is HPF, 0 is LPF.
    EXPECT_EQ (wantedFxMode (pyFloat (1.0), "low_pass"), "high_pass");
    EXPECT_EQ (wantedFxMode (pyFloat (0.0), "high_pass"), "low_pass");
}

TEST (Buttons, AWordCoreDoesNotKnowLeavesTheFilterWhereItIs)
{
    // The bug this replaces made every unrecognised value mean LOW_PASS.
    for (const auto& said : { pyStr ("bandpass"), pyStr (""), pyStr ("HIGH_PASS"), none })
    {
        EXPECT_EQ (wantedFxMode (said, "high_pass"), "high_pass");
        EXPECT_EQ (wantedFxMode (said, "low_pass"), "low_pass");
    }
}

// TheTwoSpellingsAreNotMixedUp: the distinction is the type, and only the type

TEST (Buttons, TheStringOneIsNeverReadAsTheNumberOne)
{
    EXPECT_TRUE (isState (wantedToggle (pyStr ("1"), true), false));
    EXPECT_TRUE (isState (wantedToggle (pyFloat (1.0), true), true));
}

TEST (Buttons, TheNumberZeroIsNeverReadAsTheRelease)
{
    EXPECT_TRUE (isNoChange (wantedToggle (pyStr ("0"), true)));
    EXPECT_TRUE (isState (wantedToggle (pyFloat (0.0), true), false));
}

TEST (Buttons, NoChangeRefusesToBeMistakenForOff)
{
    // Python raises on `if not wanted:`; here "asks for nothing" is its own type, which cannot be
    // tested as a truth value, and the answer for a release does not hold a state at all.
    static_assert (! std::is_convertible_v<NoChange, bool>);
    static_assert (! std::is_constructible_v<bool, NoChange>);
    EXPECT_FALSE (std::holds_alternative<bool> (wantedToggle (pyStr ("0"), true)));
}

TEST (Buttons, ABoolCountsAsANumber)
{
    // python-osc decodes an OSC boolean as True/False, and bool is an int.
    EXPECT_TRUE (isState (wantedToggle (pyBool (true), true), true));
    EXPECT_TRUE (isState (wantedToggle (pyBool (false), true), false));
}
