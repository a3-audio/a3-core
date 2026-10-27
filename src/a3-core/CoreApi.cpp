// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoreApi.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <variant>

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

namespace
{
bool isSpace (char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string stripped (const std::string& text)
{
    size_t first = 0, last = text.size();
    while (first < last && isSpace (text[first]))
        ++first;
    while (last > first && isSpace (text[last - 1]))
        --last;
    return text.substr (first, last - first);
}

// Python's number grammar lets a single underscore stand between two digits (1_000); anywhere else it
// makes the text no number. The digits only, or nullopt.
std::optional<std::string> withoutDigitSeparators (const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '_')
        {
            out += text[i];
            continue;
        }
        const bool between = i > 0 && i + 1 < text.size() && std::isdigit (static_cast<unsigned char> (text[i - 1]))
                             && std::isdigit (static_cast<unsigned char> (text[i + 1]));
        if (! between)
            return std::nullopt;
    }
    return out;
}

// int(text), or nullopt when Python's int() would raise ValueError. std::out_of_range when it fits no int64.
std::optional<long long> pythonInteger (const std::string& said)
{
    static const std::regex integer (R"(^[+-]?[0-9]+$)");
    const auto digits = withoutDigitSeparators (said);
    if (! digits || ! std::regex_match (*digits, integer))
        return std::nullopt;
    errno = 0;
    const auto number = std::strtoll (digits->c_str(), nullptr, 10);
    if (errno == ERANGE)
        throw std::out_of_range ("an integer beyond 64 bits cannot be sent as OSC");
    return number;
}

// float(text), or nullopt when Python's float() would raise ValueError.
std::optional<double> pythonFloatOf (const std::string& said)
{
    static const std::regex number (R"(^[+-]?(([0-9]+\.?[0-9]*|\.[0-9]+)([eE][+-]?[0-9]+)?|inf|infinity|nan)$)", std::regex::icase);
    const auto digits = withoutDigitSeparators (said);
    if (! digits || ! std::regex_match (*digits, number))
        return std::nullopt;
    return std::strtod (digits->c_str(), nullptr);
}

HttpResponse refused (const std::string& why)
{
    auto response = jsonResponse (objectOf ({ { "problem", juce::String::fromUTF8 (why.c_str()) } }));
    response.status = 400;
    return response;
}

// a3_core_web._jsonable of what was sent, for the answer: a number that JSON cannot hold is its repr.
juce::var answerValue (const OscArg& arg)
{
    if (arg.tag == 's')
        return juce::var (juce::String::fromUTF8 (std::get<std::string> (arg.value).c_str()));
    if (arg.tag == 'i' || arg.tag == 'h')
        return juce::var (static_cast<juce::int64> (*pythonInt (arg)));
    const auto number = *pythonFloat (arg);
    return std::isfinite (number) ? juce::var (number) : juce::var (juce::String (pythonRepr (number)));
}

struct BenchRequest
{
    std::string to, address;
    OscArg value;
};

// The body of a POST /api/send, or why it is refused.
std::variant<BenchRequest, std::string> readBenchRequest (const std::string& body)
{
    juce::String error;
    const auto asked = parseJson (juce::String::fromUTF8 (body.data(), static_cast<int> (body.size())), error);
    if (error.isNotEmpty())
        return "the body is not JSON: " + error.toStdString();
    if (! asked.isObject())
        return std::string ("the body must be a JSON object with to, address and value");

    std::map<std::string, std::string> fields;
    for (const auto* name : { "address", "to", "value" })
    {
        if (! asked.hasProperty (juce::Identifier (name)))
            return std::string ("the body has no ") + name;
        fields[name] = pythonStrOf (asked[juce::Identifier (name)]);   // B1: str(asked[name])
    }
    try
    {
        return BenchRequest { fields["to"], stripped (fields["address"]), parseValue (fields["value"]) };
    }
    catch (const std::exception& problem)
    {
        return std::string (problem.what());
    }
}

HttpResponse benchSends (const BenchSend& send, const HttpRequest& request)
{
    if (! send)
        return refused ("this window can only watch");
    const auto read = readBenchRequest (request.body);
    if (const auto* why = std::get_if<std::string> (&read))
        return refused (*why);
    const auto& asked = std::get<BenchRequest> (read);

    if (asked.address.empty() || asked.address.front() != '/')
        return refused ("an address starts with /");
    if (asked.to == "reaper")
        return refused ("reaper is not reachable from the bench: the audio engine plays, send to self, mixer or motion");
    if (asked.to != "self" && asked.to != "mixer" && asked.to != "motion")
        return refused ("unknown destination: " + asked.to);
    try
    {
        send (asked.to, OscMessage { asked.address, { asked.value } });
    }
    catch (const std::exception& problem)
    {
        return refused (problem.what());
    }
    return jsonResponse (objectOf ({ { "address", juce::String::fromUTF8 (asked.address.c_str()) }, { "value", answerValue (asked.value) } }));
}

HttpResponse textResponse (const std::string& json)
{
    HttpResponse response;
    response.contentType = "application/json";
    response.body = json;
    return response;
}
}

OscArg parseValue (const std::string& text)
{
    const auto said = stripped (text);
    if (said.empty())
        throw std::invalid_argument ("nothing typed");
    if (said.size() >= 2 && said.front() == '"' && said.back() == '"')
        return pyStr (said.substr (1, said.size() - 2));
    if (const auto integer = pythonInteger (said))
        return pyInt (*integer);
    if (const auto number = pythonFloatOf (said))
        return pyFloat (*number);
    return pyStr (said);   // a bare word is text, and so is a mistyped number
}

void addCoreRoutes (Routes& routes, const CoreView* view, BenchSend send)
{
    routes.get ("/api/status", [view] (const HttpRequest&) { return textResponse (view != nullptr ? view->texts().status : "{}"); });
    routes.get ("/api/rig", [view] (const HttpRequest&) { return textResponse (view != nullptr ? view->texts().rig : "{}"); });
    routes.post ("/api/send", [send = std::move (send)] (const HttpRequest& request) { return benchSends (send, request); });
}

std::string defaultBind (const Truth& truth)
{
    const auto listener = truth.listener ("core", "web");
    return truth.host (listener["host"].toString().toStdString()) + ":" + std::to_string (static_cast<int> (listener["port"]));
}
}
