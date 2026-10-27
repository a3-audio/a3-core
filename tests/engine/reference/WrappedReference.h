// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only: wraps one unchanged upstream Airwindows plugin (MIT, (c) 2018
// Chris Johnson) as an in-place stereo Reference. Each plugin header defines
// the same file-scope names (kUniqueId, ...), so each plugin is wrapped in a
// translation unit of its own (see the *Reference.cpp files).
#pragma once
#include "AirwindowsReference.h"
#include <vector>

namespace a3ref
{
template <typename Plugin>
class Wrapped final : public Reference
{
public:
    explicit Wrapped (float sampleRate) { plugin.setSampleRate (sampleRate); }
    void setParameter (int index, float value) override { plugin.setParameter (index, value); }
    void process (float* const* io, int n) override
    {
        scratchL.assign (io[0], io[0] + n);
        scratchR.assign (io[1], io[1] + n);
        float* in[2] { scratchL.data(), scratchR.data() };
        plugin.processReplacing (in, const_cast<float**> (io), n);
    }
private:
    Plugin plugin { nullptr };
    std::vector<float> scratchL, scratchR;   // test-only: allocation is fine here
};
}
