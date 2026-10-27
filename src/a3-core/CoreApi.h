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
//   GET /api/status    {"audio_engine", "reason"}: which engine runs (a3_core_web's /api/status)
//   GET /api/rig       who is there: Motion, the desks, the decks, StemDeck, LIVE, the engine, the devices
//   POST /api/send     the bench: {"to": self|mixer|motion, "address", "value"}
// None of these three calls into Core: status and rig are the copy the serve thread published
// (CoreView), and the bench hands what it sends to the callback it is given.
#pragma once

#include <functional>
#include <stdexcept>
#include <string>

#include <juce_core/juce_core.h>

#include "CoreView.h"
#include "Devices.h"
#include "HttpServer.h"
#include "Net.h"
#include "OscWire.h"
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

// a3_core_web.parse_value: what was typed into the bench, as the type it says it is -- "1" an int,
// 0.5 a float, "\"1\"" text, a bare word text. std::invalid_argument ("nothing typed") for blank input.
// A number too big for int64 is std::out_of_range: python-osc cannot encode it either.
OscArg parseValue (const std::string& text);

// Thrown by the bench's sender for a destination it will not send to; what() is the refusal said to
// the person typing.
struct SendRefused : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Sends one message where `to` says (self, mixer or motion); SendRefused for anything else.
using BenchSend = std::function<void (const std::string& to, const OscMessage&)>;

// Status, rig and the bench. `view` null: status and rig answer {}. `send` empty: a read-only window,
// the bench is refused.
void addCoreRoutes (Routes& routes, const CoreView* view, BenchSend send);

// a3_core_web.default_bind: where the window listens unless told otherwise, the truth's `core.web`
// listener as "host:port".
std::string defaultBind (const Truth& truth);
}
