// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "ChannelSelect.h"

namespace
{
constexpr double rate = 44100.0;
constexpr int fade = 441;

struct Sample { float analog, deck; int slot; bool resetDeck, resetAnalog; };

// Walks the select over `total` samples in calls of the given sizes (cycled),
// expanding every step into per-sample gains.
std::vector<Sample> walk (a3::ChannelSelect& sel, const std::vector<int>& requests, int total,
                          const std::vector<int>& calls)
{
    std::vector<Sample> out;
    size_t c = 0;
    while ((int) out.size() < total)
    {
        const int size = std::min (calls[c++ % calls.size()], total - (int) out.size());
        const int request = requests[std::min (out.size(), requests.size() - 1)];
        int left = size;
        while (left > 0)
        {
            const auto s = sel.advance (request, left);
            EXPECT_GT (s.length, 0);
            EXPECT_LE (s.length, left);
            if (s.length <= 0)
                return out;
            for (int j = 0; j < s.length; ++j)
            {
                const float t = (float) (j + 1) / (float) s.length;
                out.push_back ({ s.analogFrom + (s.analogTo - s.analogFrom) * t,
                                 s.deckFrom + (s.deckTo - s.deckFrom) * t, s.deckSlot,
                                 j == 0 && s.resetDeck, j == 0 && s.resetAnalog });
            }
            left -= s.length;
        }
    }
    return out;
}

// requests per start sample: request[0] applies from the start; a leading run is built by `until`.
std::vector<int> request (int first, int then, int from)
{
    std::vector<int> r ((size_t) from + 1, first);
    r.back() = then;
    return r;
}
}

TEST (ChannelSelect, AnalogToDeckCrossfadesInTenMs)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, true);
    // settled on analog for 100 samples, then deck 1
    auto g = walk (sel, request (0, 1, 100), 100 + fade + 50, { 100 });
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_FLOAT_EQ (g[(size_t) i].analog, 1.0f);
        EXPECT_FLOAT_EQ (g[(size_t) i].deck, 0.0f);
    }
    for (int i = 100; i < 100 + fade; ++i)
    {
        const auto& s = g[(size_t) i];
        EXPECT_NEAR (s.analog + s.deck, 1.0f, 1e-5f) << i;
        EXPECT_LE (s.analog, g[(size_t) i - 1].analog + 1e-7f);
        EXPECT_GE (s.deck, g[(size_t) i - 1].deck - 1e-7f);
    }
    EXPECT_FLOAT_EQ (g[100 + fade - 1].deck, 1.0f);
    EXPECT_FLOAT_EQ (g[100 + fade - 1].analog, 0.0f);
    EXPECT_FLOAT_EQ (g.back().deck, 1.0f);
    EXPECT_FALSE (sel.analogActive());
    EXPECT_TRUE (sel.deckActive());
    EXPECT_EQ (sel.deckSlot(), 1);
    EXPECT_TRUE (g[100].resetDeck);   // the deck path starts from a clean state
}

TEST (ChannelSelect, BothPathsRunOnlyDuringTheFade)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, true);
    EXPECT_TRUE (sel.analogActive());
    EXPECT_FALSE (sel.deckActive());
    sel.advance (1, 10);
    EXPECT_TRUE (sel.analogActive());
    EXPECT_TRUE (sel.deckActive());
    int left = fade;
    while (left > 0)
        left -= sel.advance (1, left).length;
    EXPECT_FALSE (sel.analogActive());
    EXPECT_TRUE (sel.deckActive());
}

TEST (ChannelSelect, DeckToDeckDipsThroughZero)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 3, false);   // deck only: starts on slot 1
    auto g = walk (sel, request (1, 2, 50), 50 + 2 * fade + 30, { 50 });
    EXPECT_FLOAT_EQ (g[49].deck, 1.0f);
    for (int i = 50; i < 50 + fade; ++i)
    {
        EXPECT_EQ (g[(size_t) i].slot, 1);
        EXPECT_LE (g[(size_t) i].deck, g[(size_t) i - 1].deck + 1e-7f);
        EXPECT_FLOAT_EQ (g[(size_t) i].analog, 0.0f);
    }
    EXPECT_FLOAT_EQ (g[50 + fade - 1].deck, 0.0f);
    EXPECT_TRUE (g[50 + fade].resetDeck);   // swapped at zero, reset before it fades in
    for (int i = 50 + fade; i < 50 + 2 * fade; ++i)
    {
        EXPECT_EQ (g[(size_t) i].slot, 2);
        EXPECT_GE (g[(size_t) i].deck, g[(size_t) i - 1].deck - 1e-7f);
    }
    EXPECT_FLOAT_EQ (g.back().deck, 1.0f);
    EXPECT_EQ (sel.deckSlot(), 2);
}

