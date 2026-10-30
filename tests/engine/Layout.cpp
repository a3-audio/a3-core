// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include "IemNode.h"
#include "SpatialConfig.h"
#include "SpatialTestData.h"
#include "TestSignals.h"

namespace
{
using Channels = std::vector<std::vector<float>>;

const juce::File& shippedDirectory = a3test::shippedSpatialDirectory;
const juce::File& reaperBoothDirectory = a3test::reaperBoothSpatialDirectory;
using a3test::ScratchDirectory;

juce::String shippedFile (const char* name) { return shippedDirectory.getChildFile (name).loadFileAsString(); }

juce::var parseJson (const juce::String& text)
{
    return juce::JSON::parse (text);
}

juce::var shippedMainParameters() { return parseJson (shippedFile ("main.parameters.json")); }

// The shipped main decoder with one edit applied to its parsed form.
juce::String editedMainDecoder (const std::function<void (juce::DynamicObject&)>& edit)
{
    auto parsed = parseJson (shippedFile ("main.decoder.json"));
    edit (*parsed.getProperty ("Decoder", {}).getDynamicObject());
    return juce::JSON::toString (parsed);
}

juce::var parametersWith (const char* id, double value)
{
    auto parameters = shippedMainParameters().clone();
    parameters.getDynamicObject()->setProperty (id, value);
    return parameters;
}

juce::var routing (std::initializer_list<int> channels)
{
    juce::Array<juce::var> array;
    for (int channel : channels)
        array.add (channel);
    return array;
}

std::optional<a3::Layout> parseMain (const juce::String& decoder, const juce::var& parameters, juce::String& error)
{
    return a3::parseLayout (decoder, parameters, error);
}
}

TEST (Layout, TheShippedMainIsSubOnOneAndFourTops)
{
    juce::String error;
    const auto layout = parseMain (shippedFile ("main.decoder.json"), shippedMainParameters(), error);
    ASSERT_TRUE (layout.has_value()) << error;
    EXPECT_EQ (layout->subChannel, 1);
    EXPECT_EQ (layout->topChannels, (std::vector<int> { 2, 3, 4, 5 }));
    EXPECT_EQ (layout->numOutputs(), 5);
}

TEST (Layout, TheShippedBoothIsSubOnOneAndFourTops)
{
    juce::String error;
    const auto layout = parseMain (shippedFile ("booth.decoder.json"), parseJson (shippedFile ("booth.parameters.json")), error);
    ASSERT_TRUE (layout.has_value()) << error;
    EXPECT_EQ (layout->subChannel, 1);
    EXPECT_EQ (layout->topChannels, (std::vector<int> { 2, 3, 4, 5 }));
}

TEST (Layout, ARoutingGapIsAllowed)
{
    // REAPER's own booth: six tops and a gap at 7 and 8.
    const auto decoder = editedMainDecoder ([] (juce::DynamicObject& d)
    {
        auto matrix = d.getProperty ("Matrix");
        for (int extra = 0; extra < 2; ++extra)
            matrix.getArray()->add (matrix[0]);
        d.setProperty ("Matrix", matrix);
        d.setProperty ("Routing", routing ({ 2, 3, 4, 5, 6, 9 }));
    });
    juce::String error;
    const auto layout = parseMain (decoder, shippedMainParameters(), error);
    ASSERT_TRUE (layout.has_value()) << error;
    EXPECT_EQ (layout->topChannels, (std::vector<int> { 2, 3, 4, 5, 6, 9 }));
    EXPECT_EQ (layout->numOutputs(), 9);
}

TEST (Layout, DuplicateRoutingIsRefused)
{
    const auto decoder = editedMainDecoder ([] (juce::DynamicObject& d) { d.setProperty ("Routing", routing ({ 2, 3, 3, 5 })); });
    juce::String error;
    EXPECT_FALSE (parseMain (decoder, shippedMainParameters(), error).has_value());
    EXPECT_TRUE (error.contains ("twice")) << error;
}

TEST (Layout, ASubOnATopsChannelIsRefused)
{
    juce::String error;
    EXPECT_FALSE (parseMain (shippedFile ("main.decoder.json"), parametersWith ("swChannel", 3), error).has_value());
    EXPECT_TRUE (error.contains ("sub")) << error;
}

