// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_stems.py (B1 7a7b2441f74e): the mirror of StemDeck's bus
// switches and the desk's rules on it. Pair p = deck ceil(p/4), stem (p-1) % 4 + 1; bus 1-4 the
// desk channels, 5 AUX.
#include <algorithm>
#include <random>

#include <gtest/gtest.h>

#include "Json.h"
#include "Stems.h"

using namespace a3core;
using Commands = std::vector<Command>;

namespace
{
constexpr int aux = Stems::aux;
constexpr int analogSource = Stems::analogSource;
constexpr int stemSource = Stems::stemSource;
constexpr int analogMode = Stems::analogMode;
constexpr int stemMode = Stems::stemMode;
constexpr int cueField = Stems::cueField;
constexpr int analogInput = Stems::analogInput;
constexpr int returnIndex = Stems::returnIndex;
using Source = std::optional<int>;

int bit (int bus) { return 1 << (bus - 1); }

Stems stemsWith (std::initializer_list<std::pair<int, int>> masks)
{
    Stems s;
    for (const auto& [pair, mask] : masks)
        s.report (pair, mask);
    return s;
}

// A mirror that already obeys the rules, as StemDeck leaves it.
Stems tidied (int mode, std::initializer_list<std::pair<int, int>> masks)
{
    auto s = stemsWith (masks);
    s.returnMode = mode;
    s.tidy();
    return s;
}

std::array<Source, 4> undecided() { return { std::nullopt, std::nullopt, std::nullopt, std::nullopt }; }

bool has (const Commands& commands, Command wanted) { return std::find (commands.begin(), commands.end(), wanted) != commands.end(); }

juce::var parsed (const char* text)
{
    juce::String error;
    return parseJson (text, error);
}

Commands sorted (Commands commands)
{
    std::sort (commands.begin(), commands.end());
    return commands;
}
}

// TheMirror

TEST (Stems, AReportSetsTheMask)
{
    auto s = stemsWith ({ { 3, bit (2) } });
    EXPECT_EQ (s.channelMask (1), 1 << 2);
}

TEST (Stems, ADamagedReportChangesNothing)
{
    Stems s;
    for (const auto& [pair, mask] : std::vector<std::pair<int, int>> { { 0, 1 }, { 9, 1 }, { 1, 64 }, { 1, -1 } })
        EXPECT_FALSE (s.report (pair, mask)) << pair << "," << mask;
    EXPECT_EQ (s.masks, (std::array<int, 8> {}));
}

TEST (Stems, AStemHasOnePlace)
{
    auto s = stemsWith ({ { 1, bit (1) }, { 2, bit (aux) } });
    EXPECT_EQ (s.placeOf (1), Source (0));
    EXPECT_EQ (s.placeOf (2), Source (returnIndex));
    EXPECT_EQ (s.placeOf (3), std::nullopt);
}

// TheSelector

TEST (Stems, TheRingOfChannelTwoIsItsStemsAndA)
{
    EXPECT_EQ (ring (1), (std::array<int, 3> { 1, 5, 8 }));
    EXPECT_EQ (analogInput, 8);
}

TEST (Stems, AStemHasItsOwnChannel)
{
    std::vector<int> own;
    for (int pair = 1; pair <= Stems::pairs; ++pair)
        own.push_back (ownChannel (pair));
    EXPECT_EQ (own, (std::vector<int> { 0, 1, 2, 3, 0, 1, 2, 3 }));
}

TEST (Stems, TheCursorStartsOnA)
{
    EXPECT_EQ (Stems().cursors, (std::array<int, 4> { 8, 8, 8, 8 }));
}

TEST (Stems, TheCursorRunsOverThreeAndWrapsAround)
{
    Stems s;
    s.cursors[2] = analogInput;
    std::vector<int> seen;
    for (int i = 0; i < 4; ++i)
    {
        s.turn (2, +1);
        seen.push_back (s.cursors[2]);
    }
    EXPECT_EQ (seen, (std::vector<int> { 2, 6, 8, 2 }));
    s.turn (2, -1);
    EXPECT_EQ (s.cursors[2], analogInput);
}

TEST (Stems, AFastTurnCountsEveryClickInTheRing)
{
    Stems s;
    s.cursors[2] = 2;
    s.turn (2, +7);
    EXPECT_EQ (s.cursors[2], 6);
}

TEST (Stems, ATurnFromOutsideTheRingLandsInIt)
{
    Stems s;
    s.cursors[1] = 0;
    s.turn (1, 1);
    EXPECT_EQ (s.cursors[1], 1);
}

