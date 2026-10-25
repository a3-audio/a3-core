// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Null tests: our compiled IEM nodes against the IEM VST3s installed on this
// machine, same state, same noise, per-channel max |difference|. A difference is
// a finding to report, never a tolerance to loosen. Sub outputs are compared
// unscaled: IEM 1.15 and 1.16 both multiply the discrete sub by sqrt(nChOut).
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>
#include "IemNode.h"
#include "Layout.h"

namespace
{
constexpr double sampleRate = 44100.0;
constexpr int blockSize = 64;
constexpr int seconds = 2;
constexpr float noiseLevel = 0.251189f;   // -12 dBFS peak
constexpr double tolerance = 1e-5;
constexpr int busChannels = 16;
constexpr int encoderInputs = 8;
constexpr int iemOrderThree = 4;          // IEM's choice value for 3rd order (0 = auto)

using Channels = std::vector<std::vector<float>>;

juce::var readJson (const juce::File& file)
{
    juce::var parsed;
    EXPECT_TRUE (juce::JSON::parse (file.loadFileAsString(), parsed).wasOk()) << file.getFullPathName();
    return parsed;
}

// The files hold "auto" (0) for the order; the engine sets 3rd order explicitly.
juce::var withOrder (const juce::var& parameters, const char* orderId)
{
    auto copy = parameters.clone();
    copy.getDynamicObject()->setProperty (orderId, iemOrderThree);
    return copy;
}

Channels noise (int channels, int frames)
{
    std::mt19937 rng (12345);
    std::uniform_real_distribution<float> dist (-noiseLevel, noiseLevel);
    Channels data ((size_t) channels, std::vector<float> ((size_t) frames));
    for (auto& channel : data)
        for (auto& sample : channel)
            sample = dist (rng);
    return data;
}

struct Vst3Instance
{
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::String version, skipReason;
};

// The VST3 host wrapper refuses discrete channel sets, so the plug-in is given the
// narrowest named speaker set that is at least as wide (there is none of 9, which
// booth 6.1 needs: it gets 7.1.2 and only its first 9 channels are compared).
// IEM's plug-ins treat the channels alike.
juce::AudioChannelSet vst3ChannelSetWithWidth (int channels)
{
    using Set = juce::AudioChannelSet;
    for (const auto& set : { Set::mono(), Set::stereo(), Set::quadraphonic(), Set::create5point0(), Set::create5point1(),
                             Set::create6point1(), Set::create7point1(), Set::create7point1point2() })
        if (set.size() >= channels)
            return set;
    return Set::discreteChannels (channels);
}

Vst3Instance loadVst3 (const juce::String& name, const juce::AudioProcessor::BusesLayout& layout)
{
    Vst3Instance result;
    const auto path = "/usr/lib/vst3/IEM/" + name + ".vst3";
    if (! juce::File (path).exists())
    {
        result.skipReason = path + " is not installed";
        return result;
    }
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile (descriptions, path);
    if (descriptions.isEmpty())
    {
        result.skipReason = path + " lists no plug-in";
        return result;
    }
    juce::String error;
    result.plugin = format.createInstanceFromDescription (*descriptions[0], sampleRate, blockSize, error);
    if (result.plugin == nullptr)
    {
        result.skipReason = "createInstanceFromDescription failed: " + error;
        return result;
    }
    result.version = descriptions[0]->version;
    if (! result.plugin->setBusesLayout (layout))
    {
        result.skipReason = name + " refuses the bus layout";
        result.plugin.reset();
    }
    return result;
}

// A hosted VST3 takes its state wrapped as <VST3PluginState><IComponent>base64</IComponent>:
// the IEM plug-in's own state (which is ours) is the component's stream.
juce::MemoryBlock vst3HostState (const juce::MemoryBlock& pluginState)
{
    juce::XmlElement wrapper ("VST3PluginState");
    wrapper.createNewChildElement ("IComponent")->addTextElement (pluginState.toBase64Encoding());
    juce::MemoryBlock wrapped;
    juce::AudioProcessor::copyXmlToBinary (wrapper, wrapped);
    return wrapped;
}

// The plug-in's own state inside a hosted VST3's wrapped state (see vst3HostState).
std::unique_ptr<juce::XmlElement> pluginStateOf (const juce::MemoryBlock& hostState)
{
    const auto wrapper = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
    if (wrapper == nullptr || wrapper->getChildByName ("IComponent") == nullptr)
        return nullptr;
    juce::MemoryBlock inner;
    inner.fromBase64Encoding (wrapper->getChildByName ("IComponent")->getAllSubText());
    return juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
}

// Every parameter and the decoder config the VST3 reports back must be what we sent.
void expectVst3TookTheState (const juce::MemoryBlock& ours, juce::AudioProcessor& vst3, const juce::String& label)
{
    juce::MemoryBlock theirsHost;
    vst3.getStateInformation (theirsHost);
    const auto theirs = pluginStateOf (theirsHost);
    const auto sent = juce::AudioProcessor::getXmlFromBinary (ours.getData(), (int) ours.getSize());
    ASSERT_TRUE (sent != nullptr);
    ASSERT_TRUE (theirs != nullptr) << label << ": the VST3 returned no readable state (state rejected?)";

    int compared = 0;
    for (auto* param : sent->getChildWithTagNameIterator ("PARAM"))
    {
        const auto id = param->getStringAttribute ("id");
        const auto* back = theirs->getChildByAttribute ("id", id);
        ASSERT_TRUE (back != nullptr) << label << ": " << id << " missing in the VST3 state";
        EXPECT_NEAR (back->getDoubleAttribute ("value"), param->getDoubleAttribute ("value"), 1e-6) << label << ": " << id;
        ++compared;
    }
    EXPECT_GT (compared, 0) << label << ": our state holds no parameters";
    EXPECT_EQ (theirs->getStringAttribute ("configString"), sent->getStringAttribute ("configString")) << label;
}

// Runs a processor over `input` in blocks of blockSize; io has max(in, out) channels.
Channels render (juce::AudioProcessor& processor, const Channels& input, int outChannels)
{
    const int frames = (int) input[0].size();
    const int ioChannels = std::max (busChannels, outChannels);
    Channels out ((size_t) outChannels, std::vector<float> ((size_t) frames));
    juce::AudioBuffer<float> io (ioChannels, blockSize);
    juce::MidiBuffer midi;
    for (int start = 0; start < frames; start += blockSize)
    {
        io.clear();
        for (size_t c = 0; c < input.size(); ++c)
            io.copyFrom ((int) c, 0, input[c].data() + start, blockSize);
        processor.processBlock (io, midi);
        for (int c = 0; c < outChannels; ++c)
            std::copy_n (io.getReadPointer (c), blockSize, out[(size_t) c].begin() + start);
    }
    return out;
}

double rmsOf (const std::vector<float>& x)
{
    double sum = 0.0;
    for (float v : x)
        sum += (double) v * v;
    return std::sqrt (sum / (double) x.size());
}

// A null of two silent signals proves nothing. The input is -12 dBFS noise; the quietest
// real channel measured is the sub at about 1.6e-2 RMS, so 1e-3 is 16x below it and
// 1e4x above a silent channel's float noise.
constexpr double silenceFloorRms = 1e-3;

// Prints one line per output channel; returns the worst max |difference|.
double reportDifference (const juce::String& label, const Channels& ours, const Channels& theirs,
                         const std::vector<juce::String>& roles)
{
    double worst = 0.0;
    for (size_t c = 0; c < ours.size(); ++c)
    {
        double maxDiff = 0.0, diffSquares = 0.0;
        for (size_t i = 0; i < ours[c].size(); ++i)
        {
            const double d = (double) ours[c][i] - (double) theirs[c][i];
            maxDiff = std::max (maxDiff, std::abs (d));
            diffSquares += d * d;
        }
        const double diffRms = std::sqrt (diffSquares / (double) ours[c].size());
        const double channelRms = rmsOf (theirs[c]);
        if (roles[c] != "unused")
        {
            EXPECT_GT (channelRms, silenceFloorRms) << label << " out " << c + 1 << ": the VST3 is silent";
            EXPECT_GT (rmsOf (ours[c]), silenceFloorRms) << label << " out " << c + 1 << ": our node is silent";
        }
        const double relativeDb = channelRms > 0.0 ? 20.0 * std::log10 (std::max (diffRms, 1e-30) / channelRms) : 0.0;
        std::printf ("[null] %s out %zu (%s): max|diff| = %.3e  diff %.1f dB re channel RMS (%.3e)  %s\n",
                     label.toRawUTF8(), c + 1, roles[c].toRawUTF8(), maxDiff, relativeDb, channelRms,
                     maxDiff <= tolerance ? "PASS" : "FAIL");
        worst = std::max (worst, maxDiff);
    }
    return worst;
}

std::vector<juce::String> numbered (int count)
{
    std::vector<juce::String> roles;
    for (int c = 1; c <= count; ++c)
        roles.push_back ("ch" + juce::String (c));
    return roles;
}

// Both processors get our node's state; the VST3 is then a mirror of the node.
void compareToVst3 (const juce::String& label, const juce::String& vst3Name, a3::IemNode& node,
                    const juce::AudioProcessor::BusesLayout& layout, int inputChannels, int outputChannels,
                    const std::vector<juce::String>& roles)
{
    auto vst3 = loadVst3 (vst3Name, layout);
    if (vst3.plugin == nullptr)
        GTEST_SKIP() << vst3.skipReason;
    std::printf ("[null] %s: installed %s version %s\n", label.toRawUTF8(), vst3Name.toRawUTF8(), vst3.version.toRawUTF8());

    const auto state = vst3HostState (node.state());
    vst3.plugin->setStateInformation (state.getData(), (int) state.getSize());
    expectVst3TookTheState (node.state(), *vst3.plugin, label);
    node.prepare (sampleRate, blockSize);
    vst3.plugin->prepareToPlay (sampleRate, blockSize);

    // Our node ran one silent block in prepare(); IEM interpolates from the previous
    // block, so the VST3 needs the same block or its first real block ramps from its defaults.
    render (*vst3.plugin, Channels ((size_t) inputChannels, std::vector<float> ((size_t) blockSize, 0.0f)), outputChannels);

    const auto input = noise (inputChannels, (int) sampleRate * seconds / blockSize * blockSize);
    const auto theirs = render (*vst3.plugin, input, outputChannels);
    const auto ours = render (node.processor(), input, outputChannels);
    EXPECT_LE (reportDifference (label, ours, theirs, roles), tolerance);
}

juce::AudioProcessor::BusesLayout vst3DecoderLayout (int outputs)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::ambisonic (3));
    layout.outputBuses.add (vst3ChannelSetWithWidth (outputs));
    return layout;
}

