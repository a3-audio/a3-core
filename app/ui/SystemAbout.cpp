// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemAbout.h"

namespace a3app::about
{
namespace
{
const juce::String base58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const juce::String bech32 = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

// https://<one of hosts>/<a path without spaces>, nothing else.
bool isLinkTo (const juce::String& text, std::initializer_list<const char*> hosts)
{
    if (! text.startsWith ("https://") || text.containsAnyOf (" \t\r\n\"'<>"))
        return false;
    const auto rest = text.fromFirstOccurrenceOf ("https://", false, false);
    const auto host = rest.upToFirstOccurrenceOf ("/", false, false).toLowerCase();
    if (! rest.containsChar ('/') || rest.fromFirstOccurrenceOf ("/", false, false).isEmpty())
        return false;
    for (const auto* allowed : hosts)
        if (host == allowed)
            return true;
    return false;
}

// paypal.me/<name>, or the same as PayPal writes it on its own site: www.paypal.com/paypalme/<name>.
bool isPayPalMe (const juce::String& text)
{
    if (isLinkTo (text, { "paypal.me", "www.paypal.me" }))
        return true;
    const juce::String personal = "https://www.paypal.com/paypalme/";
    return isLinkTo (text, { "www.paypal.com" }) && text.startsWith (personal) && text.length() > personal.length();
}

// The address's form only (prefix, alphabet, length); the checksum is the wallet's to check.
bool isBitcoinAddress (const juce::String& text)
{
    if (text.startsWith ("bc1"))
        return text.length() >= 14 && text.length() <= 90 && text.substring (3).containsOnly (bech32);
    return (text.startsWithChar ('1') || text.startsWithChar ('3')) && text.length() >= 26 && text.length() <= 35
           && text.containsOnly (base58);
}

juce::String entry (const juce::var& json, const char* key)
{
    const auto value = json.getProperty (key, {});
    return value.isString() ? value.toString().trim() : juce::String();
}
}

juce::String tagline() { return "Spatial DJ setup"; }

// The legal notice names the maintainer; the years are the project's (2020, as .reuse/dep5 has it).
juce::String copyrightLine() { return juce::String::fromUTF8 ("© 2020–2026 Raphael Eismann and contributors"); }

// The website footer's contact.
juce::String contactAddress() { return "p5hcu1@systemli.org"; }

// As LICENSES/ in a3-core, a3-mixer, a3-motion and a3-doc have it.
juce::String licenceLine()
{
    return juce::String::fromUTF8 ("Software GPL-3.0-or-later · Hardware CERN-OHL-S-2.0 · Documentation CC-BY-SA-4.0");
}

std::vector<Link> links()
{
    return { { "Documentation", "https://a3-audio.github.io/a3-doc/" },
             { "Website", "https://a3-audio.github.io/" },
             { "Source", "https://github.com/a3-audio/a3-system" } };
}

Support parseSupport (const juce::String& jsonText)
{
    const auto json = juce::JSON::parse (jsonText);
    if (! json.isObject())
        return {};
    Support support;
    if (const auto kofi = entry (json, "kofi"); isLinkTo (kofi, { "ko-fi.com", "www.ko-fi.com" }))
        support.kofi = kofi;
    if (const auto paypal = entry (json, "paypal"); isPayPalMe (paypal))
        support.paypal = paypal;
    if (const auto bitcoin = entry (json, "bitcoin"); isBitcoinAddress (bitcoin))
        support.bitcoin = bitcoin;
    return support;
}

Support loadSupport (const juce::File& user, const juce::File& shipped)
{
    if (user.existsAsFile())
        return parseSupport (user.loadFileAsString());
    if (shipped.existsAsFile())
        return parseSupport (shipped.loadFileAsString());
    return {};
}

Support machineSupport (const juce::File& configDir)
{
    return loadSupport (configDir.getChildFile ("canvas.json"), juce::File ("/usr/share/a3/canvas.json"));
}
}
