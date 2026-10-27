// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ChannelSelect.h"
#include <algorithm>
#include <cmath>

namespace a3
{
namespace
{
int fadeLength (double sampleRate) { return std::max (1, (int) std::lround (0.010 * sampleRate)); }
int towards (int value, int goal, int by) { return value < goal ? std::min (goal, value + by) : std::max (goal, value - by); }
}

void ChannelSelect::prepare (double sampleRate, int numDecks, bool analogAvailable)
{
    fadeSamples = fadeLength (sampleRate);
    decks = std::max (0, numDecks);
    hasAnalog = analogAvailable;
    target = hasAnalog ? 0 : (decks > 0 ? 1 : 0);
    slot = target;
    analogCount = hasAnalog ? fadeSamples : 0;
    deckCount = target > 0 ? fadeSamples : 0;
    analogGoal = analogCount;
    deckGoal = deckCount;
}

ChannelSelect::Step ChannelSelect::advance (int requested, int numSamples)
{
    const bool valid = requested >= 0 && requested <= decks && (requested > 0 || hasAnalog);
    if (valid)
        target = requested;

    Step s;
    if (target == 0)
    {
        analogGoal = fadeSamples;
        deckGoal = 0;
    }
    else if (slot == target || deckCount == 0)
    {
        slot = target;   // a dead path takes its new slot before it fades in
        analogGoal = 0;
        deckGoal = fadeSamples;
    }
    else
    {
        analogGoal = 0;   // deck -> deck: dip out first, swap at zero
        deckGoal = 0;
    }

    s.analogFrom = (float) analogCount / (float) fadeSamples;
    s.deckFrom = (float) deckCount / (float) fadeSamples;
    s.resetAnalog = analogCount == 0 && analogGoal > 0;
    s.resetDeck = deckCount == 0 && deckGoal > 0;
    s.deckSlot = slot;

    int length = numSamples;
    if (analogCount != analogGoal)
        length = std::min (length, std::abs (analogGoal - analogCount));
    if (deckCount != deckGoal)
        length = std::min (length, std::abs (deckGoal - deckCount));
    s.length = std::max (0, length);

    analogCount = towards (analogCount, analogGoal, s.length);
    deckCount = towards (deckCount, deckGoal, s.length);
    s.analogTo = (float) analogCount / (float) fadeSamples;
    s.deckTo = (float) deckCount / (float) fadeSamples;
    return s;
}

bool ChannelSelect::analogActive() const { return analogCount > 0 || analogGoal > 0; }
bool ChannelSelect::deckActive() const { return deckCount > 0 || deckGoal > 0; }

void ReturnSelect::prepare (double sampleRate)
{
    fadeSamples = fadeLength (sampleRate);
    selected = analog;
    shareCount = 0;
}

void ReturnSelect::process (const float* const* analogPair, float* const* outPair, float* delayShare, int numSamples,
                            int source)
{
    if (source == analog || source == delay)
        selected = source;
    const int goal = selected == delay ? fadeSamples : 0;

    for (int i = 0; i < numSamples; ++i)
    {
        shareCount = towards (shareCount, goal, 1);
        // The ends are exact: 0 and 1, so analog is bit-identical to before and delay shuts analog to 0.0f.
        const float share = shareCount == 0 ? 0.0f : shareCount == fadeSamples ? 1.0f : (float) shareCount / (float) fadeSamples;
        const float keep = 1.0f - share;
        delayShare[i] = share;
        for (int side = 0; side < 2; ++side)
        {
            const float* in = analogPair != nullptr ? analogPair[side] : nullptr;
            outPair[side][i] = in != nullptr ? keep * in[i] : 0.0f;
        }
    }
}
}
