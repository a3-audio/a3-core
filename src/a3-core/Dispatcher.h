// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// python-osc 1.10.2's Dispatcher: which mapped handlers a message's address reaches.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Net.h"

namespace a3core
{
using Handler = std::function<void (const Endpoint& from, const OscMessage&)>;

class Dispatcher
{
public:
    Dispatcher();
    ~Dispatcher();

    // Throws std::invalid_argument for a pattern with '*' that python-osc could not compile as a
    // regular expression, or that uses regex syntax beyond what Core's patterns need.
    void map (std::string pattern, Handler handler);
    void setDefault (Handler handler);

    // Every match, in the order the patterns were first mapped; the default only when none
    // matched. An address python-osc cannot turn into a regex reaches nothing, not even the default.
    void dispatch (const Endpoint& from, const OscMessage& message) const;

private:
    struct Entry;
    std::vector<std::unique_ptr<Entry>> entries;
    Handler fallback;
};
}
