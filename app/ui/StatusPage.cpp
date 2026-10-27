// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StatusPage.h"
#include <algorithm>
#include <cmath>

namespace a3app
{
juce::String modeText (const AppConfig& config, const BackendStatus& status)
{
    const juce::String backend = config.backend.type == BackendSettings::Type::jack ? "JACK" : "Device";
    return backend + (status.connecting ? ", connecting" : ", no links (--no-connect)");
}

juce::String coreText (bool heard, double secondsSince)
{
    if (! heard)
        return "Core not heard yet";
    const auto seconds = juce::String (secondsSince, 1) + " s";
    return secondsSince > coreSilentAfterSeconds ? "Core silent for " + seconds : "Core heard, last message " + seconds + " ago";
}

std::vector<std::pair<std::string, int>> groupMissingByClient (const std::vector<std::string>& missingPorts)
{
    std::vector<std::pair<std::string, int>> groups;
    for (const auto& port : missingPorts)
    {
        const auto client = port.substr (0, port.find (':'));
        const auto same = std::find_if (groups.begin(), groups.end(), [&] (const auto& g) { return g.first == client; });
        if (same == groups.end())
            groups.push_back ({ client, 1 });
        else
            ++same->second;
    }
    return groups;
}

juce::String missingText (const std::vector<std::string>& missingPorts)
{
    juce::StringArray parts;
    for (const auto& [client, count] : groupMissingByClient (missingPorts))
        parts.add (juce::String (client) + " (" + juce::String (count) + ")");
    return parts.isEmpty() ? juce::String ("none") : parts.joinIntoString (", ");
}

juce::String droppedText (const std::vector<std::string>& droppedLinks)
{
    if (droppedLinks.empty())
        return "none";
    const auto count = (int) droppedLinks.size();
    juce::String text = juce::String (count) + (count == 1 ? " retired link dropped:" : " retired links dropped:");
    for (const auto& link : droppedLinks)
        text << "\n" << link;
    return text;
}

namespace
{
constexpr float meterFloorDb = -60.0f;
constexpr float meterWarnDb = -6.0f, meterClipDb = -0.5f;

// 0..1 along the meter; a value that is not finite or not positive is silence, never a bar.
float meterPosition (float linear)
{
    if (! std::isfinite (linear) || linear <= 0.0f)
        return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (linear) - meterFloorDb) / -meterFloorDb);
}

juce::Colour meterColour (float peakLinear)
{
    const float db = juce::Decibels::gainToDecibels (std::isfinite (peakLinear) ? peakLinear : 0.0f);
    return db >= meterClipDb ? palette::danger : db >= meterWarnDb ? palette::caution : palette::running;
}
}

// Opaque and its own component: a thing that refreshes itself must not drag its parent into repainting.
class StatusPage::Meters final : public juce::Component
{
public:
    Meters() { setOpaque (true); }
    void set (std::vector<a3::Engine::OutputMeter> m, std::vector<std::string> n)
    {
        meters = std::move (m);
        names = std::move (n);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (palette::raised);
        if (meters.empty())
            return;
        const int count = (int) meters.size();
        constexpr int minRow = 14;   // a row that still reads
        const int rowsPerColumn = juce::jmax (1, getHeight() / minRow);
        const int columns = (count + rowsPerColumn - 1) / rowsPerColumn;
        const int rows = (count + columns - 1) / columns;
        const float rowHeight = (float) getHeight() / (float) rows, columnWidth = (float) getWidth() / (float) columns;
        const auto inset = juce::jmax (2.0f, columnWidth * 0.02f);
        g.setFont (juce::FontOptions (juce::jmin (rowHeight * 0.7f, 13.0f)));
        for (int i = 0; i < count; ++i)
        {
            const float x = (float) (i / rows) * columnWidth, y = (float) (i % rows) * rowHeight;
            auto cell = juce::Rectangle<float> (x, y, columnWidth, rowHeight).reduced (inset, rowHeight * 0.15f);
            g.setColour (palette::textMuted);
            const auto name = i < (int) names.size() ? juce::String (names[(size_t) i]) : juce::String (i);
            g.drawText (name, cell.removeFromLeft (cell.getWidth() * 0.4f), juce::Justification::centredLeft, true);
            g.setColour (palette::ground);
            g.fillRoundedRectangle (cell, cell.getHeight() * 0.3f);
            const auto& meter = meters[(size_t) i];
            g.setColour (meterColour (meter.peak).withAlpha (0.45f));
            g.fillRoundedRectangle (cell.withWidth (cell.getWidth() * meterPosition (meter.rms)), cell.getHeight() * 0.3f);
            g.setColour (meterColour (meter.peak));
            g.fillRect (cell.getX() + cell.getWidth() * meterPosition (meter.peak) - 1.0f, cell.getY(), 2.0f, cell.getHeight());
        }
    }

private:
    std::vector<a3::Engine::OutputMeter> meters;
    std::vector<std::string> names;
};

