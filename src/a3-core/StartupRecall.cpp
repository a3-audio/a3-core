// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StartupRecall.h"

#include <random>

namespace a3core
{
long long randomToken()
{
    static std::mt19937_64 generator { std::random_device {}() };
    return 2 + static_cast<long long> (generator() % (1ULL << 30));
}

long long StartupRecall::rearm (std::optional<long long> token)
{
    armed = token ? *token : randomToken();
    taken = false;
    return armed;
}

bool StartupRecall::isIt (const std::vector<OscArg>& arguments)
{
    if (taken || arguments.empty())
        return false;
    const auto first = pythonInt (arguments.front());
    if (! first || *first != armed)
        return false;
    taken = true;
    return true;
}
}
