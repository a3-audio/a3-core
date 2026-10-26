// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_presence.py (B0 f3345dc7feeb): whether a device that says hello is still there.
// Pure: the caller passes the clock (Clock::monotonic seconds).
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace a3core
{
inline constexpr double stemdeckSilence = 60.0;
inline constexpr double motionSilence = 60.0;

class Presence
{
public:
    explicit Presence (double silenceAfter) : silenceAfter (silenceAfter) {}

    bool present (double now) const;
    // Note a hello. True if it is news: the first, or after silence.
    bool heard (double now);
    // True exactly once, when the silence has begun.
    bool gone (double now);

private:
    double silenceAfter;
    std::optional<double> last;
    bool countedGone = false;
};

// What a device's hello means for Core's link to it (StemDeck's and Motion's alike). Each host is
// watched on its own; with oneAtATime the host followed keeps the link while it is alive and a
// newcomer is kept as a rival for takeRivals().
class HelloWatch
{
public:
    HelloWatch (double silence, std::function<bool (const std::string&)> isLocal = {}, bool oneAtATime = false);

    // True when Core should (re)connect to `host` and send a recall.
    bool hello (const std::string& host, double now);
    // True exactly once when the host followed has gone quiet.
    bool silence (double now);
    // The hosts that turned up beside the one followed since the last call, each once.
    std::vector<std::string> takeRivals();

    const std::optional<std::string>& host() const { return followed; }

private:
    bool isLocalHost (const std::string& host) const { return isLocal && isLocal (host); }
    void noteRival (const std::string& host);
    void forgetTheSilent (double now);

    double silenceAfter;
    std::function<bool (const std::string&)> isLocal;
    bool oneAtATime;
    std::map<std::string, Presence> heardFrom;
    std::set<std::string> rivals;
    std::vector<std::string> unreported;
    std::optional<std::string> followed;
};
}
