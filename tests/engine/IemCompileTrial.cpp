// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "IemNode.h"
#include "TestSignals.h"

// The 2026-09-21 trial: compile, create, one block. IEM's SH are unit-W; SN3D X at az 0, el 0 is 1.
TEST (IemCompileTrial, MultiEncoderPutsOneSourceAtTheFrontOnWAndX)
{
    auto enc = a3::IemNode::create (a3::IemKind::multiEncoder, 1, 16);
    ASSERT_TRUE (enc->setParameters (juce::JSON::parse (R"({"inputSetting":1,"orderSetting":4,"useSN3D":1,
        "azimuth0":0,"elevation0":0,"gain0":0})")).wasOk());
    enc->prepare (44100.0, 64);
    const auto in = a3test::sine (64 * 3, 1000.0, 44100.0, 0.5f);
    juce::AudioBuffer<float> io (16, 64);
    for (int b = 0; b < 3; ++b)                  // block 1 ramps from silence (addFromWithRamp)
    {
        io.clear();
        io.copyFrom (0, 0, in.l.data() + b * 64, 64);
        enc->process (io);
    }
    for (int i = 0; i < 64; ++i)
    {
        const float x = in.l[(size_t) (128 + i)];
        EXPECT_NEAR (io.getSample (0, i), x, 1e-6f);   // W
        EXPECT_NEAR (io.getSample (3, i), x, 1e-6f);   // X
        EXPECT_NEAR (io.getSample (1, i), 0.0f, 1e-6f); // Y
        EXPECT_NEAR (io.getSample (2, i), 0.0f, 1e-6f); // Z
    }
}