TEST (ChannelSelect, UnknownSourceKeepsTheLast)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, true);
    for (int bad : { -1, 3, 99 })
    {
        const auto s = sel.advance (bad, 64);
        EXPECT_FLOAT_EQ (s.analogFrom, 1.0f);
        EXPECT_FLOAT_EQ (s.analogTo, 1.0f);
        EXPECT_FLOAT_EQ (s.deckTo, 0.0f);
        EXPECT_EQ (s.length, 64);
    }
    int left = fade;
    while (left > 0)
        left -= sel.advance (2, left).length;
    const auto s = sel.advance (3, 64);
    EXPECT_FLOAT_EQ (s.deckTo, 1.0f);
    EXPECT_EQ (s.deckSlot, 2);
    EXPECT_EQ (sel.advance (-1, 64).deckSlot, 2);
}

TEST (ChannelSelect, DeckOnlyChannelRefusesAnalog)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, false);
    const auto s = sel.advance (0, 64);
    EXPECT_FLOAT_EQ (s.analogTo, 0.0f);
    EXPECT_FLOAT_EQ (s.deckFrom, 1.0f);
    EXPECT_FLOAT_EQ (s.deckTo, 1.0f);
    EXPECT_FALSE (sel.analogActive());
    EXPECT_EQ (s.deckSlot, 1);
}

TEST (ChannelSelect, RequestDuringAFadeFinishesCleanly)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, true);
    // analog -> deck 1, reverse back to analog after 200 samples
    std::vector<int> req (200, 1);
    req.push_back (0);
    auto g = walk (sel, req, 200 + fade + 20, { 64 });
    for (size_t i = 1; i < g.size(); ++i)
    {
        EXPECT_LE (std::fabs (g[i].analog - g[i - 1].analog), 1.0f / fade + 1e-6f) << i;
        EXPECT_LE (std::fabs (g[i].deck - g[i - 1].deck), 1.0f / fade + 1e-6f) << i;
        EXPECT_NEAR (g[i].analog + g[i].deck, 1.0f, 1e-5f) << i;
    }
    EXPECT_FLOAT_EQ (g.back().analog, 1.0f);
    EXPECT_FLOAT_EQ (g.back().deck, 0.0f);
}

TEST (ChannelSelect, SplitsAcrossBlocks)
{
    for (const bool hasAnalog : { true, false })   // crossfade, then dip
    {
        std::vector<Sample> ref;
        for (const auto& calls : { std::vector<int> { 64 }, std::vector<int> { 128 }, std::vector<int> { 333 },
                                   std::vector<int> { 1 }, std::vector<int> { 50, 77, 3 } })
        {
            a3::ChannelSelect sel;
            sel.prepare (rate, 2, hasAnalog);
            const auto g = walk (sel, std::vector<int> { hasAnalog ? 1 : 2 }, 2 * fade + 40, calls);
            if (ref.empty())
                ref = g;
            ASSERT_EQ (g.size(), ref.size());
            for (size_t i = 0; i < g.size(); ++i)
            {
                EXPECT_NEAR (g[i].analog, ref[i].analog, 1e-5f) << i;
                EXPECT_NEAR (g[i].deck, ref[i].deck, 1e-5f) << i;
                EXPECT_EQ (g[i].slot, ref[i].slot) << i;
            }
        }
    }
}

namespace
{
struct Routed { float analog, deck; int input, slot; bool resetAnalog, resetDeck; };
struct Request { int source; bool stereo; };

// One sample per entry; `switchAt` changes the request from `first` to `then`. Calls of `call` samples.
std::vector<Routed> walkRouted (a3::ChannelSelect& sel, Request first, Request then, int switchAt, int total, int call)
{
    std::vector<Routed> out;
    while ((int) out.size() < total)
    {
        const auto r = (int) out.size() < switchAt ? first : then;
        int left = std::min (call, total - (int) out.size());
        while (left > 0)
        {
            const auto s = sel.advance (r.source, left, r.stereo);
            if (s.length <= 0)
                return out;
            for (int j = 0; j < s.length; ++j)
            {
                const float t = (float) (j + 1) / (float) s.length;
                out.push_back ({ s.analogFrom + (s.analogTo - s.analogFrom) * t, s.deckFrom + (s.deckTo - s.deckFrom) * t,
                                 s.analogInput, s.deckSlot, j == 0 && s.resetAnalog, j == 0 && s.resetDeck });
            }
            left -= s.length;
        }
    }
    return out;
}
}

