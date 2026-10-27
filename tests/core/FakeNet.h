// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The world a scenario's Core runs in: a clock that stands where the scenario says, and an outbox that
// writes down every datagram instead of sending it -- in the transcript form of tests/core/scenarios.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

#include "Net.h"
#include "OscWire.h"

namespace fakenet
{
// The scenario clock: scenario_core.py's MONOTONIC_BASE and WALL_BASE plus the step's "at".
class FakeClock final : public a3core::Clock
{
public:
    static constexpr double monotonicBase = 1000.0;
    static constexpr double wallBase = 1800000000.0;

    double at = 0.0;

    double monotonic() override { return monotonicBase + at; }
    double wall() override { return wallBase + at; }
};

// A datagram's arguments as [tag, value] pairs, read off the encoded bytes: what the wire carries,
// a float as the float32 it became.
inline juce::var argsOf (const a3core::OscMessage& sent)
{
    const auto bytes = a3core::encodeMessage (sent);
    const auto decoded = a3core::decodeMessage (bytes.data(), bytes.size());
    juce::Array<juce::var> args;
    if (! decoded)
        return juce::var (args);
    for (const auto& arg : decoded->args)
    {
        juce::var value;
        switch (arg.tag)
        {
            case 'i': value = std::get<int32_t> (arg.value); break;
            case 'h': value = static_cast<juce::int64> (std::get<int64_t> (arg.value)); break;
            case 'f': value = static_cast<double> (std::get<float> (arg.value)); break;
            case 'd': value = std::get<double> (arg.value); break;
            case 's': value = juce::String::fromUTF8 (std::get<std::string> (arg.value).c_str()); break;
            case 'T': value = true; break;
            case 'F': value = false; break;
            case 'b':
            {
                const auto& blob = std::get<std::vector<uint8_t>> (arg.value);
                value = juce::Base64::toBase64 (blob.data(), blob.size());
                break;
            }
            default: break;
        }
        args.add (juce::var (juce::Array<juce::var> { juce::String::charToString (static_cast<juce::juce_wchar> (arg.tag)), value }));
    }
    return juce::var (args);
}

class FakeNet final : public a3core::Outbox
{
public:
    explicit FakeNet (const FakeClock& clockToUse) : clock (clockToUse) {}

    // Every datagram to Core's own port, for the runner to feed back after the step.
    std::function<void (const std::vector<uint8_t>&)> toSelf;
    // Told after each send is written down: a test that needs something to arrive in the middle of
    // a send (another thread's view of the serve loop) hooks it here.
    std::function<void (const a3core::Client&, const a3core::OscMessage&)> sent;

    void send (const a3core::Client& client, const a3core::OscMessage& message) override
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("at", clock.at);
        entry->setProperty ("to", juce::String (client.name));
        entry->setProperty ("host", juce::String (client.to.host));
        entry->setProperty ("port", client.to.port);
        entry->setProperty ("address", juce::String::fromUTF8 (message.address.c_str()));
        entry->setProperty ("args", argsOf (message));
        transcript.add (juce::var (entry));
        if (client.name == "self" && toSelf)
            toSelf (a3core::encodeMessage (message));
        if (sent)
            sent (client, message);
    }

    void sendRaw (const a3core::Client& client, const std::vector<uint8_t>& data) override
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("at", clock.at);
        entry->setProperty ("to", juce::String (client.name));
        entry->setProperty ("host", juce::String (client.to.host));
        entry->setProperty ("port", client.to.port);
        entry->setProperty ("raw", juce::Base64::toBase64 (data.data(), data.size()));
        transcript.add (juce::var (entry));
    }

    void journal (const std::string& line, bool toStderr)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("at", clock.at);
        entry->setProperty ("journal", juce::String::fromUTF8 (line.c_str()));
        entry->setProperty ("stderr", toStderr);
        transcript.add (juce::var (entry));
    }

    void file (const std::string& name, const juce::String& text, const juce::var& json)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("at", clock.at);
        entry->setProperty ("file", juce::String (name));
        entry->setProperty ("text", text);
        entry->setProperty ("json", json);
        transcript.add (juce::var (entry));
    }

    juce::Array<juce::var> transcript;

private:
    const FakeClock& clock;
};
}
