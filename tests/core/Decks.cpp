// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3-core's tools/tests/test_core_decks.py and the DeckPortFunction cases of
// test_core_deck_ports.py (B1 7a7b2441f74e): Core's engine-mode deck selector -- the desk's input
// ring (deck A, deck B, ..., analog) on each channel, the deck 3D that follows it, and deck
// presence. Pure: the clock is passed in.
#include <gtest/gtest.h>
#include <type_traits>

#include "B0Truth.h"
#include "Decks.h"

using namespace a3core;
using Changes = std::vector<std::pair<int, int>>;
using Said = std::vector<std::pair<std::string, OscArg>>;

// A8: no deck count of its own; whoever builds one says how many decks (Core: the truth's range).
TEST (Decks, TheSelectorHasNoDefaultDeckCount)
{
    EXPECT_FALSE ((std::is_default_constructible_v<DeckSelector>));
    EXPECT_FALSE ((std::is_constructible_v<DeckSelector, int>));
}

namespace
{
constexpr int analog = DeckSelector::analog;

const Truth& truth() { return b0::truth(); }

std::vector<int> ringOf (const DeckSelector& s, int channel) { return s.ring (channel); }

int atCursor (const DeckSelector& s, int channel) { return s.ring (channel)[static_cast<size_t> (s.cursor (channel))]; }

// Turn channel's cursor onto `source` and push; the changes.
Changes pick (DeckSelector& selector, int channel, int source)
{
    while (atCursor (selector, channel) != source)
        selector.turn (channel, 1);
    return selector.push (channel);
}

juce::var parsed (const char* text)
{
    juce::String error;
    return parseJson (text, error);
}

std::string evening (const DeckSelector& s) { return canonicalBytes (s.toEvening()); }

Truth withDeckBase (int port)
{
    auto data = truth().data();
    auto* listeners = data["listeners"].getArray();
    for (auto& listener : *listeners)
        if (listener["name"].toString() == "stemdeck.osc")
            listener.getDynamicObject()->setProperty ("port", port);
    return Truth (data);
}
}

// TheRing

TEST (Decks, RingIsDecksThenAnalog)
{
    // The desk reads D1 D2 ... A, analog last (maintainer, 2026-10-26).
    EXPECT_EQ (ringOf (DeckSelector (4, 2), 1), (std::vector<int> { 1, 2, analog }));
    EXPECT_EQ (ringOf (DeckSelector (4, 4), 1), (std::vector<int> { 1, 2, 3, 4, analog }));
}

TEST (Decks, CursorStartsOnAnalog)
{
    DeckSelector s (4, 2);
    EXPECT_EQ (atCursor (s, 1), analog);
}

TEST (Decks, TurnWrapsBothWays)
{
    DeckSelector s (4, 2);
    s.turn (1, 1);
    EXPECT_EQ (atCursor (s, 1), 1);
    s.turn (1, -2);
    EXPECT_EQ (atCursor (s, 1), 2);
    s.turn (1, 1);
    EXPECT_EQ (atCursor (s, 1), analog);
}

TEST (Decks, TurnAloneChangesNoSource)
{
    DeckSelector s (4, 2);
    s.turn (1, 1);
    EXPECT_EQ (s.source (1), analog);
}

TEST (Decks, PushSelectsTheCursor)
{
    DeckSelector s (4, 2);
    s.turn (2, 1);   // from analog, past the end, onto deck 1
    EXPECT_EQ (s.push (2), (Changes { { 2, 1 } }));
    EXPECT_EQ (s.source (2), 1);
    EXPECT_EQ (s.channelOf (1), std::optional<int> (2));
    EXPECT_TRUE (s.deckChannel (2));
    EXPECT_FALSE (s.deckChannel (1));
}

TEST (Decks, ADeckIsOnOneChannelAtMost)
{
    DeckSelector s (4, 2);
    pick (s, 1, 1);
    const auto changes = pick (s, 3, 1);
    EXPECT_EQ (changes, (Changes { { 3, 1 }, { 1, analog } }));
    EXPECT_EQ (s.source (1), analog);
    EXPECT_EQ (s.channelOf (1), std::optional<int> (3));
    EXPECT_EQ (atCursor (s, 1), analog);
}

TEST (Decks, TwoDecksMaySitOnTwoChannels)
{
    DeckSelector s (4, 2);
    pick (s, 1, 1);
    pick (s, 2, 2);
    EXPECT_EQ ((std::vector<int> { s.source (1), s.source (2), s.source (3), s.source (4) }), (std::vector<int> { 1, 2, 0, 0 }));
}

