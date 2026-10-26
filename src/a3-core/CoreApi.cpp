// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoreApi.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "Json.h"
#include "Register.h"

namespace a3core
{
namespace
{
HttpResponse jsonResponse (const juce::var& value)
{
    HttpResponse response;
    response.contentType = "application/json";
    response.body = canonicalBytes (value);
    return response;
}

HttpResponse noPageHere()
{
    HttpResponse response;
    response.status = 404;
    response.body = "there is no page here: the data is under /api/ (truth, traffic, unknown, register, devices, stream); "
                    "the a3-core window draws it";
    return response;
}

// One reader of the stream: remembers its last look, so the history is only what happened since, and
// the rate is made from the two looks.
Routes::Events streamOf (const MonitorSources& sources)
{
    auto previous = std::make_shared<std::optional<TrafficSnapshot>> ();
    return [sources, previous] {
        // Only the history is asked for incrementally: the rows are the addresses Core understood, about
        // forty, and the page wants them whole every tick.
        const auto since = previous->has_value() ? std::optional<double> ((*previous)->at) : std::nullopt;
        auto snapshot = sources.traffic.snapshot (since, sources.clock.monotonic());
        const auto payload = canonicalBytes (snapshotAsJson (snapshot, previous->has_value() ? &**previous : nullptr));
        snapshot.history.clear();   // the next look only needs the rows and the instant
        *previous = std::move (snapshot);
        return payload;
    };
}
}

void addMonitorRoutes (Routes& routes, MonitorSources sources)
{
    routes.get ("/", [] (const HttpRequest&) { return noPageHere(); });

    routes.get ("/api/traffic", [sources] (const HttpRequest&) {
        return jsonResponse (snapshotAsJson (sources.traffic.snapshot (std::nullopt, sources.clock.monotonic()), nullptr));
    });

    // Not on the stream: this can be tens of thousands of rows. Fetched on request, and only on request.
    routes.get ("/api/unknown", [sources] (const HttpRequest&) {
        return jsonResponse (unknownAsJson (sources.traffic.unknownSnapshot (sources.clock.monotonic())));
    });

    // The catalogue, not the log. A register that cannot be read answers 200 with `problem` set rather
    // than 500: "no register is installed" is not "the window is broken".
    routes.get ("/api/register", [sources] (const HttpRequest&) {
        return jsonResponse (reg::registerAsJson (sources.registerFile, sources.traffic, sources.clock.monotonic()));
    });

    routes.get ("/api/devices", [sources] (const HttpRequest&) {
        auto* made = new juce::DynamicObject();
        juce::var payload (made);
        if (sources.devices != nullptr)
        {
            made->setProperty ("own", juce::String (juce::CharPointer_UTF8 (sources.devices->ownHash.c_str())));
            made->setProperty ("rows", sources.devices->snapshot (sources.clock.monotonic()));
        }
        else
        {
            made->setProperty ("own", juce::var());
            made->setProperty ("rows", juce::var (juce::Array<juce::var>()));
        }
        return jsonResponse (payload);
    });

    // The joined truth every device fetches, its fingerprint in a header. Exactly the canonical bytes:
    // a reader hashes what it reads and compares it with the header.
    if (sources.truth != nullptr)
        routes.get ("/api/truth", [truth = sources.truth] (const HttpRequest&) {
            HttpResponse response;
            response.contentType = "application/json";
            response.body = truth->canonical();
            response.headers.emplace_back ("X-A3-Truth", truth->fingerprint());
            return response;
        });

    routes.stream ("/api/stream", [sources] { return streamOf (sources); }, 1000 / std::max (1, sources.streamHz));
}

std::string defaultBind (const Truth& truth)
{
    const auto listener = truth.listener ("core", "web");
    return truth.host (listener["host"].toString().toStdString()) + ":" + std::to_string (static_cast<int> (listener["port"]));
}
}
