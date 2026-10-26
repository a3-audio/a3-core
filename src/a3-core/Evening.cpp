// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Evening.h"

#include <cctype>

#include "Subscribers.h"

namespace a3core
{
namespace
{
struct Renaming
{
    std::string prefix, suffix, key;   // "/channel/" + digits + "/fx-send"; the key is a truth key
    bool hasChannel;
};

// ^/channel/(?P<ch>\d+)/fx-send$ and ^/master/fx-return$. The new name is a truth key, its address
// built from the truth: a literal here would be a second truth.
const std::vector<Renaming> renamings { { "/channel/", "/fx-send", "channel.aux-send", true },
                                        { "/master/fx-return", "", "master.aux-return", false } };

// Python's `$` also matches before a final newline.
std::string withoutFinalNewline (const std::string& text)
{
    return ! text.empty() && text.back() == '\n' ? text.substr (0, text.size() - 1) : text;
}

std::optional<std::string> renamed (const Truth& truth, const Renaming& rule, const std::string& address)
{
    const auto text = withoutFinalNewline (address);
    if (! rule.hasChannel)
        return text == rule.prefix ? std::optional<std::string> (truth.address (rule.key)) : std::nullopt;

    const auto& head = rule.prefix;
    const auto& tail = rule.suffix;
    if (text.size() <= head.size() + tail.size() || text.compare (0, head.size(), head) != 0
        || text.compare (text.size() - tail.size(), tail.size(), tail) != 0)
        return std::nullopt;
    const auto digits = text.substr (head.size(), text.size() - head.size() - tail.size());
    if (digits.find_first_not_of ("0123456789") != std::string::npos)
        return std::nullopt;
    // int() has no limit; a channel number beyond an int is beyond the truth's range, which drops it.
    const auto significant = digits.find_first_not_of ('0');
    if (significant != std::string::npos && digits.size() - significant > 9)
        throw TruthError { "channel out of range" };
    return truth.address (rule.key, { { "ch", significant == std::string::npos ? 0 : std::stoi (digits) } });
}

// A value as the Python Core sends it on from the file; arrays and objects are not one scalar.
std::optional<OscArg> argOf (const juce::var& value)
{
    if (value.isBool())
        return pyBool (static_cast<bool> (value));
    if (value.isInt() || value.isInt64())
        return pyInt (static_cast<juce::int64> (value));
    if (value.isDouble())
        return pyFloat (static_cast<double> (value));
    if (value.isString())
        return pyStr (value.toString().toStdString());
    if (value.isVoid())
        return OscArg { 'N', std::monostate {} };
    return std::nullopt;
}

juce::var varOf (const OscArg& arg)
{
    switch (arg.tag)
    {
        case 'i': return juce::var (std::get<int32_t> (arg.value));
        case 'h': case 'r': case 't': return juce::var (static_cast<juce::int64> (std::get<int64_t> (arg.value)));
        case 'T': return juce::var (true);
        case 'F': return juce::var (false);
        case 'f': case 'd':
            if (const auto* single = std::get_if<float> (&arg.value))
                return juce::var (static_cast<double> (*single));
            return juce::var (std::get<double> (arg.value));
        case 's': return juce::var (juce::String::fromUTF8 (std::get<std::string> (arg.value).c_str()));
        default: return juce::var();
    }
}
}

bool notReplayed (const std::string& key)
{
    return key == "channel.3d" || key == "stemdeck.ahead" || notPassedOnNorReplayed (key);
}

std::optional<std::string> currentName (const Truth& truth, const std::string& address)
{
    try
    {
        for (const auto& rule : renamings)
            if (const auto found = renamed (truth, rule, address))
                return found;
    }
    catch (const TruthError&)
    {
        return std::nullopt;
    }
    return address;
}

juce::var eveningState (const Relayed& relayed)
{
    auto* values = new juce::DynamicObject();
    for (const auto& [address, value] : relayed.messages())
        values->setProperty (juce::String::fromUTF8 (address.c_str()), varOf (value));
    auto* state = new juce::DynamicObject();
    state->setProperty ("values", juce::var (values));
    return juce::var (state);
}

std::vector<Message> replayable (const Truth& truth, const juce::var& state)
{
    std::vector<Message> out;
    auto* file = state.getDynamicObject();
    auto* values = file != nullptr ? file->getProperty ("values").getDynamicObject() : nullptr;
    if (values == nullptr)
        return out;

    for (const auto& property : values->getProperties())
    {
        const auto named = currentName (truth, property.name.toString().toStdString());
        if (! named || named->empty())
            continue;
        const auto found = truth.match (*named);
        if (! found)
            continue;
        const auto& key = found->first;
        if (! truth.addressTruthy (key, "relayed_to") || notReplayed (key))
            continue;
        if (const auto arg = argOf (property.value))
            out.emplace_back (*named, *arg);
    }
    return out;
}
}