TEST (Layout, SwModeZeroMeansNoSub)
{
    juce::String error;
    const auto layout = parseMain (shippedFile ("main.decoder.json"), parametersWith ("swMode", 0), error);
    ASSERT_TRUE (layout.has_value()) << error;
    EXPECT_FALSE (layout->hasSub());
    EXPECT_EQ (layout->subChannel, 0);
    EXPECT_EQ (layout->numOutputs(), 5);
}

TEST (Layout, AnUnsupportedSubModeIsRefused)
{
    juce::String error;
    EXPECT_FALSE (parseMain (shippedFile ("main.decoder.json"), parametersWith ("swMode", 2), error).has_value());
    EXPECT_TRUE (error.contains ("swMode")) << error;
}

TEST (Layout, OrderOtherThanThreeIsRefused)
{
    const auto decoder = editedMainDecoder ([] (juce::DynamicObject& d)
    {
        juce::Array<juce::var> rows;
        for (const auto& row : *d.getProperty ("Matrix").getArray())
        {
            juce::Array<juce::var> shortRow;
            for (int i = 0; i < 9; ++i)
                shortRow.add (row[i]);
            rows.add (shortRow);
        }
        d.setProperty ("Matrix", rows);
    });
    juce::String error;
    EXPECT_FALSE (parseMain (decoder, shippedMainParameters(), error).has_value());
    EXPECT_TRUE (error.contains ("order 3")) << error;
}

// SpatialBus refuses decoders wider than 31 outputs (JUCE's preallocated channel
// table); the data rule refuses them first, so a file the loader accepts runs.
TEST (Layout, MoreOutputsThanTheEngineRunsAreRefused)
{
    const auto decoder = editedMainDecoder ([] (juce::DynamicObject& d) { d.setProperty ("Routing", routing ({ 2, 3, 4, 32 })); });
    juce::String error;
    EXPECT_FALSE (parseMain (decoder, shippedMainParameters(), error).has_value());
    EXPECT_TRUE (error.contains ("31")) << error;

    error = {};
    EXPECT_FALSE (parseMain (shippedFile ("main.decoder.json"), parametersWith ("swChannel", 32), error).has_value());
    EXPECT_TRUE (error.contains ("31")) << error;

    const auto widest = editedMainDecoder ([] (juce::DynamicObject& d) { d.setProperty ("Routing", routing ({ 2, 3, 4, 31 })); });
    EXPECT_TRUE (parseMain (widest, shippedMainParameters(), error).has_value()) << error;
}

TEST (Layout, BadJsonIsRefused)
{
    juce::String error;
    EXPECT_FALSE (parseMain ("{ not json", shippedMainParameters(), error).has_value());
    EXPECT_TRUE (error.contains ("JSON")) << error;
}

TEST (SpatialConfig, TheShippedDirectoryLoads)
{
    juce::String error;
    const auto config = a3::loadSpatialConfig (shippedDirectory, error);
    ASSERT_TRUE (config.has_value()) << error;
    EXPECT_EQ (config->main.layout.topChannels.size(), 4u);
    EXPECT_EQ (config->booth.layout.topChannels.size(), 4u);
    EXPECT_EQ (config->main.topDirections.size(), 4u);
    EXPECT_NEAR (config->main.fixedGainDb, -12.785921f, 1e-4f);
    EXPECT_NEAR (config->main.absorbedGainDb, -30.88f, 1e-4f);
    EXPECT_NEAR (config->booth.fixedGainDb, -0.1564f, 1e-4f);
    EXPECT_NEAR (config->booth.absorbedGainDb, -30.88f, 1e-4f);
}

TEST (SpatialConfig, TheReaperBoothVariantLoads)
{
    juce::String error;
    const auto config = a3::loadSpatialConfig (reaperBoothDirectory, error);
    ASSERT_TRUE (config.has_value()) << error;
    EXPECT_EQ (config->booth.layout.subChannel, 1);
    EXPECT_EQ (config->booth.layout.topChannels, (std::vector<int> { 2, 3, 4, 5, 6, 9 }));
    EXPECT_EQ (config->booth.topDirections.size(), 6u);
    EXPECT_EQ (config->main.layout.topChannels.size(), 4u);
}

