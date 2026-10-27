// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --render-truth user|network-file|network` (B1's a3-osc-render) and
// `a3-core --truth host NAME | port PROGRAM ROLE` (the viewer recipe's question): what they say and
// their exit code, returned rather than printed so a test sees exactly what a script would.
#pragma once

#include <string>

#include "Platform.h"
#include "ProgramArgs.h"

namespace a3app
{
struct SubcommandResult
{
    int exitCode = 0;
    std::string out, err;
};

SubcommandResult runSubcommand (const ProgramArgs&, const a3core::Platform&);
}
