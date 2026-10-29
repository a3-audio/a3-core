// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DelayValues.h"

#include <algorithm>
#include <cmath>

#include "a3-delay/DelayParameters.h"

namespace a3core
{
namespace
{
constexpr double feedbackPerUnit = 20.0, colorPerUnit = 10.0, spreadStep = 15.0;   // 0.05 and 0.1 as divisions: 14 / 20.0 is 0.7, 14 * 0.05 is not
constexpr int feedbackLast = 19, colorEnd = 10, spreadLast = 12;   // 0.95, +-1.0, 180 degrees
constexpr long long mostClicks = 1000;   // a turn of any real size; keeps the arithmetic far from overflow

// The defaults are a3::delay's; the indexes above are those values, written down once.
static_assert (a3::delay::legalBeats[1] == a3::delay::defaultBeat);
static_assert (a3::delay::defaultFeedback == static_cast<float> (14 / feedbackPerUnit));
static_assert (a3::delay::defaultColor == 0.0f);
static_assert (a3::delay::defaultSpread == static_cast<float> (4 * spreadStep));

int lastStep (DelayWord word)
{
    switch (word)
    {
        case DelayWord::beat: return static_cast<int> (a3::delay::legalBeats.size()) - 1;
        case DelayWord::feedback: return feedbackLast;
        case DelayWord::color: return colorEnd;
        case DelayWord::spread: return spreadLast;
    }
    return 0;
}

int firstStep (DelayWord word) { return word == DelayWord::color ? -colorEnd : 0; }
}

const char* delayTurnKey (DelayWord word)
{
    switch (word)
    {
        case DelayWord::beat: return "aux-return.delay.beat.turn";
        case DelayWord::feedback: return "aux-return.delay.feedback.turn";
        case DelayWord::color: return "aux-return.delay.color.turn";
        case DelayWord::spread: return "aux-return.delay.spread.turn";
    }
    return "";
}

const char* delayValueKey (DelayWord word)
{
    switch (word)
    {
        case DelayWord::beat: return "aux-return.delay.beat";
        case DelayWord::feedback: return "aux-return.delay.feedback";
        case DelayWord::color: return "aux-return.delay.color";
        case DelayWord::spread: return "aux-return.delay.spread";
    }
    return "";
}

const char* delayStateKey (DelayWord word)
{
    switch (word)
    {
        case DelayWord::beat: return "delay_beat";
        case DelayWord::feedback: return "delay_feedback";
        case DelayWord::color: return "delay_color";
        case DelayWord::spread: return "delay_spread";
    }
    return "";
}

std::optional<DelayWord> delayWordOfTurnKey (const std::string& key)
{
    for (const auto word : delayWords)
        if (key == delayTurnKey (word))
            return word;
    return std::nullopt;
}

int& DelaySteps::at (DelayWord word)
{
    switch (word)
    {
        case DelayWord::beat: return beat;
        case DelayWord::feedback: return feedback;
        case DelayWord::color: return color;
        case DelayWord::spread: return spread;
    }
    return beat;
}

int DelaySteps::at (DelayWord word) const { return const_cast<DelaySteps&> (*this).at (word); }

void turnDelay (DelaySteps& steps, DelayWord word, long long clicks)
{
    auto& step = steps.at (word);
    const auto wanted = step + std::clamp (clicks, -mostClicks, mostClicks);
    auto next = static_cast<int> (std::clamp<long long> (wanted, firstStep (word), lastStep (word)));
    if (word == DelayWord::color && step * next < 0)
        next = 0;
    step = next;
}

double delayValue (const DelaySteps& steps, DelayWord word)
{
    switch (word)
    {
        case DelayWord::beat: return a3::delay::legalBeats[static_cast<size_t> (steps.beat)];
        case DelayWord::feedback: return steps.feedback / feedbackPerUnit;
        case DelayWord::color: return steps.color / colorPerUnit;
        case DelayWord::spread: return steps.spread * spreadStep;
    }
    return 0.0;
}

void setDelayFromValue (DelaySteps& steps, DelayWord word, double value)
{
    if (! std::isfinite (value))
        return;
    const auto nearest = [&] (double unit) { return static_cast<int> (std::lround (std::clamp (value / unit, -1000.0, 1000.0))); };
    auto& step = steps.at (word);
    switch (word)
    {
        case DelayWord::beat:
        {
            const auto snapped = a3::delay::snapBeat (static_cast<float> (value));
            step = static_cast<int> (std::find (a3::delay::legalBeats.begin(), a3::delay::legalBeats.end(), snapped)
                                     - a3::delay::legalBeats.begin());
            return;
        }
        case DelayWord::feedback: step = std::clamp (nearest (1.0 / feedbackPerUnit), 0, feedbackLast); return;
        case DelayWord::color: step = std::clamp (nearest (1.0 / colorPerUnit), -colorEnd, colorEnd); return;
        case DelayWord::spread: step = std::clamp (nearest (spreadStep), 0, spreadLast); return;
    }
}

double announcedBeat (const DelaySteps& steps, std::optional<double> tempo)
{
    const auto selected = static_cast<float> (delayValue (steps, DelayWord::beat));
    return tempo ? a3::delay::fitBeatsToLine (static_cast<float> (*tempo), selected) : selected;
}
}