StatusPage::StatusPage (HostView& v) : view (v), meters (std::make_unique<Meters>())
{
    addAndMakeVisible (*meters);
    refresh();
}

StatusPage::~StatusPage() = default;

void StatusPage::refresh()
{
    config = view.running();   // the pending edit belongs to the AUDIO page
    status = view.status();
    problem = view.engineProblem();
    dropped = view.droppedLinks();
    heard = view.coreHeard();
    sinceCore = view.secondsSinceCore();
    gate = view.gateOpen();
    gates = view.gateLevels();
    meters->set (view.meters(), view.outputNames());
    repaint (getLocalBounds().removeFromTop (getHeight() / 2));
}

void StatusPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    meters->setBounds (area.removeFromBottom (area.getHeight() / 2));
}

void StatusPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    auto area = getLocalBounds().reduced (getWidth() / 40);
    area.removeFromBottom (area.getHeight() / 2 + getHeight() / 80);
    constexpr int rowCount = 10;   // seven single lines; DROPPED takes the other three
    const int row = area.getHeight() / rowCount;
    g.setFont (juce::FontOptions ((float) row * 0.55f));

    auto line = [&] (const juce::String& caption, const juce::String& value, juce::Colour colour)
    {
        auto r = area.removeFromTop (row);
        g.setColour (palette::textMuted);
        g.drawText (caption, r.removeFromLeft (r.getWidth() / 4), juce::Justification::centredLeft, true);
        g.setColour (colour);
        g.drawText (value, r, juce::Justification::centredLeft, true);
    };

    line ("MODE", modeLine(), ! problem.isEmpty() ? palette::danger : status.connecting ? palette::text : palette::caution);
    const bool silent = heard && sinceCore > coreSilentAfterSeconds;
    line ("CORE", coreText (heard, sinceCore), ! heard || silent ? palette::caution : palette::running);

    static const char* const zones[a3::numGatedZones] = { "main", "booth", "phones" };
    juce::String gateText;
    bool anyOpen = false;
    for (int z = 0; z < a3::numGatedZones; ++z)
    {
        const bool open = std::isfinite (gates.gain[z]) && gates.gain[z] > 0.0f;
        anyOpen = anyOpen || open;
        gateText << zones[z] << (open ? " open" : " closed") << (z + 1 < a3::numGatedZones ? "   " : "");
    }
    line ("GATE", gateText + (gate ? "   (Core: open)" : "   (Core: closed)"), anyOpen ? palette::running : palette::textMuted);

    line ("AUDIO", juce::String (juce::roundToInt (status.sampleRate)) + " Hz, " + juce::String (status.blockSize) + " frames", palette::text);
    constexpr float loadWarn = 70.0f, loadDanger = 90.0f;
    line ("DSP LOAD", juce::String (status.dspLoadPercent, 1) + " %",
          status.dspLoadPercent >= loadDanger ? palette::danger : status.dspLoadPercent >= loadWarn ? palette::caution : palette::text);
    line ("XRUNS", juce::String (status.xruns), status.xruns > 0 ? palette::caution : palette::text);
    std::vector<std::string> missing;
    for (const auto& peer : status.missingPeers)
        missing.push_back (peer.toStdString());
    line ("MISSING", missingText (missing), missing.empty() ? palette::textMuted : palette::caution);

    // The dropped links take the rest of the block: a count, then one link a line.
    auto rest = area;
    const bool none = dropped.empty();
    g.setColour (palette::textMuted);
    g.drawText ("DROPPED", rest.removeFromLeft (rest.getWidth() / 4), juce::Justification::topLeft, true);
    g.setColour (none ? palette::textMuted : palette::caution);
    g.drawFittedText (droppedText (dropped), rest, juce::Justification::topLeft, juce::jmax (1, (int) ((float) rest.getHeight() / ((float) row * 0.65f))), 0.8f);
}
}