TEST (Decks, PushOnTheSameSourceChangesNothing)
{
    DeckSelector s (4, 2);
    pick (s, 1, 2);
    EXPECT_TRUE (s.push (1).empty());
    EXPECT_TRUE (s.push (4).empty());   // analog on analog
}

TEST (Decks, ChannelOfAFreeDeckIsNone)
{
    EXPECT_EQ (DeckSelector (4, 2).channelOf (1), std::nullopt);
}

// TheThreeD

TEST (Decks, ThreeDGoesToTheDeckOnThatChannelOnly)
{
    DeckSelector s (4, 2);
    pick (s, 2, 1);
    EXPECT_EQ (s.threeDMessages (truth(), 2, 0.4), (Said { { "/stemdeck/1/3d", pyFloat (0.4) } }));
    EXPECT_TRUE (s.threeDMessages (truth(), 1, 0.4).empty());
}

TEST (Decks, ALeavingDeckGetsNeutralThreeD)
{
    DeckSelector s (4, 2);
    pick (s, 1, 2);
    const auto changes = pick (s, 1, analog);
    EXPECT_EQ (s.leaveMessages (truth(), changes), (Said { { "/stemdeck/2/3d", pyFloat (1.0) } }));
}

TEST (Decks, ADeckThatOnlyMovesIsNotNeutralised)
{
    DeckSelector s (4, 2);
    pick (s, 1, 1);
    const auto changes = pick (s, 2, 1);
    EXPECT_TRUE (s.leaveMessages (truth(), changes).empty());
}

TEST (Decks, ADeckDisplacedByAnotherPickOnItsChannelLeaves)
{
    DeckSelector s (4, 2);
    pick (s, 1, 1);
    const auto changes = pick (s, 1, 2);
    EXPECT_EQ (s.leaveMessages (truth(), changes), (Said { { "/stemdeck/1/3d", pyFloat (1.0) } }));
}

TEST (Decks, APickWithoutALeaverSendsNoLeave)
{
    DeckSelector s (4, 2);
    const auto changes = pick (s, 1, 1);
    EXPECT_TRUE (s.leaveMessages (truth(), changes).empty());
}

TEST (Decks, HelloResendsTheThreeD)
{
    DeckSelector s (4, 2);
    pick (s, 3, 2);
    EXPECT_EQ (s.helloMessages (truth(), 2, 0.7), (Said { { "/stemdeck/2/3d", pyFloat (0.7) } }));
}

TEST (Decks, HelloOfADeckOnNoChannelSendsNeutral)
{
    DeckSelector s (4, 2);
    EXPECT_EQ (s.helloMessages (truth(), 1, 0.7), (Said { { "/stemdeck/1/3d", pyFloat (1.0) } }));
}

// Presence

TEST (Decks, PresenceTimesOutAfterFiveSeconds)
{
    DeckSelector s (4, 2);
    EXPECT_FALSE (s.present (1, 0.0));
    s.heard (1, 100.0);
    EXPECT_TRUE (s.present (1, 100.0));
    EXPECT_TRUE (s.present (1, 105.0));
    EXPECT_FALSE (s.present (1, 105.01));
    EXPECT_FALSE (s.present (2, 100.0));
}

TEST (Decks, PresentWithHelloAndAudio)
{
    DeckSelector s (4, 2);
    s.heard (1, 100.0);
    s.heardAudio (1, true, 100.0);
    EXPECT_TRUE (s.present (1, 101.0));
}

TEST (Decks, AbsentWhenEngineReportsItsAudioMissing)
{
    DeckSelector s (4, 2);
    s.heard (1, 100.0);
    s.heardAudio (1, false, 100.0);
    EXPECT_FALSE (s.present (1, 101.0));
}

TEST (Decks, PresentOnHelloAloneWithoutEngineReports)
{
    DeckSelector s (4, 2);   // an engine that never reports (an older one)
    s.heard (1, 100.0);
    EXPECT_TRUE (s.present (1, 101.0));
}

TEST (Decks, AudioReportOlderThanFiveSecondsIsIgnored)
{
    DeckSelector s (4, 2);
    s.heardAudio (1, false, 100.0);
    s.heard (1, 105.0);
    EXPECT_FALSE (s.present (1, 105.0));
    s.heard (1, 105.5);
    EXPECT_TRUE (s.present (1, 105.5));
}

TEST (Decks, AudioReportAloneDoesNotMakeADeckPresent)
{
    DeckSelector s (4, 2);
    s.heardAudio (1, true, 100.0);
    EXPECT_FALSE (s.present (1, 100.0));
}

