// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What the clock loop measures (ClockSteering.h says why): the Scarlett's sample clock from PipeWire's
// profiler and the PHC against CLOCK_MONOTONIC, brought to one phase difference per update.
//
// The Scarlett: the graph driver's "clock" record in `pw-profiler -J` carries nsec (MONOTONIC) and
// position + delay, the sample at the converter at that instant (12.5 us rms per record on the rig,
// 2026-10-30). A second of records is reduced to its centroid (mean nsec, mean Scarlett time): a point
// on the fitted line, with the jitter divided by the root of the count and no slope needed.
// The PHC: read in a MONOTONIC-PHC-MONOTONIC sandwich once per update; the PHC at the centroid's instant
// is interpolated between two such readings. Both sides are MONOTONIC, so NTP slewing it cancels.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace a3clock
{
// One "clock" record of the profiler.
struct ProfilerClock
{
    int id = 0;
    std::uint32_t flags = 0;
    std::int64_t nsec = 0;        // CLOCK_MONOTONIC
    std::int64_t position = 0;    // samples
    std::int64_t delay = 0;       // samples
    int rate = 0;                 // Hz (the record's "1/<rate>")
    std::int64_t xrun = 0;        // the driver's xrun count
    std::string_view name;        // into the parsed line
};

// A line of `pw-profiler -J` that is a "clock" record, else nullopt (other records, half lines,
// anything malformed). Allocates nothing.
std::optional<ProfilerClock> parseProfilerClock (std::string_view line);

// The Scarlett's time at an instant, from its epoch's first sample.
struct ScarlettPoint
{
    std::uint64_t epoch = 0;      // changes at every discontinuity
    std::int64_t monoNs = 0;      // CLOCK_MONOTONIC
    std::int64_t scarlettNs = 0;  // samples since the epoch's first, as ns at the nominal rate
    int records = 0;
};

struct IntakeConfig
{
    std::string_view namePrefix = "api.alsa.";   // the driver's clock name (the Scarlett: api.alsa.<card>)
    double maxJumpNs = 1'000'000.0;               // position + delay off its prediction by more: discontinuity
    double maxGapNs = 1'000'000'000.0;            // no record for longer: discontinuity
    int minRecords = 5;                           // a centroid of fewer is not taken
};

// The profiler's records of one driver, a window at a time. A discontinuity -- an xrun, freewheel, a
// position that jumped, a gap, another node or rate, or the feed restarting -- starts a new epoch.
class ScarlettIntake
{
public:
    explicit ScarlettIntake (IntakeConfig = {});

    enum class Taken { used, ignored, discontinuity };
    Taken add (const ProfilerClock&);
    void restart();   // the feed was lost (the profiler process ended): the next record starts an epoch

    // The window's centroid, and a new window; nullopt (the window kept) while it has fewer than
    // minRecords.
    std::optional<ScarlettPoint> take();

    std::uint64_t epoch() const { return currentEpoch; }
    std::int64_t discontinuities() const { return breaks; }
    int lockedId() const { return id; }

private:
    void startEpoch (const ProfilerClock&, std::int64_t samples);
    void clearWindow();

    IntakeConfig cfg;
    bool started = false;
    int id = -1, rate = 0;
    std::int64_t xrun = 0;
    std::uint64_t currentEpoch = 0;
    std::int64_t breaks = 0;
    std::int64_t epochSamples = 0;     // position + delay of the epoch's first record
    std::int64_t lastNsec = 0, lastSamples = 0;
    // The window: sums relative to its first record, so doubles keep their precision.
    std::int64_t baseNsec = 0, baseScarlettNs = 0;
    double sumNsec = 0.0, sumScarlett = 0.0;
    int count = 0;
};

// One PHC reading: the PHC (ns, its own epoch) at a MONOTONIC instant.
struct PhcSample
{
    std::int64_t monoNs = 0;
    std::int64_t phcNs = 0;
};

// The last few PHC readings; the PHC at another MONOTONIC instant, interpolated between the two that
// bracket it (or extrapolated from the two nearest).
class PhcHistory
{
public:
    void add (PhcSample);
    void clear() { size = 0; }
    std::optional<std::int64_t> phcAt (std::int64_t monoNs) const;

private:
    static constexpr int capacity = 8;
    std::array<PhcSample, capacity> samples {};
    int size = 0;
};

// The rate of PHC against Scarlett over the last updates (the slope of the phase), in ppm: positive,
// the PHC runs faster. Restarts with every epoch.
class RateEstimate
{
public:
    void add (std::uint64_t epoch, double seconds, double phaseNs);
    std::optional<double> ppm() const;

private:
    static constexpr int capacity = 30;
    std::array<double, capacity> times {}, phases {};
    int size = 0, next = 0;
    std::uint64_t epoch = 0;
};
}
