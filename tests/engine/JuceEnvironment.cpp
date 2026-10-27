// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <juce_events/juce_events.h>
#include <memory>

namespace
{
// IEM's AudioProcessorValueTreeState is a juce::Timer: it needs the message
// manager alive for the whole run, not per test.
class JuceEnvironment : public ::testing::Environment
{
public:
    void SetUp() override { juce = std::make_unique<juce::ScopedJuceInitialiser_GUI>(); }
    void TearDown() override { juce.reset(); }

private:
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juce;
};

const auto* const registered = ::testing::AddGlobalTestEnvironment (new JuceEnvironment);
}