// Analog -> a stereo deck: the analog path dips out on its own pair, swaps to the deck's pair at zero
// (reset), and fades in; the deck path never runs.
TEST (ChannelSelect, AStereoDeckRunsThroughTheAnalogPath)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, true);
    const auto g = walkRouted (sel, { 0, false }, { 1, true }, 100, 100 + 2 * fade + 50, 100);
    for (const auto& s : g)
        EXPECT_FLOAT_EQ (s.deck, 0.0f);
    EXPECT_FLOAT_EQ (g[99].analog, 1.0f);
    for (int i = 100; i < 100 + fade; ++i)
    {
        EXPECT_EQ (g[(size_t) i].input, 0) << i;
        EXPECT_LE (g[(size_t) i].analog, g[(size_t) i - 1].analog + 1e-7f) << i;
    }
    EXPECT_FLOAT_EQ (g[100 + fade - 1].analog, 0.0f);
    EXPECT_TRUE (g[100 + fade].resetAnalog);
    for (int i = 100 + fade; i < 100 + 2 * fade; ++i)
    {
        EXPECT_EQ (g[(size_t) i].input, 1) << i;
        EXPECT_GE (g[(size_t) i].analog, g[(size_t) i - 1].analog - 1e-7f) << i;
    }
    EXPECT_FLOAT_EQ (g.back().analog, 1.0f);
    EXPECT_EQ (sel.analogInput(), 1);
    EXPECT_FALSE (sel.deckActive());
}

// The deck changes mode while it is selected: stereo -> B-format is the analog <-> deck crossfade.
TEST (ChannelSelect, AStereoDeckTurningBFormatCrossfadesToTheDeckPath)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, true);
    const int at = 1024;   // settled (> 2 x fade), on a call boundary of 64
    const auto g = walkRouted (sel, { 1, true }, { 1, false }, at, at + fade + 50, 64);
    EXPECT_FLOAT_EQ (g[(size_t) at - 1].analog, 1.0f);
    EXPECT_EQ (g[(size_t) at - 1].input, 1);
    for (int i = at; i < at + fade; ++i)
    {
        EXPECT_NEAR (g[(size_t) i].analog + g[(size_t) i].deck, 1.0f, 1e-5f) << i;
        EXPECT_EQ (g[(size_t) i].input, 1) << i;
        EXPECT_EQ (g[(size_t) i].slot, 1) << i;
    }
    EXPECT_TRUE (g[(size_t) at].resetDeck);
    EXPECT_FLOAT_EQ (g.back().deck, 1.0f);
    EXPECT_FLOAT_EQ (g.back().analog, 0.0f);

    // And back: B-format -> stereo crossfades the other way, the analog path rising on the deck's pair.
    const auto back = walkRouted (sel, { 1, true }, { 1, true }, 0, fade + 10, 64);
    EXPECT_TRUE (back[0].resetAnalog);
    for (int i = 0; i < fade; ++i)
    {
        EXPECT_NEAR (back[(size_t) i].analog + back[(size_t) i].deck, 1.0f, 1e-5f) << i;
        EXPECT_EQ (back[(size_t) i].input, 1) << i;
    }
    EXPECT_FLOAT_EQ (back.back().analog, 1.0f);
}

// One stereo deck to another: the analog path dips and swaps its input, as deck -> deck does on the deck path.
TEST (ChannelSelect, StereoDeckToStereoDeckDipsTheAnalogPath)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, true);
    const int at = 1024;   // settled (> 2 x fade), on a call boundary of 64
    const auto g = walkRouted (sel, { 1, true }, { 2, true }, at, at + 2 * fade + 10, 64);
    EXPECT_EQ (g[(size_t) at - 1].input, 1);
    EXPECT_FLOAT_EQ (g[(size_t) at + fade - 1].analog, 0.0f);
    EXPECT_EQ (g[(size_t) at + fade - 1].input, 1);
    EXPECT_TRUE (g[(size_t) at + fade].resetAnalog);
    EXPECT_EQ (g[(size_t) at + fade].input, 2);
    EXPECT_FLOAT_EQ (g.back().analog, 1.0f);
    for (const auto& s : g)
        EXPECT_FLOAT_EQ (s.deck, 0.0f);
}

// A channel without an analog pair has no analog path: a stereo deck there takes the deck path, as before.
TEST (ChannelSelect, ADeckOnlyChannelPlaysAStereoDeckOnItsDeckPath)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, false);
    const auto s = sel.advance (1, 64, true);
    EXPECT_FLOAT_EQ (s.analogTo, 0.0f);
    EXPECT_FLOAT_EQ (s.deckTo, 1.0f);
    EXPECT_EQ (s.analogInput, 0);
}

// Back to analog from a stereo deck: dip, swap to the own pair, fade in.
TEST (ChannelSelect, StereoDeckBackToAnalogSwapsToTheOwnPair)
{
    a3::ChannelSelect sel;
    sel.prepare (rate, 2, true);
    const int at = 1024;   // settled (> 2 x fade), on a call boundary of 64
    const auto g = walkRouted (sel, { 1, true }, { 0, false }, at, at + 2 * fade + 10, 64);
    EXPECT_EQ (g[(size_t) at - 1].input, 1);
    EXPECT_TRUE (g[(size_t) at + fade].resetAnalog);
    EXPECT_EQ (g.back().input, 0);
    EXPECT_FLOAT_EQ (g.back().analog, 1.0f);
}
