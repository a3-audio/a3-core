// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A HostView with plain fields, for the pages' tests and for what stands behind the control API in the
// remote view's tests.
#pragma once
#include "EngineFixture.h"
#include "HostView.h"
#include "SpatialTestData.h"

namespace a3apptest
{
struct FakeView final : a3app::HostView
{
    FakeView()
    {
        cfg.engine = a3test::engineConfig (4, 2);
        cfg.spatialDirectory = a3test::shippedSpatialDirectory;
        file = juce::File::createTempFile ("a3-pages-config");
        runningCfg = cfg;
        state.sampleRate = 44100;
        state.blockSize = 128;
        state.rateSource = "set by the JACK server";
        state.bitDepth = 32;
        state.connecting = true;
        for (int i = 0; i < 6; ++i) { meterValues.push_back ({ 0.1f * (float) i, 0.05f * (float) i }); names.push_back ("out_" + std::to_string (i)); }
    }
    a3app::BackendStatus status() const override { return state; }
    std::vector<a3::Engine::OutputMeter> meters() override { return meterValues; }
    std::vector<std::string> outputNames() const override { return names; }
    bool coreHeard() const override { return heard; }
    double secondsSinceCore() const override { return sinceCore; }
    bool gateOpen() const override { return open; }
    a3::GateLevels gateLevels() const override { a3::GateLevels g; for (auto& v : g.gain) v = open ? 1.0f : 0.0f; return g; }
    a3app::AppConfig config() const override { return cfg; }
    a3app::AppConfig running() const override { return runningCfg; }
    juce::String engineProblem() const override { return problem; }
    juce::File configFile() const override { return file; }
    a3app::LiveLinks liveLinks() const override { return links; }
    void edit (const a3app::AppConfig& c) override { cfg = c; ++edited; }
    juce::Result apply (const a3app::AppConfig& c) override
    {
        ++applied;
        fileExistedAtApply = file.existsAsFile();
        if (applyResult.wasOk())
            cfg = runningCfg = c;
        return applyResult;
    }
    void setFilterKind (a3::FilterKind kind) override { cfg.engine.filterKind = runningCfg.engine.filterKind = kind; ++filterSets; }
    ~FakeView() override { file.deleteFile(); }

    void runDevice() { cfg.backend.type = runningCfg.backend.type = a3app::BackendSettings::Type::device; }
    a3app::AppConfig cfg, runningCfg;   // the pending edit and what runs
    a3app::BackendStatus state;
    std::vector<a3::Engine::OutputMeter> meterValues;
    std::vector<std::string> names;
    bool heard = true, open = true;
    double sinceCore = 0.4;
    juce::File file;
    a3app::LiveLinks links;
    juce::Result applyResult = juce::Result::ok();
    juce::String problem;
    bool fileExistedAtApply = false;
    int edited = 0, applied = 0, filterSets = 0;
};
}
