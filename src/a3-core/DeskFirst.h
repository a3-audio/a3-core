// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_desk_first.py (B0 f3345dc7feeb): the desk beats Core's own evening replay. The
// desk (the A3 Mixer) sends every position it holds as soon as it hears Core, before or after
// Core's replay of the evening; both orders must end with the desk's values. So the replay
// carries the desk's value instead of the evening's, for every address the desk has sent in this
// run, on every replay of the run. Between the desk and Motion the latest touch wins.
// A lock, because the receive thread hears and the replay thread reads.
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace a3core
{
class DeskFirst
{
public:
    using Pair = std::pair<std::string, double>;   // an address and the number it holds
    using Pairs = std::vector<Pair>;

    // A value that passed through Core's door this run.
    void heard (const std::string& address, double value, bool fromDesk);

    // `evening` with the desk's values laid over it: replaced where the evening has the address,
    // added where it does not. Python's dict order: the evening's, then the desk's new ones.
    Pairs over (const Pairs& evening) const;

private:
    mutable std::mutex lock;
    Pairs values;                                       // in the order the addresses were first heard
    std::unordered_map<std::string, size_t> position;   // address -> index in `values`
};
}
