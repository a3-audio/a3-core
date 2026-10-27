// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <initializer_list>
#include <juce_core/juce_core.h>

// Helpers for the config parsers (engine section, app file, I/O map). A refusal is
// thrown inside one parser and caught at its entry point, never across it; parsing
// is off the audio thread.
namespace a3::jsonparse
{
struct Refusal { juce::String reason; };

[[noreturn]] inline void refuse (const juce::String& reason) { throw Refusal { reason }; }

inline juce::DynamicObject& objectOf (const juce::var& v, const juce::String& what)
{
    auto* object = v.getDynamicObject();
    if (object == nullptr)
        refuse (what + " must be an object");
    return *object;
}

inline void rejectUnknownKeys (const juce::DynamicObject& object, std::initializer_list<const char*> known, const juce::String& where)
{
    for (const auto& property : object.getProperties())
    {
        bool found = false;
        for (const char* key : known)
            found = found || property.name.toString() == key;
        if (! found)
            refuse ("unknown key '" + property.name.toString() + "' in " + where);
    }
}
}