void nullSimpleDecoder (const juce::String& label, const juce::File& dataDirectory, const juce::String& zone)
{
    const auto decoderJson = dataDirectory.getChildFile (zone + ".decoder.json").loadFileAsString();
    const auto parameters = withOrder (readJson (dataDirectory.getChildFile (zone + ".parameters.json")),
                                       "inputOrderSetting");
    juce::String error;
    const auto layout = a3::parseLayout (decoderJson, parameters, error);
    ASSERT_TRUE (layout.has_value()) << error;

    const int outputs = layout->numOutputs();
    auto node = a3::IemNode::create (a3::IemKind::simpleDecoder, busChannels, outputs);
    ASSERT_TRUE (node != nullptr);
    ASSERT_TRUE (node->setParameters (parameters).wasOk());
    ASSERT_TRUE (node->loadDecoderConfig (decoderJson).wasOk());

    std::vector<juce::String> roles ((size_t) outputs, "unused");
    for (size_t k = 0; k < layout->topChannels.size(); ++k)
        roles[(size_t) layout->topChannels[k] - 1] = "top " + juce::String ((int) k + 1);
    if (layout->hasSub())
        roles[(size_t) layout->subChannel - 1] = "sub";
    compareToVst3 (label, "SimpleDecoder", *node, vst3DecoderLayout (outputs), busChannels, outputs, roles);
}