TEST (SpatialConfig, TheVariantSharesEverythingButTheBoothWithTheDefault)
{
    int compared = 0;
    for (const auto& file : shippedDirectory.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        // fx: the engine's delay rotates (SPREAD, R5); the variant keeps REAPER's warp for the parity run.
        if (file.getFileName().startsWith ("booth.") || file.getFileName() == "fx.parameters.json" || file.getFileName() == "variant.json")
            continue;
        const auto twin = reaperBoothDirectory.getChildFile (file.getFileName());
        EXPECT_TRUE (twin.existsAsFile()) << file.getFileName();
        EXPECT_EQ (file.loadFileAsString(), twin.loadFileAsString()) << file.getFileName();
        ++compared;
    }
    EXPECT_GE (compared, 6);
    EXPECT_EQ (reaperBoothDirectory.findChildFiles (juce::File::findFiles, false, "*.json").size(),
               shippedDirectory.findChildFiles (juce::File::findFiles, false, "*.json").size());
}

TEST (SpatialConfig, AMissingFileIsRefusedByName)
{
    ScratchDirectory scratch;
    ASSERT_TRUE (scratch.root().getChildFile ("rec.parameters.json").deleteFile());
    juce::String error;
    EXPECT_FALSE (a3::loadSpatialConfig (scratch.root(), error).has_value());
    EXPECT_TRUE (error.contains ("rec.parameters.json")) << error;
}

TEST (SpatialConfig, RefusesMissingFxParameters)
{
    ScratchDirectory scratch;
    ASSERT_TRUE (scratch.root().getChildFile ("fx.parameters.json").deleteFile());
    juce::String error;
    EXPECT_FALSE (a3::loadSpatialConfig (scratch.root(), error).has_value());
    EXPECT_TRUE (error.contains ("fx.parameters.json")) << error;
}

TEST (SpatialConfig, RefusesMissingPhonesParameters)
{
    ScratchDirectory scratch;
    ASSERT_TRUE (scratch.root().getChildFile ("phones.parameters.json").deleteFile());
    juce::String error;
    EXPECT_FALSE (a3::loadSpatialConfig (scratch.root(), error).has_value());
    EXPECT_TRUE (error.contains ("phones.parameters.json")) << error;
}

TEST (SpatialConfig, FxAndPhonesCarryTheOrderExplicitly)
{
    juce::String error;
    const auto config = a3::loadSpatialConfig (shippedDirectory, error);
    ASSERT_TRUE (config.has_value()) << error;
    EXPECT_EQ ((float) (double) config->fxParameters.getProperty ("orderSetting", {}), a3::iemOrderThree);
    EXPECT_EQ ((float) (double) config->phonesParameters.getProperty ("inputOrderSetting", {}), a3::iemOrderThree);
}

TEST (SpatialConfig, AnUnknownStageKindIsRefused)
{
    ScratchDirectory scratch;
    const auto zone = scratch.root().getChildFile ("main.zone.json");
    ASSERT_TRUE (zone.replaceWithText (zone.loadFileAsString().replace ("\"fixed\"", "\"guessed\"")));
    juce::String error;
    EXPECT_FALSE (a3::loadSpatialConfig (scratch.root(), error).has_value());
    EXPECT_TRUE (error.contains ("main.zone.json") && error.contains ("guessed")) << error;
}

TEST (SpatialConfig, ATopDirectionCountThatDiffersFromTheRoutingIsRefused)
{
    ScratchDirectory scratch;
    const auto zone = scratch.root().getChildFile ("booth.zone.json");
    ASSERT_TRUE (zone.replaceWithText (zone.loadFileAsString().replace ("[135, 0]", "[135, 0], [0, 0]")));
    juce::String error;
    EXPECT_FALSE (a3::loadSpatialConfig (scratch.root(), error).has_value());
    EXPECT_TRUE (error.contains ("booth.zone.json")) << error;
}

