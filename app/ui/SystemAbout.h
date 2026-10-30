// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What the SYSTEM page says about the project (the Canvas's greeter, a3-system canvas/Source/model/Greeter and Support,
// moved here on 2026-10-30: "all content from the Canvas goes to the Engine"). Pure: no component, no file but loadSupport's.
#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace a3app::about
{
juce::String tagline();
juce::String copyrightLine();
juce::String contactAddress();
juce::String licenceLine();

struct Link
{
    juce::String label, url;
};
// Documentation, Website, Source.
std::vector<Link> links();

// Where "Support A³" leads, as the Canvas reads it: ~/.config/a3/canvas.json, else the package's default. An entry
// that is not what its key says is dropped, so nobody is sent to a mistyped target.
struct Support
{
    juce::String kofi;      // https://ko-fi.com/<name>
    juce::String paypal;    // https://paypal.me/<name> or https://www.paypal.com/paypalme/<name>
    juce::String bitcoin;   // an address (1…, 3… or bc1…)

    bool isEmpty() const { return kofi.isEmpty() && paypal.isEmpty() && bitcoin.isEmpty(); }
};

Support parseSupport (const juce::String& jsonText);
// The user's file when it exists (whole, not merged), else the shipped one, else nothing.
Support loadSupport (const juce::File& user, const juce::File& shipped);
// ~/.config/a3/canvas.json, else /usr/share/a3/canvas.json (the a3-canvas package's).
Support machineSupport (const juce::File& configDir);
}
