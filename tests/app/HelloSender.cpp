// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <vector>
#include <gtest/gtest.h>
#include "AppTruth.h"
#include "HelloSender.h"

namespace
{
struct Sent { double at; juce::OSCMessage message; };
}

TEST (HelloSender, EveryTwoSecondsWithOneBootId)
{
    std::vector<Sent> sent;
    double now = 0.0;
    a3app::HelloSender hello (a3apptest::engineTruth(), [&] (const juce::OSCMessage& m) { sent.push_back ({ now, m }); });
    EXPECT_EQ (hello.bootId().length(), 32) << hello.bootId();   // juce::Uuid, 32 hex digits

    for (int tick = 0; tick < 500; ++tick)   // 50 Hz for 10 s
    {
        now = tick * 0.02;
        hello.tick (now);
    }

    ASSERT_EQ (sent.size(), 5u);
    EXPECT_EQ (sent[0].at, 0.0) << "the first tick says hello at once";
    for (size_t i = 0; i < sent.size(); ++i)
    {
        const auto& m = sent[i].message;
        EXPECT_EQ (m.getAddressPattern().toString(), "/engine/hello");
        ASSERT_EQ (m.size(), 1);
        ASSERT_TRUE (m[0].isString());
        EXPECT_EQ (m[0].getString(), hello.bootId());
        if (i > 0)
        {
            EXPECT_GE (sent[i].at - sent[i - 1].at, 2.0 - 1e-9);
            EXPECT_LE (sent[i].at - sent[i - 1].at, 2.0 + 0.02 + 1e-9);
        }
    }
}

TEST (HelloSender, NewSenderNewBootId)
{
    const auto truth = a3apptest::engineTruth();
    a3app::HelloSender first (truth, [] (const juce::OSCMessage&) {});
    a3app::HelloSender second (truth, [] (const juce::OSCMessage&) {});
    EXPECT_FALSE (first.bootId().isEmpty());
    EXPECT_NE (first.bootId(), second.bootId());
    EXPECT_EQ (first.bootId(), first.bootId());
}
