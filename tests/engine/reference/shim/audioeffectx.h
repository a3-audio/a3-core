// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A minimal stand-in for the VST2 SDK's audioeffectx.h, so the unchanged Airwindows sources compile in the test runner.
#pragma once
#define __audioeffect__
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <set>
#include <string>

typedef int32_t VstInt32;
typedef void* audioMasterCallback;
enum VstPlugCategory { kPlugCategEffect };
enum { kVstMaxProgNameLen = 24, kVstMaxParamStrLen = 8, kVstMaxProductStrLen = 64, kVstMaxVendorStrLen = 64 };

inline void vst_strncpy(char* d, const char* s, size_t n) { std::strncpy(d, s, n); d[n] = 0; }
inline void float2string(float v, char* t, size_t n) { std::snprintf(t, n + 1, "%f", v); }

class AudioEffect {};
class AudioEffectX : public AudioEffect {
public:
    AudioEffectX(audioMasterCallback, int, int) {}
    virtual ~AudioEffectX() = default;
    // The VST2 SDK returns float here; keep that, it is what the plugin sees.
    float getSampleRate() { return sampleRate; }
    void setSampleRate(float s) { sampleRate = s; }
    void setNumInputs(int) {}
    void setNumOutputs(int) {}
    void setUniqueID(unsigned long) {}
    void canProcessReplacing() {}
    void canDoubleReplacing() {}
    void programsAreChunks(bool) {}
private:
    float sampleRate = 44100.0f;
};
