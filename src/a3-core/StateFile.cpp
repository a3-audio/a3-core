// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StateFile.h"

#include <filesystem>
#include <system_error>

#include "Json.h"

namespace a3core
{
StateFile::StateFile (juce::File fileToUse, double delayToUse) : path (std::move (fileToUse)), delay (delayToUse) {}

juce::var StateFile::load() const
{
    const auto empty = [] { return juce::var (new juce::DynamicObject()); };

    juce::MemoryBlock bytes;
    if (! path.loadFileAsData (bytes))
        return empty();
    const auto* chars = static_cast<const char*> (bytes.getData());
    const auto size = static_cast<int> (bytes.getSize());
    if (! juce::CharPointer_UTF8::isValidString (chars, size))
        return empty();

    // Not loadFileAsString: that drops a byte-order mark, which Python's json.loads refuses.
    juce::String error;
    auto state = parseJson (juce::String::fromUTF8 (chars, size), error);
    return error.isEmpty() ? state : empty();
}

void StateFile::remember (const juce::var& state, double now, bool freshNan)
{
    if (last && ! freshNan && pythonEquals (state, *last, true))
        return;
    last = state.clone();
    pending = state.clone();
    dueAt = now + delay;
}

void StateFile::flushIfDue (double now)
{
    if (pending && now >= dueAt)
        flush();
}

void StateFile::flush()
{
    if (! pending)
        return;
    const auto state = *pending;
    pending.reset();
    if (write (state))
        ++written;
}

bool StateFile::write (const juce::var& state) const
{
    if (! path.getParentDirectory().createDirectory())
        return false;
    const auto temporary = path.getSiblingFile (path.getFileName() + ".new");
    const auto text = dumpIndented (state, 2) + "\n";
    if (! temporary.replaceWithData (text.data(), text.size()))
        return false;

    std::error_code problem;
    std::filesystem::rename (temporary.getFullPathName().toStdString(), path.getFullPathName().toStdString(), problem);
    if (problem)
    {
        temporary.deleteFile();
        return false;
    }
    return true;
}
}
