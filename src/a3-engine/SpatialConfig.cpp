// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "SpatialConfig.h"

namespace a3
{
namespace
{
class SpatialConfigReader
{
public:
    SpatialConfigReader (const juce::File& directory, juce::String& errorOut) : dir (directory), error (errorOut) {}

    std::optional<SpatialConfig> read()
    {
        SpatialConfig config;
        auto encoder = readParameters ("encoder.parameters.json");
        auto rec = readParameters ("rec.parameters.json");
        if (! encoder || ! rec)
            return std::nullopt;
        config.encoderParameters = withOrder (*encoder, "orderSetting");
        config.recParameters = withOrder (*rec, "inputOrderSetting");

        auto main = readZone ("main");
        auto booth = readZone ("booth");
        if (! main || ! booth)
            return std::nullopt;
        config.main = std::move (*main);
        config.booth = std::move (*booth);
        return config;
    }

private:
    bool fail (const juce::String& file, const juce::String& reason)
    {
        error = file + ": " + reason;
        return false;
    }

    std::optional<juce::String> readText (const juce::String& name)
    {
        const auto file = dir.getChildFile (name);
        if (! file.existsAsFile())
        {
            fail (name, "file is missing in " + dir.getFullPathName());
            return std::nullopt;
        }
        return file.loadFileAsString();
    }

    std::optional<juce::var> readJson (const juce::String& name)
    {
        const auto text = readText (name);
        if (! text)
            return std::nullopt;
        juce::var parsed;
        if (const auto parse = juce::JSON::parse (*text, parsed); parse.failed())
        {
            fail (name, "not valid JSON: " + parse.getErrorMessage());
            return std::nullopt;
        }
        return parsed;
    }

    std::optional<juce::var> readParameters (const juce::String& name)
    {
        auto parsed = readJson (name);
        if (! parsed)
            return std::nullopt;
        if (parsed->getDynamicObject() == nullptr)
        {
            fail (name, "expected a JSON object {id: value}");
            return std::nullopt;
        }
        return parsed;
    }

    static juce::var withOrder (const juce::var& parameters, const char* orderId)
    {
        auto explicitOrder = parameters.clone();
        explicitOrder.getDynamicObject()->setProperty (orderId, iemOrderThree);
        return explicitOrder;
    }

    // Sums the dB of every stage of one kind; false (with the reason) for a kind that is neither.
    bool readStages (const juce::String& name, const juce::var& stages, float& fixedDb, float& plan4Db)
    {
        if (! stages.isArray())
            return fail (name, "\"stages\" must be an array");
        for (const auto& stage : *stages.getArray())
        {
            const auto kind = stage.getProperty ("kind", {}).toString();
            const auto db = stage.getProperty ("db", {});
            if (! (db.isInt() || db.isDouble()))
                return fail (name, "stage \"" + stage.getProperty ("name", {}).toString() + "\" has no numeric \"db\"");
            if (kind == "fixed")
                fixedDb += (float) (double) db;
            else if (kind == "plan4")
                plan4Db += (float) (double) db;
            else
                return fail (name, "unknown stage kind \"" + kind + "\" (fixed or plan4)");
        }
        return true;
    }

    bool readDirections (const juce::String& name, const juce::var& directions, size_t expected,
                         std::vector<std::pair<float, float>>& out)
    {
        if (! directions.isArray() || (size_t) directions.size() != expected)
            return fail (name, "\"topDirections\" needs one [azimuth, elevation] per top ("
                                   + juce::String ((int) expected) + ")");
        for (const auto& pair : *directions.getArray())
        {
            if (! pair.isArray() || pair.size() != 2)
                return fail (name, "every \"topDirections\" entry must be [azimuth, elevation]");
            out.emplace_back ((float) (double) pair[0], (float) (double) pair[1]);
        }
        return true;
    }

    std::optional<ZoneConfig> readZone (const juce::String& zoneName)
    {
        const auto decoderName = zoneName + ".decoder.json";
        const auto parametersName = zoneName + ".parameters.json";
        const auto zoneFileName = zoneName + ".zone.json";

        const auto decoderJson = readText (decoderName);
        const auto parameters = readParameters (parametersName);
        const auto zoneFile = readJson (zoneFileName);
        if (! decoderJson || ! parameters || ! zoneFile)
            return std::nullopt;

        juce::String layoutError;
        auto layout = parseLayout (*decoderJson, *parameters, layoutError);
        if (! layout)
        {
            fail (decoderName, layoutError);
            return std::nullopt;
        }

        ZoneConfig zone;
        zone.layout = std::move (*layout);
        zone.parameters = withOrder (*parameters, "inputOrderSetting");
        if (! readStages (zoneFileName, zoneFile->getProperty ("stages", {}), zone.fixedGainDb, zone.absorbedGainDb)
            || ! readDirections (zoneFileName, zoneFile->getProperty ("topDirections", {}),
                                 zone.layout.topChannels.size(), zone.topDirections))
            return std::nullopt;
        return zone;
    }

    juce::File dir;
    juce::String& error;
};
}

std::optional<SpatialConfig> loadSpatialConfig (const juce::File& directory, juce::String& error)
{
    return SpatialConfigReader (directory, error).read();
}
}
