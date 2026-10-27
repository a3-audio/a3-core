// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CoreState.h"

#include <utility>

#include "Json.h"

namespace a3core
{
namespace
{
// A file written before the rename says toggle_pfl.
const std::vector<std::pair<std::string, std::string>> renamedChannelFields { { "toggle_pfl", "toggle_cue" } };

// The Python Core stores whatever the file holds; here a value that is not the kind of thing the
// field holds (a text where a number is due) is skipped, because the Python Core would fail with
// it at its first recall and a state file must not stop a start.
void setFlag (bool& flag, const juce::var& value)
{
    if (value.isBool() || numberOf (value))
        flag = pythonTruthy (value);
}

void setNumber (double& number, const juce::var& value)
{
    if (const auto read = numberOf (value))
        number = *read;
}

void setCrossfade (std::optional<double>& crossfade, const juce::var& value)
{
    if (value.isVoid())
        crossfade.reset();   // the file said "no value": Python sets None
    else if (const auto read = numberOf (value))
        crossfade = *read;
}

void applyChannel (ChannelInfo& channel, const juce::var& remembered)
{
    auto* object = remembered.getDynamicObject();
    if (object == nullptr)
        return;
    for (const auto& [old, current] : renamedChannelFields)
        if (object->hasProperty (old.c_str()) && ! object->hasProperty (current.c_str()))
            setFlag (channel.toggleCue, object->getProperty (old.c_str()));
    if (object->hasProperty ("toggle_fx"))
        setFlag (channel.toggleFx, object->getProperty ("toggle_fx"));
    if (object->hasProperty ("toggle_cue"))
        setFlag (channel.toggleCue, object->getProperty ("toggle_cue"));
    if (object->hasProperty ("three_d"))
        setCrossfade (channel.threeD, object->getProperty ("three_d"));
}

void applyFxMode (MasterInfo& master, const juce::var& value)
{
    const auto number = numberOf (value);
    if (number == 0.0)
        master.fxMode = FxMode::lowPass;
    else if (number == 1.0)
        master.fxMode = FxMode::highPass;   // a mode this version does not have leaves the mode alone
}
}

juce::var stateOf (const std::vector<ChannelInfo>& channels, const MasterInfo& master, const juce::var* stemsData)
{
    juce::Array<juce::var> remembered;
    for (const auto& channel : channels)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("toggle_fx", channel.toggleFx);
        object->setProperty ("toggle_cue", channel.toggleCue);
        object->setProperty ("three_d", channel.threeD ? juce::var (*channel.threeD) : juce::var());
        remembered.add (juce::var (object));
    }

    auto* state = new juce::DynamicObject();
    state->setProperty ("channels", juce::var (remembered));
    state->setProperty ("fx_mode", static_cast<int> (master.fxMode));
    state->setProperty ("phones_mix", master.phonesMix);
    state->setProperty ("return_cue", master.returnCue);
    if (stemsData != nullptr)
        state->setProperty ("stems", stemsData->clone());
    return juce::var (state);
}

void applyState (const juce::var& state, std::vector<ChannelInfo>& channels, MasterInfo& master)
{
    auto* object = state.getDynamicObject();
    if (object == nullptr)
        return;

    if (auto* remembered = object->getProperty ("channels").getArray())
        for (size_t i = 0; i < channels.size() && static_cast<int> (i) < remembered->size(); ++i)
            applyChannel (channels[i], remembered->getReference (static_cast<int> (i)));

    if (object->hasProperty ("phones_mix"))
        setNumber (master.phonesMix, object->getProperty ("phones_mix"));
    if (object->hasProperty ("return_cue"))
        setFlag (master.returnCue, object->getProperty ("return_cue"));
    if (object->hasProperty ("fx_mode"))
        applyFxMode (master, object->getProperty ("fx_mode"));
}
}
