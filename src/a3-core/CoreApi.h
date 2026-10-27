// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The monitor half of a3_core_web.py: what Core is passing about, as JSON. No HTML page any more
// (the a3-core window draws it); only the endpoints people and one machine reader use.
//
//   GET /api/truth      the joined truth, canonical bytes, its fingerprint in `X-A3-Truth`.
//                       StemDeck, the desk, Motion and the pro-DJ app fetch exactly this.
//   GET /api/traffic    a snapshot of what Core understood, with a rate per row (null: no earlier look)
//   GET /api/unknown    the table of what Core could not route (fetched on request, never streamed)
//   GET /api/register   the catalogue of addresses held against the traffic of both tables
//   GET /api/devices    {"own", "rows"}: which truth each device speaks
//   GET /api/stream     server-sent events, `data: <json>` at streamHz, history incremental
//   GET /               404 text that says where the JSON is (P7)
// Status, send and the rig come in Task 21.
#pragma once

#include <string>

#include <juce_core/juce_core.h>

#include "Devices.h"
#include "HttpServer.h"
#include "Net.h"
#include "Traffic.h"
#include "Truth.h"

namespace a3core
{
// Everything the handlers read, and the one clock they all read it by (Traffic is given `now`, never
// the wall clock). `truth` and `devices` may be null, as the Python window's could be: no /api/truth
// route at all (404), and /api/devices answers {"own": null, "rows": []}.
struct MonitorSources
{
    const Truth* truth;
    Traffic& traffic;
    Devices* devices;
    juce::File registerFile;
    Clock& clock;
    int streamHz = 4;
};

void addMonitorRoutes (Routes& routes, MonitorSources sources);

// a3_core_web.default_bind: where the window listens unless told otherwise, the truth's `core.web`
// listener as "host:port".
std::string defaultBind (const Truth& truth);
}