TEST (Stems, TurningSwitchesNothing)
{
    auto s = stemsWith ({ { 3, bit (1) } });
    s.turn (0, -5);
    EXPECT_EQ (s.channelMask (0), 1 << 2);
}

TEST (Stems, PushOnAStemMakesItTheOnlyInput)
{
    auto s = stemsWith ({ { 5, bit (1) } });
    s.returnMode = analogMode;
    s.cursors[0] = 0;   // D1, stem 1 = pair 1
    const auto commands = s.push (0);
    ASSERT_GE (commands.size(), 2u);
    EXPECT_EQ (Commands (commands.begin(), commands.begin() + 2), (Commands { { 1, 1, true }, { 1, aux, false } }));
    EXPECT_TRUE (has (commands, { 5, 1, false }));
    EXPECT_EQ (s.channelMask (0), 1);
    EXPECT_EQ (s.sources[0], Source (stemSource));
}

TEST (Stems, ChannelOnesSecondStemIsCursor4)
{
    Stems s;
    s.returnMode = analogMode;
    s.cursors[0] = 4;   // D2, stem 1 = pair 5
    s.push (0);
    EXPECT_EQ (s.placeOf (5), Source (0));
}

TEST (Stems, PushOnWhatPlaysChangesNothing)
{
    // Until 2026-10-14 it released the stem to AUX; analog is A's now, and A leaves StemDeck alone.
    auto s = tidied (stemMode, { { 1, bit (1) } });
    s.sources[0] = stemSource;
    s.cursors[0] = 0;
    EXPECT_EQ (s.push (0), Commands());
    EXPECT_EQ (s.channelMask (0), 1);
    EXPECT_EQ (s.sources[0], Source (stemSource));
}

TEST (Stems, PushOnTheStemOnTheBusWhileAnalogPlaysHearsIt)
{
    // The stem stayed on bus 1 while A played: a push only opens the stems send, StemDeck has nothing to do.
    auto s = tidied (stemMode, { { 1, bit (1) } });
    s.cursors[0] = 0;
    EXPECT_EQ (s.push (0), Commands());
    EXPECT_EQ (s.sources[0], Source (stemSource));
}

TEST (Stems, TheOtherDecksStemNMovesToAux)
{
    auto s = stemsWith ({ { 1, bit (1) } });
    s.returnMode = analogMode;
    s.cursors[0] = 4;   // pair 5
    const auto commands = s.push (0);
    EXPECT_EQ (sorted (commands), sorted ({ { 5, 1, true }, { 5, aux, false }, { 1, 1, false }, { 1, aux, true } }));
    EXPECT_EQ (s.placeOf (5), Source (0));
    EXPECT_EQ (s.placeOf (1), Source (returnIndex));
}

TEST (Stems, APushOnAForeignStemIsRefused)
{
    auto s = stemsWith ({ { 1, bit (1) } });
    s.cursors[0] = 1;   // pair 2, own channel 2
    s.sources[0] = analogSource;
    const auto masks = s.masks;
    EXPECT_EQ (s.push (0), Commands());
    EXPECT_EQ (s.masks, masks);
    EXPECT_EQ (s.sources[0], Source (analogSource));
}

TEST (Stems, AReportDoesNotMoveTheCursor)
{
    Stems s;
    s.cursors[0] = 5;
    s.report (2, bit (1));
    EXPECT_EQ (s.cursors[0], 5);
}

// TheAnalogInput

namespace
{
Commands pushA (Stems& s, int index = 0, bool connected = true)
{
    s.cursors[index] = analogInput;
    return s.push (index, connected);
}

Commands select (Stems& s, int index, int pair)
{
    s.cursors[index] = pair - 1;
    return s.push (index);
}
}

TEST (Stems, WithoutAStoredSourceAChannelIsUndecided)
{
    // Decided by StemDeck's first settled report (maintainer, 2026-10-14).
    EXPECT_EQ (Stems().sources, undecided());
}

TEST (Stems, AOnAnUndecidedChannelDecidesAnalog)
{
    Stems s;
    pushA (s, 0, false);
    EXPECT_EQ (s.sources[0], Source (analogSource));
}

