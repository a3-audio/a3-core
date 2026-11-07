// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ClockSteering.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace a3clock
{
namespace
{
constexpr double nsPerUs = 1000.0;
constexpr double ppmAsNsPerSecond = 1000.0;
constexpr double damping = 1.0;   // critically damped: no ringing in the deck hosts' servos
}

const char* healthWord (Health health)
{
    switch (health)
    {
        case Health::acquiring: return "acquiring";
        case Health::locked: return "locked";
        case Health::holdover: return "holdover";
        case Health::fault: return "fault";
        case Health::observing: return "observing";
    }
    return "fault";
}

ClockSteering::ClockSteering (const SteeringConfig& config, double startFrequencyPpm, double nowSeconds,
                              std::optional<double> warmFrequencyPpm)
    : cfg (config),
      frequency (startFrequencyPpm),
      integral (startFrequencyPpm),
      lastMeasurementAt (nowSeconds)
{
    if (warmFrequencyPpm && inRange (*warmFrequencyPpm))
    {
        pendingWarm = warmFrequencyPpm;
        why = "warm start: the last locked frequency";
    }
}

// Critically damped PI: kp = 2/tau (ppm per us), ki = 1/tau^2 (ppm per us*s).
double ClockSteering::gainFor (double timeConstant, bool proportional) const
{
    return proportional ? 2.0 * damping / timeConstant : 1.0 / (timeConstant * timeConstant);
}

bool ClockSteering::inRange (double ppm) const { return std::abs (ppm - cfg.centreFrequencyPpm) <= cfg.frequencyRangePpm; }

// One write straight to `ppm`, then the phase is anchored afresh. Only while acquiring: once tracking,
// every change goes through the slew limit.
Decision ClockSteering::jumpTo (double ppm, const char* reason)
{
    auto decision = hold();
    frequency = integral = ppm;
    decision.write = ppm;
    decision.jump = true;
    decision.frequencyPpm = ppm;
    ++count.corrections;
    stage = Stage::tracking;
    anchored = false;
    why = decision.reason = reason;
    return decision;
}

// Frequency first: the phase's slope over rateUpdates measurements (about 5 s), at the frequency as
// found (nothing is written meanwhile), says where the PHC has to run.
Decision ClockSteering::measureRate (double nowSeconds, const Measurement& m)
{
    if (rate.count() == 0 || m.epoch != epoch)
    {
        rate.clear();
        epoch = m.epoch;
        rateOrigin = m.phaseNs;
    }
    rate.add (m.epoch, nowSeconds, (double) (m.phaseNs - rateOrigin));
    why = "measuring the rate against the Scarlett";
    const auto slope = rate.ppm();
    if (rate.count() < cfg.rateUpdates || ! slope)
        return hold();
    const double measured = frequency - *slope;
    // A rate just past the edge may be the 5 s measurement's noise: steer from the edge, the PI (and
    // the acquire limit, if it really is beyond) decides. Clearly beyond: refused before any write.
    const double low = cfg.centreFrequencyPpm - cfg.frequencyRangePpm, high = cfg.centreFrequencyPpm + cfg.frequencyRangePpm;
    const double target = std::clamp (measured, low, high);
    if (std::abs (measured - target) > cfg.atRatePpm)
    {
        writeFailed ("the PTP clock is further from the Scarlett than the frequency range: not steered");
        return hold();
    }
    rate.clear();
    if (std::abs (*slope) <= cfg.atRatePpm)
    {
        // Already at the rate, within what 5 s can tell (a restart on a locked PHC): nothing is written.
        stage = Stage::tracking;
        anchored = false;
        integral = frequency;
        why = "already at the Scarlett's rate: anchoring the phase";
        return hold();
    }
    return jumpTo (target, "at the Scarlett's rate: anchoring the phase");
}

// A warm start is checked: the anchored error's slope over the first rateUpdates measurements is what
// the saved frequency is off by. Beyond the tolerance, one more write corrects it; the anchor stays.
std::optional<double> ClockSteering::warmCorrection (double nowSeconds)
{
    rate.add (epoch, nowSeconds, error);
    const auto slope = rate.ppm();
    if (rate.count() < cfg.rateUpdates || ! slope)
        return std::nullopt;
    verifyingWarm = false;
    rate.clear();
    const double corrected = frequency - *slope;
    if (std::abs (*slope) <= cfg.warmTolerancePpm || ! inRange (corrected))
        return std::nullopt;
    return corrected;
}

Decision ClockSteering::hold() const
{
    Decision decision;
    decision.health = state;
    decision.frequencyPpm = frequency;
    decision.errorNs = error;
    decision.reason = why;
    return decision;
}

void ClockSteering::writeFailed (const char* reason)
{
    state = Health::fault;
    why = reason;
}

Decision ClockSteering::noMeasurement (double nowSeconds)
{
    if (nowSeconds - lastMeasurementAt >= cfg.holdoverAfterSeconds && state != Health::holdover)
    {
        state = Health::holdover;
        why = "no measurement: the last frequency is held";
        inThreshold = 0;
        ++count.holdovers;
    }
    return hold();
}

// The error after anchoring. A new Scarlett epoch, or a phase change no frequency in the range could
// have made (a stepped PHC), moves the anchor so that the error goes on from where it was.
double ClockSteering::anchoredError (const Measurement& m, double gapSeconds)
{
    if (! anchored)
    {
        anchored = true;
        epoch = m.epoch;
        offsetNs = m.phaseNs;
        smoothed = 0.0;
        return 0.0;
    }
    const auto reanchor = [&] {
        offsetNs = m.phaseNs - (std::int64_t) std::llround (error);
        return error;
    };
    if (m.epoch != epoch)
    {
        epoch = m.epoch;
        ++count.reanchors;
        return reanchor();
    }
    const double candidate = (double) (m.phaseNs - offsetNs);
    // What the held frequency can drift by in the gap, never the whole range: after a long holdover a
    // stepped PHC (an igb reset) must still be seen as a step, not as error to steer away.
    const double possible = std::min (cfg.phcStepNs + cfg.heldDriftPpm * ppmAsNsPerSecond * gapSeconds, cfg.faultNs);
    if (std::abs (candidate - error) > possible)
    {
        ++count.phcSteps;
        return reanchor();
    }
    return candidate;
}

// A PI on the smoothed phase error. When the range or the slew limit holds the output back, the
// integral only moves if that takes it back towards what is applied (conditional integration): it
// never runs ahead of the clock, and measurement noise is never written into it.
double ClockSteering::nextFrequency (double errorNs, double dtSeconds)
{
    const double weight = std::min (1.0, dtSeconds / std::max (cfg.errorSmoothingSeconds, dtSeconds));
    smoothed += weight * (errorNs - smoothed);
    const double errorUs = smoothed / nsPerUs;
    // The quiet time constant only once the lock has held a while: a residual the 5 s rate left is
    // pulled in by the fast one first.
    const double tau = inThreshold >= cfg.settleUpdates ? cfg.timeConstantSeconds : cfg.acquireTimeConstantSeconds;
    const double kp = gainFor (tau, true), ki = gainFor (tau, false);
    const double low = cfg.centreFrequencyPpm - cfg.frequencyRangePpm, high = cfg.centreFrequencyPpm + cfg.frequencyRangePpm;
    const double candidate = std::clamp (integral - ki * errorUs * dtSeconds, low, high);
    const double desired = candidate - kp * errorUs;
    const double applied = slewed (desired, dtSeconds);
    const bool heldBack = applied != desired;
    const bool integralPushesFurther = (desired - applied) * (candidate - integral) > 0.0;
    if (! (heldBack && integralPushesFurther))
        integral = candidate;
    return applied;
}

// Towards `desiredPpm`, inside the range and never faster than the slew limit.
double ClockSteering::slewed (double desiredPpm, double dtSeconds) const
{
    const double low = cfg.centreFrequencyPpm - cfg.frequencyRangePpm, high = cfg.centreFrequencyPpm + cfg.frequencyRangePpm;
    const double maxStep = cfg.maxSlewPpmPerSecond * dtSeconds;
    return frequency + std::clamp (std::clamp (desiredPpm, low, high) - frequency, -maxStep, maxStep);
}

// The phase error and the Scarlett's rate (the frequency that would hold the phase), estimated from what
// was measured and what was written: the PHC ran at `frequency` since the last update, so a change the
// loop made is never mistaken for the Scarlett's. An alpha-beta estimator with a double pole at
// exp(-dt/T): critically damped, and after a long gap (holdover) it trusts the measurement.
void ClockSteering::observe (double errorNs, double dtSeconds)
{
    const double pole = std::exp (-dtSeconds / cfg.observerTimeConstantSeconds);
    const double predicted = estimatedErrorNs + (frequency - scarlettPpm) * ppmAsNsPerSecond * dtSeconds;
    const double innovation = errorNs - predicted;
    estimatedErrorNs = predicted + (1.0 - pole * pole) * innovation;
    scarlettPpm -= (1.0 - pole) * (1.0 - pole) * innovation / (ppmAsNsPerSecond * dtSeconds);
}

// Braking outside the lock, or at a rate whose braking distance alone exceeds it; the quiet PI only
// where the error and the rate are both well inside (hysteresis, so the two never chatter). The PI
// starts from the estimates, so the handover is bumpless.
void ClockSteering::chooseLaw()
{
    const double size = std::abs (estimatedErrorNs);
    const double rate = std::abs (frequency - scarlettPpm);
    const double brakeRate = std::sqrt (2.0 * cfg.maxSlewPpmPerSecond * cfg.lockThresholdNs / nsPerUs);
    if (! braking && (size > cfg.lockThresholdNs || rate > brakeRate))
    {
        braking = true;
        return;
    }
    if (braking && size < 0.5 * cfg.lockThresholdNs && rate < 0.5 * brakeRate)
    {
        braking = false;
        integral = scarlettPpm;
        smoothed = estimatedErrorNs;
    }
}

// The time-optimal law for a rate limit: the rate error allowed is the one that `a` can still brake to
// zero before the error is gone, sqrt(2 a |e|) (us, ppm = us/s, ppm/s = us/s^2). Near zero it is linear
// with the acquiring PI's gain, matched in value and slope, so it does not chatter on the noise.
double ClockSteering::brakingFrequency (double dtSeconds) const
{
    const double a = cfg.brakingShare * cfg.maxSlewPpmPerSecond;
    const double k = gainFor (cfg.acquireTimeConstantSeconds, true);
    const double errorUs = estimatedErrorNs / nsPerUs;
    const double size = std::abs (errorUs);
    const double linearUs = a / (k * k);
    const double allowed = size <= linearUs ? k * size : std::sqrt (2.0 * a * (size - 0.5 * linearUs));
    const double rate = -std::copysign (std::min (allowed, cfg.maxRateErrorPpm), errorUs);
    return slewed (scarlettPpm + rate, dtSeconds);
}

void ClockSteering::judge (double errorNs)
{
    const double size = std::abs (errorNs);
    inThreshold = size < cfg.lockThresholdNs ? inThreshold + 1 : 0;
    if (state == Health::locked && size > 2.0 * cfg.lockThresholdNs)
    {
        state = Health::acquiring;
        why = "lock lost: steering back";
        ++count.lockLosses;
    }
    if (state != Health::locked && inThreshold >= cfg.lockUpdates && ! verifyingWarm)   // a warm start is checked first
    {
        state = Health::locked;
        why = "the PTP clock follows the Scarlett";
        lockedOnce = true;
    }
}

Decision ClockSteering::update (double nowSeconds, std::optional<Measurement> measurement)
{
    ++count.updates;
    if (state == Health::fault)
        return hold();
    // A warm start is written at once, with or without a measurement.
    if (pendingWarm)
    {
        verifyingWarm = true;
        rate.clear();
        return jumpTo (*std::exchange (pendingWarm, std::nullopt), "warm start: the last locked frequency, anchoring the phase");
    }
    if (! measurement)
        return noMeasurement (nowSeconds);

    // The loop's step is the time since the last measurement, at most one update: a long gap must
    // not become one big push, nor a frequency jump larger than one update's slew.
    const double sinceLast = std::max (nowSeconds - lastMeasurementAt, 1e-3);
    const double gap = std::min (sinceLast, cfg.updateSeconds);
    lastMeasurementAt = nowSeconds;
    if (state == Health::holdover)
    {
        state = Health::acquiring;
        why = "measuring again: steering back";
    }
    if (stage == Stage::measuringRate)
        return measureRate (nowSeconds, *measurement);
    const bool first = ! anchored;
    error = anchoredError (*measurement, sinceLast);
    if (first)
    {
        why = "anchored: steering towards the lock";
        estimatedErrorNs = 0.0;
        scarlettPpm = frequency;   // what the acquisition measured (or found at the rate)
        braking = false;
    }

    const double size = std::abs (error);
    if (lockedOnce && size > cfg.faultNs)
    {
        writeFailed ("the phase error ran away after a lock: steering stopped, the last frequency is held");
        return hold();
    }
    if (! lockedOnce && size > cfg.acquireLimitNs)
    {
        writeFailed ("could not acquire inside the frequency range: steering stopped, the last frequency is held");
        return hold();
    }
    if (! first)
        observe (error, sinceLast);   // the real time since the last measurement: the PHC ran all of it

    if (verifyingWarm && ! first)
    {
        const auto corrected = warmCorrection (nowSeconds);
        if (verifyingWarm)
        {
            // Still measuring the warm start's residual: the PI waits, so it does not hide it.
            judge (error);
            auto decision = hold();
            decision.health = state;
            decision.reason = why;
            return decision;
        }
        if (corrected)
        {
            auto decision = hold();
            frequency = integral = scarlettPpm = *corrected;   // the anchor and the error stay: the PI goes on from here
            decision.write = *corrected;
            decision.jump = true;
            decision.frequencyPpm = *corrected;
            ++count.corrections;
            judge (error);
            decision.health = state;
            decision.reason = why;
            return decision;
        }
    }

    auto decision = hold();
    if (! first)
    {
        chooseLaw();
        const double next = braking ? brakingFrequency (gap) : nextFrequency (error, gap);
        if (next != frequency)
        {
            frequency = next;
            decision.write = next;
            ++count.corrections;
        }
    }
    judge (error);
    decision.health = state;
    decision.frequencyPpm = frequency;
    decision.reason = why;
    return decision;
}
}
