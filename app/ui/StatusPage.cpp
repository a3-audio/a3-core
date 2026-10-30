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

juce::String droppedText (const std::vector<std::string>& droppedLinks, int maxLines)
{
    return droppedText (droppedLinks, {}, maxLines);
}

juce::String droppedText (const std::vector<std::string>& droppedLinks, const std::vector<std::string>& ignoredKeys, int maxLines)
{
    if (droppedLinks.empty() && ignoredKeys.empty())
        return "none";
    // "deckA_cue_L -> x:y" joins the line of deckA; anything else keeps a line of its own.
    std::vector<std::pair<std::string, std::string>> groups;
    for (const auto& link : droppedLinks)
    {
        const auto port = link.substr (0, link.find (" -> "));
        const auto cut = port.find ('_');
        const bool deckPort = port.rfind ("deck", 0) == 0 && cut != std::string::npos;
        const auto key = deckPort ? port.substr (0, cut) : link;
        const auto item = deckPort ? port.substr (cut + 1) : std::string();
        auto same = std::find_if (groups.begin(), groups.end(), [&] (const auto& g) { return g.first == key; });
        if (same == groups.end())
            same = groups.insert (groups.end(), { key, "" });
        same->second += (same->second.empty() ? "" : " ") + item;
    }
    juce::StringArray lines;
    if (const auto count = (int) droppedLinks.size(); count > 0)
        lines.add (juce::String (count) + (count == 1 ? " retired link dropped:" : " retired links dropped:"));
    for (const auto& [key, items] : groups)
        lines.add (items.empty() ? juce::String (key) : juce::String (key) + ": " + items);
    const int linkLines = ignoredKeys.empty() ? maxLines : juce::jmax (1, maxLines - 1);   // the keys keep their line
    if (lines.size() > linkLines)
    {
        const int hidden = lines.size() - juce::jmax (1, linkLines - 1);
        lines.removeRange (juce::jmax (1, linkLines - 1), hidden);
        lines.add ("+" + juce::String (hidden) + " more");
    }
    if (! ignoredKeys.empty())
    {
        juce::StringArray keys;
        for (const auto& key : ignoredKeys)
            keys.add (key);
        const auto count = (int) ignoredKeys.size();
        lines.add (juce::String (count) + (count == 1 ? " retired key ignored: " : " retired keys ignored: ")
                   + keys.joinIntoString (", "));
    }
    return lines.joinIntoString ("\n");
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

DriverInfo driverInfo (const AppConfig& running, const BackendStatus& status)
{
    const bool device = running.backend.type == BackendSettings::Type::device;
    const int periods = status.timing.available ? status.timing.applied.periods : running.backend.periods;
    return { juce::String (juce::roundToInt (status.sampleRate)) + " Hz",
             device ? "Block " + juce::String (status.blockSize) + " frames" : timingText (status.blockSize, periods),
             status.rateSource,
             juce::String (status.bitDepth) + (device ? "  -  set by the device" : "  -  float samples, set by JACK"),
             juce::String (status.dspLoadPercent, 1) + " %",
             juce::String (status.xruns) };
}

StatusPage::StatusPage (HostView& v) : view (v), meters (std::make_unique<Meters>())
{
    addAndMakeVisible (*meters);
    addAndMakeVisible (clock);
    refresh();
}

StatusPage::~StatusPage() = default;

void StatusPage::refresh()
{
    config = view.running();   // the pending edit belongs to the AUDIO page
    status = view.status();
    problem = view.engineProblem();
    dropped = view.droppedLinks();
    ignored = view.ignoredKeys();
    truthWarning = view.truthNote();
    heard = view.coreHeard();
    sinceCore = view.secondsSinceCore();
    gate = view.gateOpen();
    gates = view.gateLevels();
    meters->set (view.meters(), view.outputNames());
    clock.show (view.clock());
    repaint (getLocalBounds().removeFromTop (getHeight() / 2));
}

// The lines on the top half, the clock's card and the meters below them.
void StatusPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    area.removeFromTop (area.getHeight() / 2);
    clock.setTextHeight ((float) getHeight() / 60.0f);
    clock.setBounds (area.removeFromTop (area.getHeight() / 2).withTrimmedTop (getHeight() / 80));
    meters->setBounds (area.withTrimmedTop (getHeight() / 80));
}

void StatusPage::paint (juce::Graphics& g)
{
    g.fillAll (palette::surface);
    auto area = getLocalBounds().reduced (getWidth() / 40);
    area.removeFromBottom (area.getHeight() / 2 + getHeight() / 80);
    constexpr int rowCount = 13;   // ten single lines; DROPPED takes the other three
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

    const auto driver = driverInfo (config, status);
    const auto audioText = driver.rate + ",  " + driver.timing;
    line ("AUDIO", truthWarning.isEmpty() ? audioText : audioText + "   (" + truthWarning + ")", palette::text);
    line ("RATE FROM", driver.rateSource, palette::textMuted);
    line ("BIT DEPTH", driver.bitDepth, palette::textMuted);
    constexpr float loadWarn = 70.0f, loadDanger = 90.0f;
    line ("DSP LOAD", driver.load,
          status.dspLoadPercent >= loadDanger ? palette::danger : status.dspLoadPercent >= loadWarn ? palette::caution : palette::text);
    line ("XRUNS", driver.xruns, status.xruns > 0 ? palette::caution : palette::text);
    std::vector<std::string> missing;
    for (const auto& peer : status.missingPeers)
        missing.push_back (peer.toStdString());
    line ("MISSING", missingText (missing), missing.empty() ? palette::textMuted : palette::caution);

    // The dropped links take the rest of the block: a count, then one link a line.
    auto rest = area;
    const bool none = dropped.empty() && ignored.empty();
    g.setColour (palette::textMuted);
    g.drawText ("DROPPED", rest.removeFromLeft (rest.getWidth() / 4), juce::Justification::topLeft, true);
    g.setColour (none ? palette::textMuted : palette::caution);
    const int maxLines = juce::jmax (2, (int) ((float) rest.getHeight() / ((float) row * 0.65f)));
    g.drawFittedText (droppedText (dropped, ignored, maxLines), rest, juce::Justification::topLeft, maxLines, 0.8f);
}
}