TEST (Stems, APushOnASendsNothingToStemdeck)
{
    for (int mode : { analogMode, stemMode })
    {
        auto s = tidied (mode, { { 3, bit (1) } });
        s.sources[0] = stemSource;
        const auto masks = s.masks;
        EXPECT_EQ (pushA (s), Commands()) << mode;
        EXPECT_EQ (s.masks, masks) << mode;
        EXPECT_EQ (s.sources[0], Source (analogSource)) << mode;
    }
}

TEST (Stems, TheStemStaysOnItsBusAndOffAux)
{
    auto s = tidied (stemMode, { { 1, bit (1) } });
    select (s, 0, 1);
    pushA (s);
    EXPECT_EQ (s.placeOf (1), Source (0));
    EXPECT_FALSE (s.playsOnReturn (1));
}

TEST (Stems, ASwitchesWithoutStemdeckToo)
{
    // REAPER's sends are Core's; StemDeck is not asked.
    auto s = stemsWith ({ { 1, bit (1) } });
    s.sources[0] = stemSource;
    EXPECT_EQ (pushA (s, 0, false), Commands());
    EXPECT_EQ (s.sources[0], Source (analogSource));
}

TEST (Stems, AOnOneChannelLeavesTheOthersAlone)
{
    auto s = tidied (analogMode, { { 1, bit (1) }, { 6, bit (2) } });
    s.sources = { stemSource, stemSource, stemSource, stemSource };
    pushA (s);
    EXPECT_EQ (s.sources, (std::array<Source, 4> { analogSource, stemSource, stemSource, stemSource }));
    EXPECT_EQ (s.placeOf (6), Source (1));
}

TEST (Stems, AStemAfterAIsHeardAgain)
{
    Stems s;
    s.returnMode = analogMode;
    select (s, 0, 1);
    pushA (s);
    EXPECT_EQ (select (s, 0, 1), Commands());
    EXPECT_EQ (s.sources[0], Source (stemSource));
    EXPECT_EQ (s.placeOf (1), Source (0));
}

TEST (Stems, StemdecksReportsNeverChangeTheSource)
{
    // Auto-DJ or a click in StemDeck moves stems on and off bus N; channel N keeps what the desk chose.
    Stems s;
    s.sources = { stemSource, analogSource, stemSource, analogSource };
    s.report (1, bit (2));
    s.report (2, bit (3));
    s.tidy();
    s.report (1, 0);
    s.forget();
    EXPECT_EQ (s.sources, (std::array<Source, 4> { stemSource, analogSource, stemSource, analogSource }));
}

TEST (Stems, NoStemIsRemembered)
{
    Stems s;
    select (s, 0, 1);
    const auto data = s.asData();
    EXPECT_FALSE (data.hasProperty ("last_stems"));
    EXPECT_EQ (data.getDynamicObject()->getProperties().size(), 4);
}

// WithoutStemDeck

TEST (Stems, TheCursorMovesTheSwitchesDoNot)
{
    Stems s;
    s.turn (0, -1);
    EXPECT_EQ (s.push (0, false), Commands());
    EXPECT_EQ (s.cursors[0], 4);
    EXPECT_EQ (s.channelMask (0), 0);
}

TEST (Stems, AStemNobodyPlaysDoesNotShutTheAnalogInput)
{
    Stems s;
    s.sources[0] = analogSource;
    s.cursors[0] = 0;
    s.push (0, false);
    EXPECT_EQ (s.sources[0], Source (analogSource));
}

TEST (Stems, AStemPushWithoutStemdeckDecidesNothing)
{
    Stems s;
    s.cursors[0] = 0;
    s.push (0, false);
    EXPECT_EQ (s.sources[0], std::nullopt);
}

TEST (Stems, ADoesNotTouchTheMirrorEither)
{
    auto s = stemsWith ({ { 1, bit (1) } });
    EXPECT_EQ (s.push (0, false), Commands());
    EXPECT_EQ (s.channelMask (0), 1);
}

TEST (Stems, TheReturnModeSwitchesWithoutCommands)
{
    auto s = stemsWith ({ { 2, bit (aux) } });
    s.returnCursor = analogMode;
    EXPECT_EQ (s.push (returnIndex, false), Commands());
    EXPECT_EQ (s.returnMode, analogMode);
    EXPECT_TRUE (s.playsOnReturn (2));
}

// OnePerChannel

TEST (Stems, TwoStemsOnABusKeepTheLowestWhateverTheirDeck)
{
    auto s = stemsWith ({ { 5, bit (1) }, { 1, bit (1) } });
    s.returnMode = analogMode;
    const auto commands = s.tidy();
    Commands onChannels;
    for (const auto& c : commands)
        if (c.bus != aux)
            onChannels.push_back (c);
    EXPECT_EQ (onChannels, (Commands { { 5, 1, false } }));
    EXPECT_TRUE (has (commands, { 5, aux, true }));
    EXPECT_EQ (s.channelMask (0), 1);
}

