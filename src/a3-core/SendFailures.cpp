// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SendFailures.h"

namespace a3core
{
bool SendFailures::send (const std::string& destination, const std::function<void()>& attempt)
{
    try
    {
        attempt();
    }
    catch (const OsError& problem)
    {
        if (failing.insert (destination).second)
            say (destination + ": send failed (" + (problem.strerror.empty() ? problem.text : problem.strerror)
                     + "); said once until it gets through again",
                 false);
        return false;
    }

    if (failing.erase (destination) > 0)
        say (destination + ": sends get through again", false);
    return true;
}
}