class IemNull : public ::testing::Test
{
protected:
    // The VST3 hosting code wants a message manager, as any JUCE host does.
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
};
}

TEST_F (IemNull, MultiEncoderMatchesTheVst3)
{
    const juce::File data (A3_SPATIAL_DATA);
    auto node = a3::IemNode::create (a3::IemKind::multiEncoder, encoderInputs, busChannels);
    ASSERT_TRUE (node != nullptr);
    ASSERT_TRUE (node->setParameters (withOrder (readJson (data.getChildFile ("encoder.parameters.json")),
                                                 "orderSetting")).wasOk());
    const float azimuths[] = { 30.0f, -60.0f, 120.0f, -150.0f };
    const float elevations[] = { 0.0f, 20.0f, -10.0f, 45.0f };
    for (int source = 0; source < 4; ++source)
    {
        ASSERT_TRUE (node->setParameter ("azimuth" + juce::String (source), azimuths[source]).wasOk());
        ASSERT_TRUE (node->setParameter ("elevation" + juce::String (source), elevations[source]).wasOk());
    }

    juce::AudioProcessor::BusesLayout layout;   // the VST3's; our node was built with discrete(8) in
    layout.inputBuses.add (vst3ChannelSetWithWidth (encoderInputs));
    layout.outputBuses.add (juce::AudioChannelSet::ambisonic (3));
    compareToVst3 ("MultiEncoder", "MultiEncoder", *node, layout, encoderInputs, busChannels, numbered (busChannels));
}

TEST_F (IemNull, SimpleDecoderMainMatchesTheVst3)
{
    nullSimpleDecoder ("SimpleDecoder main", juce::File (A3_SPATIAL_DATA), "main");
}

TEST_F (IemNull, SimpleDecoderBoothMatchesTheVst3)
{
    nullSimpleDecoder ("SimpleDecoder booth 4.1", juce::File (A3_SPATIAL_DATA), "booth");
}

TEST_F (IemNull, SimpleDecoderBoothReaper61MatchesTheVst3)
{
    nullSimpleDecoder ("SimpleDecoder booth 6.1", juce::File (A3_SPATIAL_REAPER_BOOTH_DATA), "booth");
}

TEST_F (IemNull, BinauralDecoderMatchesTheVst3)
{
    auto node = a3::IemNode::create (a3::IemKind::binauralDecoder, busChannels, 2);
    ASSERT_TRUE (node != nullptr);
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::ambisonic (3));
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    compareToVst3 ("BinauralDecoder", "BinauralDecoder", *node, layout, busChannels, 2, { "left", "right" });
}

TEST_F (IemNull, StereoEncoderMatchesTheVst3)
{
    auto node = a3::IemNode::create (a3::IemKind::stereoEncoder, 2, busChannels);
    ASSERT_TRUE (node != nullptr);
    // Set notifying, so the pose is in the state the VST3 is given.
    ASSERT_TRUE (node->setParameters (juce::JSON::parse (R"({"azimuth":30,"elevation":10,"width":60})")).wasOk());

    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::ambisonic (3));
    compareToVst3 ("StereoEncoder", "StereoEncoder", *node, layout, 2, busChannels, numbered (busChannels));
}
