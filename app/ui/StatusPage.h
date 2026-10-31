// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "HostView.h"
#include "ClockPanel.h"
#include "CoreClient.h"
#include "Look.h"
#include <atomic>
#include <functional>
#include <optional>

namespace a3app
{
// Until 2026-10-31 the Core row went red ("Core silent for N s") when Core had sent nothing for more than 5 s. Silence is not
// a fault when there is nothing to send; now the row asks Core's API (GET /api/status) and is red only when it does not answer.
// nullopt: not asked (no monitor client), the row then only reports the last message and is never red.
using CoreAnswers = std::optional<bool>;

// Asks Core's API every two seconds on its own thread (the window's tick must not wait on the network) and keeps the answer.
class CoreProbe : private juce::Thread
{
public:
    explicit CoreProbe (CoreClient& c) : juce::Thread ("core probe"), client (c) { startThread(); }
    ~CoreProbe() override { stopThread (2000); }
    CoreAnswers answered() const { return known.load() ? CoreAnswers (up.load()) : CoreAnswers(); }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            up = client.answers();
            known = true;
            wait (2000);
        }
    }
    CoreClient& client;
    std::atomic<bool> up { false }, known { false };
};

juce::String modeText (const AppConfig&, const BackendStatus&);   // "JACK, connecting", "Device, no links (--no-connect)"
juce::String coreText (bool heard, double secondsSince, CoreAnswers answers);   // "Core running, last message 3.6 s ago"

// "client:port" names as (client, count), in the order the clients first appear.
std::vector<std::pair<std::string, int>> groupMissingByClient (const std::vector<std::string>& missingPorts);
juce::String missingText (const std::vector<std::string>& missingPorts);   // "AES67 deck B (41), system (1)" or "none"
// A count, then one line per deck slot ("deckA: cue_L cue_R"), or "none"; a line past maxLines
// becomes "+N more", so a cut is always said.
juce::String droppedText (const std::vector<std::string>& droppedLinks, int maxLines = 1000);
juce::String droppedText (const std::vector<std::string>& droppedLinks, const std::vector<std::string>& ignoredKeys,
                          int maxLines = 1000);
// The MONITOR's lines of what the engine's config named and the load left out (droppedText), "" when nothing was.
juce::String retiredText (const HostView&);

// What the driver runs, as STATUS says it (the AUDIO page's info lines until 2026-10-30): "44100 Hz", "Block 64 frames
// x 3 periods" (a device: no periods), where the rate comes from, the bit depth and who sets it, the DSP load, xruns.
struct DriverInfo
{
    juce::String rate, timing, rateSource, bitDepth, load, xruns, roundTrip;   // roundTrip: the estimate (AUDIO says the measured)
};
DriverInfo driverInfo (const AppConfig& running, const BackendStatus&);

// ENGINE's round-trip lines (2026-10-31), values only: the estimate ("16.0 ms (768 samples)"), the digital loop as measured
// ("4.4 ms  14:05", else "not measured") and the analog chain (that digital measurement plus the stored converters' share,
// "6.1 ms  2026-10-30", else "not measured").
juce::String estimatedText (const RoundTrip&);
juce::String measuredDigitalText (const RoundTrip&, const RoundTripReport&);
juce::String measuredAnalogText (const RoundTrip&, const RoundTripReport&);

// A deck's stream over the network: the deck host's sender (its sess.latency and packet time, StemDeck's shipped values:
// the Core does not know the deck host's) plus the Core's receiver buffer, the deck rule's sess.latency.msec in the
// rendered pipewire-aes67.conf. nullopt: no deck rule there.
std::optional<double> deckReceiveLatencyMs (const juce::String& aes67Conf);
juce::String aes67LatencyText (std::optional<double> receiverMs);   // "9.3 ms", or "no AES67 receiver config"
// A³ CORE's rows (2026-10-31: the round trip and the network in the same rows as the driver's lines): a caption, its
// value, and how the value is drawn -- plain text, dim (something that should be there and is not), or a fault (red).
struct StatusRow
{
    enum class Tone { plain, dim, fault };
    juce::String caption, value;
    Tone tone = Tone::plain;
};
// Estimated, Measured digital, Measured analog, AES67 latency: "not measured" and a missing AES67 config dim.
std::vector<StatusRow> roundTripRows (const RoundTrip&, const RoundTripReport&, const juce::String& aes67);
// Every row of the first card, drawn "Label: value": Mode, Core, Gate, Audio, Rate from, Bit depth, DSP load, Xruns, then
// roundTripRows.
std::vector<StatusRow> statusRows (const juce::String& modeLine, bool modeFault, const juce::String& coreLine, bool coreHeard,
                                   bool coreSilent, const juce::String& gateLine, const DriverInfo&, const juce::String& truthWarning,
                                   const BackendStatus&, const juce::String& aes67);
inline DriverInfo driverInfo (const HostView& view) { return driverInfo (view.running(), view.status()); }

// Read-only: engine mode, Core's last word, the gate per zone, load and xruns, missing peers
// and the per-port meters; the driver's details (DriverInfo) and the clock's (ClockPanel::Mode::detail). refresh() re-reads the view (the window's timer calls it).
class StatusPage final : public juce::Component, public CardPage
{
public:
    // `aes67Config`: the rendered PipeWire AES67 config (~/.config/pipewire/pipewire-aes67.conf by default).
    explicit StatusPage (HostView&, juce::File aes67Config = defaultAes67Config());
    // Whether Core's API answered (cached, never blocks); unset: unknown.
    std::function<CoreAnswers()> coreAnswers;
    static juce::File defaultAes67Config();
    ~StatusPage() override;

    void refresh();
    // What runs, not the pending edit; or why no engine runs.
    const ClockPanel& clockPanel() const { return clock; }
    // The window says a3-core, not "engine" (2026-10-31): the engine role's own "NO ENGINE RUNNING: " is left off.
    juce::String modeLine() const
    {
        if (problem.isEmpty())
            return modeText (config, status);
        const juce::String engineWords = "NO ENGINE RUNNING: ";
        return "A3-CORE NOT RUNNING - " + (problem.startsWith (engineWords) ? problem.substring (engineWords.length()) : problem);
    }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Meters;
    int row() const;
    HostView& view;
    int clockLinesLaid = 0;
    juce::String aes67;
    std::unique_ptr<Meters> meters;
    ClockPanel clock { ClockPanel::Mode::detail };
    AppConfig config;
    BackendStatus status;
    juce::String problem;
    std::vector<std::string> dropped, ignored;
    juce::String truthWarning;
    bool heard = false, gate = false;
    double sinceCore = 0;
    CoreAnswers answers;
    a3::GateLevels gates;
};
}
