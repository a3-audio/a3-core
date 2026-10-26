// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the tests of state.json, evening.json and the recall: what tools/parity/record_state.py
// recorded from B0 (a3-core f3345dc7feeb), and the files it recorded them from, written to disk and
// read the way Core reads them.
#pragma once

#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

#include "Channels.h"
#include "CoreState.h"
#include "Json.h"
#include "OscWire.h"
#include "StateFile.h"

namespace recorded
{
inline juce::var load (const char* name)
{
    juce::String error;
    auto value = a3core::parseJson (juce::File (A3_CORE_TEST_DATA "/parity/state").getChildFile (name).loadFileAsString(), error);
    EXPECT_EQ (error, juce::String()) << name;
    return value;
}

inline std::string text (const juce::var& value) { return value.toString().toStdString(); }

// A scratch directory that is gone when the test is.
struct TempDir
{
    TempDir()
    {
        dir = juce::File::createTempFile ("a3-core-state-test");
        dir.createDirectory();
    }
    ~TempDir() { dir.deleteRecursively(); }
    juce::File dir;
};

inline void writeBytes (const juce::File& file, const std::string& bytes)
{
    file.getParentDirectory().createDirectory();
    ASSERT_TRUE (file.replaceWithData (bytes.data(), bytes.size()));
}

inline std::string readBytes (const juce::File& file)
{
    juce::MemoryBlock block;
    file.loadFileAsData (block);
    return std::string (static_cast<const char*> (block.getData()), block.getSize());
}

// The text of a file as StateFile::load makes of it.
inline juce::var loadedFrom (const std::string& fileText)
{
    TempDir temp;
    const auto file = temp.dir.getChildFile ("file.json");
    writeBytes (file, fileText);
    return a3core::StateFile (file).load();
}

struct Rig
{
    std::vector<a3core::ChannelInfo> channels;
    a3core::MasterInfo master;
};

inline Rig aRig (int channels = 4) { return { std::vector<a3core::ChannelInfo> (static_cast<size_t> (channels)), {} }; }

// What a rig looks like after the file's text was read into it.
inline Rig rigFrom (const std::string& fileText)
{
    auto rig = aRig();
    a3core::applyState (loadedFrom (fileText), rig.channels, rig.master);
    return rig;
}

// ["float", 0.5] as the recorder wrote a Python value: the OscArg a Python value becomes.
inline a3core::OscArg expectedArg (const juce::String& type, const juce::var& value)
{
    if (type == "float") return a3core::pyFloat (value);
    if (type == "int") return a3core::pyInt (static_cast<juce::int64> (value));
    if (type == "str") return a3core::pyStr (value.toString().toStdString());
    if (type == "bool") return a3core::pyBool (static_cast<bool> (value));
    if (type == "NoneType") return { 'N', std::monostate {} };
    ADD_FAILURE() << "unexpected type " << type;
    return {};
}
}
