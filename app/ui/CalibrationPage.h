// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "Look.h"
#include <functional>
#include <memory>
#include <vector>

namespace a3app
{
// Trim per zone in dB, -90..+6. The sliders only propose a value; the trim changes after a
// confirmation naming the old and the new value. A zone without a trim is closed ("not set").
class CalibrationPage final : public juce::Component
{
public:
    // Asks the person; calls onYes if they agree. The default is a modal box; tests pass their own.
    using Confirm = std::function<void (const juce::String& message, std::function<void()> onYes)>;
    explicit CalibrationPage (HostView&, Confirm = {});
    ~CalibrationPage() override;

    void proposeTrim (TrimZone, float db);
    void refresh();
    double sliderRangeMin() const { return rows.front()->proposal.getMinimum(); }
    double sliderRangeMax() const { return rows.front()->proposal.getMaximum(); }
    double proposalValue (TrimZone zone) const { return rows[(size_t) zone]->proposal.getValue(); }
    const juce::Label& messageLabel() const { return message; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    struct Row
    {
        explicit Row (TrimZone z) : zone (z) {}
        TrimZone zone;
        juce::Label name, current;
        juce::Slider proposal { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        juce::TextButton set { "Set..." };
    };
    static juce::String describe (std::optional<float>);

    HostView& view;
    Confirm confirm;
    juce::Label title, message, helpText;
    std::vector<std::unique_ptr<Row>> rows;
};
}