TEST (Stems, AStemReportedOnAnotherChannelGoesToAux)
{
    auto s = stemsWith ({ { 2, bit (1) } });
    const auto commands = s.tidy();
    EXPECT_TRUE (has (commands, { 2, 1, false }));
    EXPECT_TRUE (has (commands, { 2, aux, true }));
    EXPECT_EQ (s.placeOf (2), Source (returnIndex));
    EXPECT_EQ (s.tidy(), Commands());
}

TEST (Stems, AForeignStemNeverDisplacesAChannelsOwn)
{
    // Pair 2 (stem 2) and pair 5 (stem 1 of deck 2) both on bus 1: the foreign one goes first, so
    // the one-per-bus rule cannot keep it.
    auto s = stemsWith ({ { 2, bit (1) }, { 5, bit (1) } });
    const auto commands = s.tidy();
    EXPECT_EQ (s.channelMask (0), 1 << 4);
    EXPECT_EQ (s.placeOf (5), Source (0));
    EXPECT_EQ (s.placeOf (2), Source (returnIndex));
    EXPECT_TRUE (has (commands, { 2, 1, false }));
    EXPECT_TRUE (has (commands, { 2, aux, true }));
    EXPECT_FALSE (has (commands, { 5, 1, false }));
    EXPECT_EQ (s.tidy(), Commands());
}

TEST (Stems, TidyTwiceIsNothing)
{
    auto s = stemsWith ({ { 3, bit (1) }, { 6, bit (1) }, { 4, bit (4) }, { 8, bit (4) } });
    s.tidy();
    EXPECT_EQ (s.tidy(), Commands());
}

// ReturnModes

TEST (Stems, AStemOnNoBusStaysOff)
{
    // StemDeck's Auto-DJ leaves the incoming deck's stems on no bus at all until their switch;
    // putting them on AUX would play them early.
    auto s = stemsWith ({ { 1, bit (1) } });
    EXPECT_EQ (s.tidy(), Commands());
    EXPECT_EQ (std::vector<int> (s.masks.begin() + 1, s.masks.end()), std::vector<int> (7, 0));
    EXPECT_FALSE (s.playsOnReturn (1));
}

TEST (Stems, AStemOnAChannelLeavesAux)
{
    auto s = stemsWith ({ { 1, bit (1) | bit (aux) } });
    EXPECT_TRUE (has (s.tidy(), { 1, aux, false }));
}

TEST (Stems, AnalogModeTakesAStemOnAChannelOffAuxToo)
{
    // One stem per bus whatever the mode: Core switches the return's source, so AUX is silent on
    // the return in ANALOG mode.
    auto s = stemsWith ({ { 1, bit (1) | bit (aux) }, { 2, bit (aux) } });
    s.returnMode = analogMode;
    EXPECT_EQ (sorted (s.tidy()), (Commands { { 1, aux, false } }));
    EXPECT_EQ (s.channelMask (0), 1);
    EXPECT_TRUE (s.playsOnReturn (2));
}

TEST (Stems, InBothModesTheRulesHoldAndAnOffStemStaysOff)
{
    std::mt19937 rng (20261007);
    for (int mode : { analogMode, stemMode })
    {
        for (int round = 0; round < 300; ++round)
        {
            Stems s;
            for (int pair = 1; pair <= Stems::pairs; ++pair)
                s.report (pair, static_cast<int> (rng() % (1u << aux)));
            s.returnMode = mode;
            std::vector<int> off;
            for (int pair = 1; pair <= Stems::pairs; ++pair)
                if (s.masks[pair - 1] == 0)
                    off.push_back (pair);
            s.tidy();
            for (int index = 0; index < Stems::channels; ++index)
                EXPECT_LE (__builtin_popcount (s.channelMask (index)), 1);
            for (int pair = 1; pair <= Stems::pairs; ++pair)
            {
                int channels = 0;
                for (int c = 0; c < Stems::channels; ++c)
                    channels += (s.channelMask (c) >> (pair - 1)) & 1;
                EXPECT_LE (channels, 1);
                if (channels)
                    EXPECT_FALSE (s.playsOnReturn (pair));
            }
            for (int pair : off)
                EXPECT_EQ (s.masks[pair - 1], 0) << mode << " " << pair;
            EXPECT_EQ (s.tidy(), Commands());
        }
    }
}

