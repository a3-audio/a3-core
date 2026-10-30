// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A row of sub-page keys (the A3 look): one on at a time, equal widths a unit apart. Only JUCE and the look.
#pragma once
#include "Tokens.h"
#include <functional>
#include <memory>

namespace a3look
{
class SubPageKeys final : public juce::Component
{
public:
    explicit SubPageKeys (juce::StringArray names)
    {
        for (int i = 0; i < names.size(); ++i)
            add (names[i]);
    }
    void add (const juce::String& name)
    {
        auto key = std::make_unique<juce::TextButton> (name);
        key->setRadioGroupId (radioGroup);
        key->setClickingTogglesState (false);   // select() decides which one is on
        key->onClick = [this, index = (int) keys.size()] { if (onChoose) onChoose (index); };
        addAndMakeVisible (*key);
        keys.push_back (std::move (key));
        resized();
    }
    void select (int index)
    {
        for (size_t i = 0; i < keys.size(); ++i)
            keys[i]->setToggleState ((int) i == index, juce::dontSendNotification);
    }
    juce::TextButton* key (const juce::String& name) const
    {
        for (const auto& k : keys)
            if (k->getButtonText() == name)
                return k.get();
        return nullptr;
    }
    std::function<void (int index)> onChoose;
    void resized() override
    {
        std::vector<juce::Component*> parts;
        for (auto& k : keys)
            parts.push_back (k.get());
        layout::equalRow (getLocalBounds(), parts, layout::tokens (*this));
    }

private:
    static constexpr int radioGroup = 41;
    std::vector<std::unique_ptr<juce::TextButton>> keys;
};
}