TEST (Decks, ALaterAudioReportReplacesAnEarlierOne)
{
    DeckSelector s (4, 2);
    s.heard (1, 100.0);
    s.heardAudio (1, false, 100.0);
    s.heardAudio (1, true, 101.0);
    EXPECT_TRUE (s.present (1, 101.0));
}

TEST (Decks, AbsentDeckKeepsItsChannel)
{
    DeckSelector s (4, 2);
    pick (s, 1, 1);
    s.heard (1, 0.0);
    EXPECT_FALSE (s.present (1, 60.0));
    EXPECT_EQ (s.source (1), 1);
    EXPECT_EQ (s.channelOf (1), std::optional<int> (1));
}

// OnDisk

TEST (Decks, RoundTrip)
{
    DeckSelector s (4, 2);
    pick (s, 2, 1);
    pick (s, 4, 2);
    EXPECT_EQ (evening (s), R"({"channel.source":[0,1,0,2]})");
    DeckSelector t (4, 2);
    t.fromEvening (s.toEvening());
    EXPECT_EQ ((std::vector<int> { t.source (1), t.source (2), t.source (3), t.source (4) }), (std::vector<int> { 0, 1, 0, 2 }));
    EXPECT_EQ (atCursor (t, 2), 1);
}

TEST (Decks, OldStemCursorsReadAsAnalog)
{
    DeckSelector t (4, 2);
    t.fromEvening (parsed (R"({"cursors": [0, 4, 8, 8], "sources": [1, 1, null, 0]})"));
    EXPECT_EQ (evening (t), R"({"channel.source":[0,0,0,0]})");
}

TEST (Decks, UnknownDecksAndJunkReadAsAnalog)
{
    DeckSelector t (4, 2);
    t.fromEvening (parsed (R"({"channel.source": [3, -1, true, "x"]})"));
    EXPECT_EQ (evening (t), R"({"channel.source":[0,0,0,0]})");
}

TEST (Decks, ADeckTwiceKeepsTheFirst)
{
    DeckSelector t (4, 2);
    t.fromEvening (parsed (R"({"channel.source": [0, 1, 1, 2]})"));
    EXPECT_EQ (evening (t), R"({"channel.source":[0,1,0,2]})");
}

TEST (Decks, NotADictOrWrongLengthIsIgnored)
{
    for (const char* junk : { "null", "[]", R"({"channel.source": [1, 1]})", R"({"channel.source": "1"})" })
    {
        DeckSelector t (4, 2);
        t.fromEvening (parsed (junk));
        EXPECT_EQ (evening (t), R"({"channel.source":[0,0,0,0]})") << junk;
    }
}

TEST (Decks, ChannelSource4IsAcceptedAnd5IsRefusedWithFourDecks)
{
    DeckSelector s (4, 4);
    s.fromEvening (parsed (R"({"channel.source": [4, 0, 0, 0]})"));
    EXPECT_EQ (s.source (1), 4);
    s.fromEvening (parsed (R"({"channel.source": [5, 0, 0, 0]})"));
    EXPECT_EQ (s.source (1), 0);
}

// DeckPortFunction

TEST (Decks, DeckKIsTheBasePlusKMinusOne)
{
    const auto base = truth().endpoint ("stemdeck", "osc").port;
    EXPECT_EQ ((std::vector<int> { deckPort (truth(), 1), deckPort (truth(), 2), deckPort (truth(), 3), deckPort (truth(), 4) }),
               (std::vector<int> { base, base + 1, base + 2, base + 3 }));
}

TEST (Decks, TheBaseFollowsTheTruth)
{
    const auto moved = withDeckBase (9000);
    EXPECT_EQ (deckPort (moved, 1), 9000);
    EXPECT_EQ (deckPort (moved, 2), 9001);
}

TEST (Decks, DeckKListensOnItsOwnPort)
{
    // The ruling P13: deck 1 on the truth's port, deck 2 one above, on the host given.
    const auto base = truth().endpoint ("stemdeck", "osc").port;
    EXPECT_EQ (deckEndpoint (truth(), "10.0.0.7", 1), (Endpoint { "10.0.0.7", base }));
    EXPECT_EQ (deckEndpoint (truth(), "10.0.0.7", 2), (Endpoint { "10.0.0.7", base + 1 }));
    const auto moved = withDeckBase (9000);
    EXPECT_EQ (deckEndpoint (moved, "10.0.0.7", 2), (Endpoint { "10.0.0.7", 9001 }));
}