TEST (Stems, TheEncoderRunsARingOfThree)
{
    // The display's order, left to right: STEM, ANALOG, CUE, and round again (spec return-cue, 2026-10-04).
    Stems s;
    EXPECT_EQ (s.returnCursor, stemMode);
    s.turn (returnIndex, +1);
    EXPECT_EQ (s.returnCursor, analogMode);
    s.turn (returnIndex, +1);
    EXPECT_EQ (s.returnCursor, cueField);
    s.turn (returnIndex, +1);
    EXPECT_EQ (s.returnCursor, stemMode);
}

TEST (Stems, LeftFromStemIsCue)
{
    Stems s;
    s.turn (returnIndex, -1);
    EXPECT_EQ (s.returnCursor, cueField);
}

TEST (Stems, AFastTurnCountsEveryClick)
{
    Stems s;
    s.turn (returnIndex, +2);
    EXPECT_EQ (s.returnCursor, cueField);
    s.turn (returnIndex, -4);
    EXPECT_EQ (s.returnCursor, analogMode);
}

TEST (Stems, PushOnCueSwitchesNothingAndKeepsTheMode)
{
    auto s = stemsWith ({ { 1, bit (1) }, { 2, bit (aux) } });
    s.returnMode = analogMode;
    s.returnCursor = cueField;
    const auto masks = s.masks;
    EXPECT_EQ (s.push (returnIndex), Commands());
    EXPECT_EQ (s.returnMode, analogMode);
    EXPECT_EQ (s.masks, masks);
}

TEST (Stems, PushSwitchesTheModeAndNoStem)
{
    // The mode chooses the return's source; StemDeck's switches stay as they are.
    for (const auto& [start, target] : std::vector<std::pair<int, int>> { { stemMode, analogMode }, { analogMode, stemMode } })
    {
        auto s = tidied (start, { { 1, bit (1) }, { 6, bit (3) } });
        const auto masks = s.masks;
        s.returnCursor = target;
        EXPECT_EQ (s.push (returnIndex), Commands());
        EXPECT_EQ (s.returnMode, target);
        EXPECT_EQ (s.masks, masks);
    }
}

TEST (Stems, APutsNothingOnAux)
{
    // Until 2026-10-14 A released the stem onto AUX.
    auto s = tidied (stemMode, { { 1, bit (1) } });
    s.cursors[0] = analogInput;
    EXPECT_EQ (s.push (0), Commands());
    EXPECT_FALSE (s.playsOnReturn (1));
}

TEST (Stems, AnalogModeLeavesTheStemsOnNoBusOff)
{
    auto s = stemsWith ({ { 1, bit (1) }, { 2, bit (2) }, { 3, bit (3) }, { 4, bit (4) } });   // 1-4 on channels 1-4
    s.returnMode = analogMode;
    EXPECT_EQ (s.tidy(), Commands());
    s.returnCursor = stemMode;
    EXPECT_EQ (s.push (returnIndex), Commands());
}

// TheCueLeavesStemDeckAlone: the mirror has no cue commands and no C switch to clear -- there is
// nothing to call, which a compiler enforces better than a test; only the report is pinned.

TEST (Stems, TheMirrorHasNoCueCommands)
{
    // A push on the return's CUE field is the only cue the mirror knows, and it says nothing.
    Stems s;
    s.returnCursor = cueField;
    EXPECT_EQ (s.push (returnIndex), Commands());
}

TEST (Stems, ThereIsNoCSwitchToClear)
{
    // StemDeck's C switches are gone (2026-10-07): a mask's sixth bit is never kept.
    Stems s;
    s.report (1, bit (1) | bit (aux) | (1 << 5));
    EXPECT_EQ (s.masks[0] >> aux, 0);
}

TEST (Stems, AnOlderStemdecksCBitIsNotABus)
{
    // A StemDeck from before 2026-10-07 may still report its C (bit 5): the stem is taken at its
    // word on buses 1-5 and the C is dropped, not the whole report.
    Stems s;
    EXPECT_TRUE (s.report (1, bit (1) | (1 << 5)));
    EXPECT_EQ (s.masks[0], bit (1));
}

// TheFirstReportDecides

TEST (Stems, ABusWithAStemStartsOnStem)
{
    auto s = stemsWith ({ { 1, bit (1) }, { 6, bit (3) }, { 2, bit (aux) } });
    s.adoptSources();
    EXPECT_EQ (s.sources, (std::array<Source, 4> { stemSource, analogSource, stemSource, analogSource }));
}