TEST (SpatialConfig, AnUnknownParameterIdIsRefusedWhenAppliedToANode)
{
    ScratchDirectory scratch;
    const auto parameters = scratch.root().getChildFile ("main.parameters.json");
    ASSERT_TRUE (parameters.replaceWithText (parameters.loadFileAsString().replace ("\"weights\"", "\"weightz\"")));
    juce::String error;
    const auto config = a3::loadSpatialConfig (scratch.root(), error);
    ASSERT_TRUE (config.has_value()) << error;   // the loader knows the format, the node knows the ids
    auto decoder = a3::IemNode::create (a3::IemKind::simpleDecoder, 16, config->main.layout.numOutputs());
    ASSERT_TRUE (decoder != nullptr);
    const auto result = decoder->setParameters (config->main.parameters);
    EXPECT_TRUE (result.failed());
    EXPECT_TRUE (result.getErrorMessage().contains ("weightz")) << result.getErrorMessage();
}

TEST (SpatialConfig, TheAmbisonicOrderIsSetToThreeExplicitly)
{
    juce::String error;
    const auto config = a3::loadSpatialConfig (shippedDirectory, error);
    ASSERT_TRUE (config.has_value()) << error;
    EXPECT_EQ (shippedMainParameters().getProperty ("inputOrderSetting", {}), juce::var (0.0));   // the file keeps the template's "auto"

    auto decoder = a3::IemNode::create (a3::IemKind::simpleDecoder, 16, 5);
    ASSERT_TRUE (decoder->setParameters (config->main.parameters).wasOk());
    EXPECT_EQ (decoder->rawParameter ("inputOrderSetting")->load(), 4.0f);

    auto encoder = a3::IemNode::create (a3::IemKind::multiEncoder, 8, 16);
    ASSERT_TRUE (encoder->setParameters (config->encoderParameters).wasOk());
    EXPECT_EQ (encoder->rawParameter ("orderSetting")->load(), 4.0f);

    auto rec = a3::IemNode::create (a3::IemKind::binauralDecoder, 16, 2);
    ASSERT_TRUE (rec->setParameters (config->recParameters).wasOk());
    EXPECT_EQ (rec->rawParameter ("inputOrderSetting")->load(), 4.0f);
}

namespace
{
constexpr int busChannels = 16;
constexpr int blockSize = 256;
constexpr size_t settleFrames = 12000;   // IEM's smoothing and the 150 Hz filters have settled

struct ZoneCase
{
    const char* name;
    const char* directory;   // "default" or "reaper"
    bool booth;
    double sampleRate;
};

std::string caseName (const testing::TestParamInfo<ZoneCase>& info) { return info.param.name; }

class Zone : public testing::TestWithParam<ZoneCase>
{
protected:
    void SetUp() override
    {
        const auto directory = std::string (GetParam().directory) == "reaper" ? reaperBoothDirectory : shippedDirectory;
        std::optional<a3::SpatialConfig> loaded = a3::loadSpatialConfig (directory, error);
        ASSERT_TRUE (loaded.has_value()) << error;
        config = *loaded;
        zone = GetParam().booth ? &config.booth : &config.main;

        encoder = a3::IemNode::create (a3::IemKind::multiEncoder, 1, busChannels);
        decoder = a3::IemNode::create (a3::IemKind::simpleDecoder, busChannels, zone->layout.numOutputs());
        ASSERT_TRUE (encoder != nullptr && decoder != nullptr);
        ASSERT_TRUE (encoder->setParameters (config.encoderParameters).wasOk());
        ASSERT_TRUE (encoder->setParameter ("inputSetting", 1.0f).wasOk());
        ASSERT_TRUE (decoder->setParameters (zone->parameters).wasOk());
        ASSERT_TRUE (decoder->loadDecoderConfig (zone->layout.decoderJson).wasOk());
        encoder->prepare (GetParam().sampleRate, blockSize);
        decoder->prepare (GetParam().sampleRate, blockSize);
    }

    void pointSourceAt (float azimuth, float elevation)
    {
        ASSERT_TRUE (encoder->setParameter ("azimuth0", azimuth).wasOk());
        ASSERT_TRUE (encoder->setParameter ("elevation0", elevation).wasOk());
    }

