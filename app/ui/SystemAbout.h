// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What the ABOUT page says about the project (the Canvas's greeter, a3-system canvas/Source/model/Greeter,
// moved here on 2026-10-30: "all content from the Canvas goes to the Engine"). Pure: no component, no file.
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
// Documentation, Website, Source, Support A³.
std::vector<Link> links();

// Where "Support A³" leads: the website's link (footer.pug), the same for everyone -- no file is read.
juce::String supportUrl();
}
