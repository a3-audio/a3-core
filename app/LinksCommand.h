// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// `a3-core --links`: reads the JACK/PipeWire graph and the engine's I/O map and prints formatLinks' text.
// The thin layer; the wording is LinksOverview's and is tested there.
#pragma once
#include "Platform.h"

namespace a3app
{
// 0 after printing; 1 when there is no graph to read (one line on stderr says so).
int runLinks (const a3core::Platform&);
}