    // One mono source through encoder and decoder; returns the decoder's outputs.
    Channels render (const std::vector<float>& source)
    {
        const int outputs = zone->layout.numOutputs();
        Channels out ((size_t) outputs, std::vector<float> (source.size(), 0.0f));
        juce::AudioBuffer<float> io (busChannels, blockSize);
        for (size_t start = 0; start < source.size(); start += blockSize)
        {
            const int n = (int) std::min ((size_t) blockSize, source.size() - start);
            io.clear();
            io.copyFrom (0, 0, source.data() + start, n);
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), busChannels, 0, n);
            encoder->process (view);
            decoder->process (view);
            for (int c = 0; c < outputs; ++c)
                std::copy_n (io.getReadPointer (c), n, out[(size_t) c].begin() + (long) start);
        }
        return out;
    }

    static double rmsDb (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (size_t i = settleFrames; i < x.size(); ++i)
            sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (sum / (double) (x.size() - settleFrames) + 1e-30);
    }

    juce::String error;
    a3::SpatialConfig config;
    const a3::ZoneConfig* zone = nullptr;
    std::unique_ptr<a3::IemNode> encoder, decoder;
};

std::vector<float> sineWave (size_t frames, double hz, double rate, float level)
{
    std::vector<float> x (frames);
    for (size_t i = 0; i < frames; ++i)
        x[i] = level * (float) std::sin (6.283185307179586 * hz * (double) i / rate);
    return x;
}

float minus12dBFs() { return std::pow (10.0f, -12.0f / 20.0f); }
}

TEST_P (Zone, ASourceTowardEachTopIsLoudestThere)
{
    const size_t frames = (size_t) GetParam().sampleRate;
    const auto source = a3test::pink (frames, minus12dBFs(), 7);
    double worstMargin = 1e9;
    for (size_t k = 0; k < zone->topDirections.size(); ++k)
    {
        pointSourceAt (zone->topDirections[k].first, zone->topDirections[k].second);
        const auto out = render (source);
        const double aimed = rmsDb (out[(size_t) zone->layout.topChannels[k] - 1]);
        for (size_t other = 0; other < zone->layout.topChannels.size(); ++other)
        {
            if (other == k)
                continue;
            const double margin = aimed - rmsDb (out[(size_t) zone->layout.topChannels[other] - 1]);
            worstMargin = std::min (worstMargin, margin);
            EXPECT_GE (margin, 3.0) << "source at top " << k + 1 << ", louder top " << other + 1;
        }
    }
    RecordProperty ("worstMarginDb", std::to_string (worstMargin));
    std::cout << "[margin] " << GetParam().name << " worst loudest-top margin " << worstMargin << " dB\n";
}

TEST_P (Zone, TheSubCarriesOnlyTheLowEnd)
{
    ASSERT_TRUE (zone->layout.hasSub());
    const size_t frames = (size_t) GetParam().sampleRate;
    pointSourceAt (0.0f, 0.0f);
    const auto low = render (sineWave (frames, 50.0, GetParam().sampleRate, 0.25f));
    const auto high = render (sineWave (frames, 1000.0, GetParam().sampleRate, 0.25f));

    const size_t sub = (size_t) zone->layout.subChannel - 1;
    EXPECT_GE (rmsDb (low[sub]) - rmsDb (high[sub]), 40.0);

    double lowTops = 0.0, highTops = 0.0;
    for (const int channel : zone->layout.topChannels)
    {
        lowTops += rmsDb (low[(size_t) channel - 1]);
        highTops += rmsDb (high[(size_t) channel - 1]);
    }
    const double count = (double) zone->layout.topChannels.size();
    EXPECT_GE (highTops / count - lowTops / count, 20.0);
    std::cout << "[sub] " << GetParam().name << " sub(50)-sub(1k) " << rmsDb (low[sub]) - rmsDb (high[sub])
              << " dB, tops(1k)-tops(50) " << (highTops - lowTops) / count << " dB\n";
}

INSTANTIATE_TEST_SUITE_P (Zones, Zone,
    testing::Values (ZoneCase { "main_44k", "default", false, 44100.0 },
                     ZoneCase { "main_48k", "default", false, 48000.0 },
                     ZoneCase { "booth41_44k", "default", true, 44100.0 },
                     ZoneCase { "booth41_48k", "default", true, 48000.0 },
                     ZoneCase { "booth61_44k", "reaper", true, 44100.0 },
                     ZoneCase { "booth61_48k", "reaper", true, 48000.0 }),
    caseName);
