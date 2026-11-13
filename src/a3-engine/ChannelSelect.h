// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace a3
{
// Decides which signal path of a channel runs and with which gain ramp; does no
// audio itself (A7 runs the paths). Source: 0 = analog, k = deck slot k (1..decks).
// A request outside 0..decks (or analog on a channel without an analog pair)
// keeps the current one.
//   analog <-> deck: 10 ms crossfade, both paths run.
//   deck k -> deck j: the deck path fades out over 10 ms, swaps slot (resetDeck),
//   fades in over 10 ms.
// A deck in stereo mode runs through the analog path instead, with the deck's pair
// as that path's input (analogInput k); its deck path stays off:
//   analog <-> stereo deck k, stereo deck k -> j: the analog path fades out over
//   10 ms, swaps input (resetAnalog), fades in over 10 ms.
//   stereo deck k <-> B-format deck: 10 ms crossfade, both paths run.
// All ramps are linear in absolute time with a fixed slope, so the gain at a
// given sample is independent of how the stream is cut into calls.
class ChannelSelect
{
public:
    void prepare (double sampleRate, int decks, bool hasAnalog);

    // One linear segment: gain at the j-th sample (0-based) of `length` is
    // from + (to - from) * (j + 1) / length. A path at 0 at both ends is not
    // processed; a path that starts a segment at 0 and rises has its reset flag set.
    struct Step
    {
        float analogFrom = 0, analogTo = 0, deckFrom = 0, deckTo = 0;
        int deckSlot = 0;
        int analogInput = 0;   // what feeds the analog path: 0 = the channel's own pair, k = deck slot k's stereo pair
        bool resetDeck = false, resetAnalog = false;
        int length = 0;   // samples covered (<= numSamples asked for; a segment stops where a ramp ends)
    };

    // Covers up to numSamples; the caller loops until the block is used up:
    //   while (n > 0) { auto s = sel.advance (src, n); ...run s.length samples...; n -= s.length; }
    // requestedDeckIsStereo: the requested deck slot sends one stereo pair (ignored for analog, for a
    // request that is refused, and on a channel without an analog path, whose deck path runs instead).
    Step advance (int requestedSource, int numSamples, bool requestedDeckIsStereo);
    Step advance (int requestedSource, int numSamples) { return advance (requestedSource, numSamples, false); }

    bool analogActive() const;   // analog gain > 0 or rising
    bool deckActive() const;
    int deckSlot() const { return slot; }
    int analogInput() const { return input; }

private:
    int fadeSamples = 441, decks = 0;
    bool hasAnalog = true;
    int target = 0;           // wanted source
    bool targetStereo = false;   // the wanted deck runs through the analog path
    int input = 0;            // what feeds the analog path (0 = own pair, k = deck slot k's stereo pair)
    int slot = 0;             // deck slot loaded in the deck path (0 = none)
    int analogCount = 0, deckCount = 0;   // gain = count / fadeSamples
    int analogGoal = 0, deckGoal = 0;
};
}
