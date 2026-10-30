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
//   GET /api/departments   {"departments": [...]}: the service file's and the stored ones, with what each was sent
//   POST /api/departments  {"departments": [{"name", "host", "port", "on"}, ...]}: the whole stored list, checked,
//                          then stored and applied live; 400 {"problem"} and nothing stored when it is refused
// None of these three calls into Core: status and rig are the copy the serve thread published
// (CoreView), and the bench hands what it sends to the callback it is given.
#pragma once

#include <functional>
#include <stdexcept>
#include <string>

#include <juce_core/juce_core.h>

#include "CoreView.h"
#include "Departments.h"
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

// Thrown by a departments save that could not be done (the file not written); what() is said to the window.
struct DepartmentsRefused : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Where a checked list goes: `resolves` decides whether a host name resolves; `save` stores the list and
// has Core apply it (DepartmentsRefused when it cannot). No `save`: this Core keeps no list, POST is refused.
struct DepartmentsDoor
{
    HostResolves resolves;
    std::function<void (const std::vector<Department>&)> save;
};

// GET /api/departments from the view (null: an empty list), POST /api/departments through the door.
void addDepartmentRoutes (Routes& routes, const CoreView* view, DepartmentsDoor door);

// a3_core_web.default_bind: where the window listens unless told otherwise, the truth's `core.web`
// listener as "host:port".
std::string defaultBind (const Truth& truth);
}