TEST (Stems, AStoredSourceWins)
{
    auto s = stemsWith ({ { 1, bit (1) } });
    s.sources = { analogSource, stemSource, std::nullopt, std::nullopt };
    s.report (2, bit (3));
    s.adoptSources();
    EXPECT_EQ (s.sources, (std::array<Source, 4> { analogSource, stemSource, stemSource, analogSource }));
}

TEST (Stems, OnlyTheFirstReportDecides)
{
    Stems s;
    s.adoptSources();
    s.report (1, bit (1));
    s.adoptSources();
    EXPECT_EQ (s.sources, (std::array<Source, 4> { analogSource, analogSource, analogSource, analogSource }));
}

TEST (Stems, AnUndecidedChannelIsWrittenDownUndecided)
{
    Stems s;
    s.sources[1] = stemSource;
    EXPECT_EQ (Stems::fromData (s.asData()).sources, (std::array<Source, 4> { std::nullopt, stemSource, std::nullopt, std::nullopt }));
}

// StateOnDisk

TEST (Stems, TheSourcesAreKept)
{
    Stems s;
    s.sources = { stemSource, analogSource, stemSource, stemSource };
    EXPECT_EQ (Stems::fromData (s.asData()).sources, s.sources);
}

TEST (Stems, AFileWithoutSourcesLeavesThemUndecided)
{
    EXPECT_EQ (Stems::fromData (parsed (R"({"cursors": [1, 2, 3, 4]})")).sources, undecided());
}

TEST (Stems, GarbledSourcesAreUndecided)
{
    for (const char* sources : { R"("x")", "[1, 1, 1]", "[1, 1, 1, 2]", "[true, 1, 1, 1]", "null" })
        EXPECT_EQ (Stems::fromData (parsed ((std::string (R"({"sources": )") + sources + "}").c_str())).sources, undecided()) << sources;
}

TEST (Stems, CursorsAndModeAreKept)
{
    Stems s;
    s.cursors = { analogInput, 1, 2, 7 };
    s.returnCursor = analogMode;
    s.returnMode = analogMode;
    const auto back = Stems::fromData (s.asData());
    EXPECT_EQ (back.cursors, s.cursors);
    EXPECT_EQ (back.returnCursor, analogMode);
    EXPECT_EQ (back.returnMode, analogMode);
}

TEST (Stems, AStoredCursorOutsideTheRingFallsBackToA)
{
    EXPECT_EQ (Stems::fromData (parsed (R"({"cursors": [0, 0, 6, 7]})")).cursors, (std::array<int, 4> { 0, analogInput, 6, 7 }));
}

TEST (Stems, TheCursorOnCueIsKept)
{
    Stems s;
    s.returnCursor = cueField;
    EXPECT_EQ (Stems::fromData (s.asData()).returnCursor, cueField);
}

TEST (Stems, CueIsNeverAMode)
{
    EXPECT_EQ (Stems::fromData (parsed (R"({"return_mode": 2})")).returnMode, stemMode);
    EXPECT_EQ (Stems::fromData (parsed (R"({"return_cursor": 3})")).returnCursor, stemMode);
}

TEST (Stems, AnOldFileWithLastStemsStillLoads)
{
    const auto s = Stems::fromData (parsed (R"({"last_stems": [3, null, 8, 1], "cursors": [4, 5, 2, 3]})"));
    EXPECT_EQ (s.cursors, (std::array<int, 4> { 4, 5, 2, 3 }));
}

TEST (Stems, AnOldOrGarbledFileNeverRaises)
{
    for (const char* data : { "null", "{}", R"({"menus": [[0, 0], [0, 0], [0, 0], [0, 0]]})", R"({"cursors": "x"})",
                              R"({"cursors": [9, 0, 0, 0]})", R"({"cursors": [0, 0, 0]})", R"({"cursors": [true, 0, 0, 0]})",
                              R"({"return_mode": 7})", R"({"cursors": [1e999, 0, 0, 0]})", R"({"cursors": [1.0, 0, 0, 0]})", "[1]", "3" })
    {
        const auto s = Stems::fromData (parsed (data));
        EXPECT_EQ (s.cursors, (std::array<int, 4> { analogInput, analogInput, analogInput, analogInput })) << data;
        EXPECT_EQ (s.returnMode, stemMode) << data;
    }
}
