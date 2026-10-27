// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>

namespace a3bench
{
enum class Shape { off, sine, pink, sweep };

// A test signal for the bench's gen_L/gen_R outputs (the same signal on both).
// set() before the stream runs, or from the thread that calls render(); render()
// allocates nothing.
// "sine:1000:-20", "pink:-20", "sweep:-20" (level in dBFS: peak for sine and sweep, RMS for pink).
// False for anything else, leaving the outputs untouched.
struct GenSpec { Shape shape = Shape::off; double hz = 0.0; float levelDb = 0.0f; };
bool parseGenSpec (const std::string& text, GenSpec& out);

class Generator
{
public:
    void prepare (double sampleRate);
    void set (Shape newShape, double hz, float levelDb);
    void render (float* l, float* r, int n);

    static constexpr double sweepSeconds = 10.0, sweepFromHz = 20.0, sweepToHz = 20000.0;

private:
    float nextPink();

    double sampleRate = 48000.0, phase = 0.0, hz = 1000.0, sweepPosition = 0.0;
    float amplitude = 0.0f;
    Shape shape = Shape::off;
    uint32_t rng = 0x12345678u;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
};
}
