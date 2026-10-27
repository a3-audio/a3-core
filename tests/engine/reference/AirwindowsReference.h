// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>

namespace a3ref
{
// One upstream plugin, stereo in place. Parameters by the plugin's own index.
class Reference
{
public:
    virtual ~Reference() = default;
    virtual void setParameter (int index, float value) = 0;
    virtual void process (float* const* io, int n) = 0;   // in place
};

std::unique_ptr<Reference> makePurestGain (float sampleRate);
std::unique_ptr<Reference> makeSmoothEQ3 (float sampleRate);
std::unique_ptr<Reference> makeIsolator3 (float sampleRate);
}
