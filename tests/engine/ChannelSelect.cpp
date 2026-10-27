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

// Analog = 0 plays the analog pair, delay = 1 shuts it and hands the delay over to RETURN.
TEST (ReturnSelect, AnalogOrDelay)
{
    a3::ReturnSelect ret;
    ret.prepare (rate);
    const int n = 1000;
    std::vector<float> a (n, 0.25f), oL (n), oR (n), share (n, 9.0f);
    const float* analog[2] = { a.data(), a.data() };
    float* out[2] = { oL.data(), oR.data() };
    ret.process (analog, out, share.data(), n, a3::ReturnSelect::analog);
    EXPECT_EQ (oL[n - 1], 0.25f);
    EXPECT_EQ (share[0], 0.0f);
    EXPECT_EQ (share[n - 1], 0.0f);
    ret.process (analog, out, share.data(), n, a3::ReturnSelect::delay);
    EXPECT_EQ (oL[n - 1], 0.0f);
    EXPECT_EQ (oR[n - 1], 0.0f);
    EXPECT_EQ (share[n - 1], 1.0f);
    ret.process (analog, out, share.data(), n, a3::ReturnSelect::analog);
    EXPECT_EQ (oL[n - 1], 0.25f);
    EXPECT_EQ (share[n - 1], 0.0f);
}

TEST (ReturnSelect, CrossfadeTakesTenMs)
{
    a3::ReturnSelect ret;
    ret.prepare (rate);
    const int n = 1000;
    std::vector<float> a (n, 1.0f), oL (n), oR (n), share (n);
    const float* analog[2] = { a.data(), a.data() };
    float* out[2] = { oL.data(), oR.data() };
    ret.process (analog, out, share.data(), n, a3::ReturnSelect::delay);
    EXPECT_GT (oL[0], 0.99f);
    EXPECT_NEAR (oL[fade / 2], 0.5f, 0.01f);
    EXPECT_EQ (oL[fade], 0.0f);
    for (int i = 0; i < n; ++i)
        EXPECT_FLOAT_EQ (oL[i] + share[i], 1.0f) << i;   // linear: the two shares add up
    for (int i = 1; i < fade; ++i)
        EXPECT_LE (oL[i], oL[i - 1] + 1e-7f);
}

TEST (ReturnSelect, NullInputIsSilence)
{
    a3::ReturnSelect ret;
    ret.prepare (rate);
    const int n = 1000;
    std::vector<float> oL (n, 9.0f), oR (n, 9.0f), share (n);
    float* out[2] = { oL.data(), oR.data() };
    ret.process (nullptr, out, share.data(), n, a3::ReturnSelect::analog);
    EXPECT_EQ (oL[n - 1], 0.0f);
    EXPECT_EQ (oR[n - 1], 0.0f);
}

TEST (ReturnSelect, OutOfRangeKeepsTheLast)
{
    a3::ReturnSelect ret;
    ret.prepare (rate);
    const int n = 1000;
    std::vector<float> a (n, 0.25f), oL (n), oR (n), share (n);
    const float* analog[2] = { a.data(), a.data() };
    float* out[2] = { oL.data(), oR.data() };
    ret.process (analog, out, share.data(), n, a3::ReturnSelect::delay);
    ret.process (analog, out, share.data(), n, 2);
    ret.process (analog, out, share.data(), n, -1);
    EXPECT_EQ (oL[n - 1], 0.0f);
    EXPECT_EQ (share[n - 1], 1.0f);
}
