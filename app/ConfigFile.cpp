// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConfigFile.h"
#include "ConfigEdits.h"
#include "JsonParse.h"
#include "SpatialConfig.h"
#include <algorithm>
#include <cmath>

namespace a3app
{
namespace
{
constexpr int minBlock = 64, maxBlock = 2048;

using a3::jsonparse::Refusal;
using a3::jsonparse::refuse;
using a3::jsonparse::objectOf;
using a3::jsonparse::rejectUnknownKeys;

juce::String stringProperty (const juce::DynamicObject& object, const char* key)
{
    const auto v = object.getProperty (key);
    if (! v.isString())
        refuse (juce::String ("backend.") + key + " must be a string");
    return v.toString();
}

double numberProperty (const juce::DynamicObject& object, const char* key)
{
    const auto v = object.getProperty (key);
    if (! (v.isInt() || v.isInt64() || v.isDouble()) || ! std::isfinite ((double) v))
        refuse (juce::String ("backend.") + key + " must be a finite number");
    return (double) v;
}

BackendSettings parseBackend (const juce::var& section)
{
    const auto& object = objectOf (section, "backend");
    rejectUnknownKeys (object, { "type", "deviceType", "inputDevice", "outputDevice", "sampleRate", "blockSize" }, "backend");
    BackendSettings backend;
    if (object.hasProperty ("type"))
    {
        const auto type = object.getProperty ("type").toString();
        if (type == "jack")
            backend.type = BackendSettings::Type::jack;
        else if (type == "device")
            backend.type = BackendSettings::Type::device;
        else
            refuse ("backend.type must be \"jack\" or \"device\"");
    }
    if (object.hasProperty ("deviceType"))
        backend.deviceType = stringProperty (object, "deviceType");
    if (object.hasProperty ("inputDevice"))
        backend.inputDevice = stringProperty (object, "inputDevice");
    if (object.hasProperty ("outputDevice"))
        backend.outputDevice = stringProperty (object, "outputDevice");
    if (object.hasProperty ("sampleRate"))
    {
        backend.sampleRate = numberProperty (object, "sampleRate");
        if (backend.sampleRate < 8000 || backend.sampleRate > 384000)
            refuse ("backend.sampleRate must be within 8000..384000");
    }
    if (object.hasProperty ("blockSize"))
    {
        const double size = numberProperty (object, "blockSize");
        if (size != std::floor (size) || size < minBlock || size > maxBlock)
            refuse ("backend.blockSize must be a whole number within 64..2048");
        backend.blockSize = (int) size;
    }
    return backend;
}

void checkTrim (const std::optional<float>& trim, const char* name)
{
    if (! trim)
        return;
    if (! std::isfinite (*trim) || *trim < minTrimDb || *trim > maxTrimDb)
        refuse (juce::String ("engine.trimsDb.") + name + " must be within -90..+6 dB (is " + juce::String (*trim) + ")");
}

void checkTrims (const a3::Trims& trims)
{
    checkTrim (trims.main, "main");
    checkTrim (trims.booth, "booth");
    checkTrim (trims.phones, "phones");
    checkTrim (trims.rec, "rec");
}

juce::var parseFile (const juce::File& file)
{
    if (! file.existsAsFile())
        refuse ("the file does not exist");
    juce::var parsed;
    const auto result = juce::JSON::parse (file.loadFileAsString(), parsed);
    if (result.failed())
        refuse ("not valid JSON: " + result.getErrorMessage());
    return parsed;
}

std::string peerText (const juce::var& peer) { return peer.toString().toStdString(); }

// Removes the retired ports from the io section before it is validated, so an engine.json saved
// before aux and cue grew still loads; each removed link is recorded for STATUS and stderr.
juce::var withoutRetiredPorts (const juce::var& io, const std::vector<std::string>& retired, std::vector<std::string>& dropped)
{
    const auto* object = io.getDynamicObject();
    if (object == nullptr)
        return io;   // parseIoMap refuses it, naming the section
    auto* copy = object->clone().release();
    juce::var result (copy);   // owns the clone

    const auto isRetired = [&] (const juce::Identifier& name)
    {
        return std::find (retired.begin(), retired.end(), name.toString().toStdString()) != retired.end();
    };
    // The clone is deep (var::clone), so removing from it never touches the caller's section.
    const auto retiredIn = [&] (const char* key)
    {
        std::vector<std::pair<juce::Identifier, juce::var>> found;
        if (auto* section = copy->getProperty (key).getDynamicObject())
        {
            for (const auto& property : section->getProperties())
                if (isRetired (property.name))
                    found.emplace_back (property.name, property.value);
            for (const auto& entry : found)
                section->removeProperty (entry.first);
        }
        return found;
    };
    for (const auto& [name, peers] : retiredIn ("jack"))
    {
        const auto port = name.toString().toStdString();
        if (peers.getArray() == nullptr || peers.getArray()->isEmpty())
            dropped.push_back (port);
        else
            for (const auto& peer : *peers.getArray())
                dropped.push_back (port + " -> " + peerText (peer));
    }
    for (const auto& [name, channel] : retiredIn ("deviceChannels"))
        dropped.push_back (name.toString().toStdString() + " -> device channel " + channel.toString().toStdString());
    return result;
}

juce::File spatialPath (const juce::var& path, const juce::File& base, const juce::String& what)
{
    if (! path.isString() || path.toString().isEmpty())
        refuse (what + " must be a directory path");
    return base.getChildFile (path.toString());
}

// One dir for both zones (an older file), or {"main": dir, "booth": dir}.
void parseSpatial (const juce::var& spatial, const juce::File& base, AppConfig& config)
{
    if (! spatial.isObject())
    {
        config.spatialDirectory = spatialPath (spatial, base, "spatial");
        return;
    }
    rejectUnknownKeys (*spatial.getDynamicObject(), { "main", "booth" }, "spatial");
    config.spatialDirectory = spatialPath (spatial["main"], base, "spatial.main");
    const auto booth = spatialPath (spatial["booth"], base, "spatial.booth");
    config.boothSpatialDirectory = booth == config.spatialDirectory ? juce::File() : booth;
}

AppConfig parseRoot (const juce::var& root, const juce::File& base, int truthDecks)
{
    const auto& object = objectOf (root, "the file");
    rejectUnknownKeys (object, { "engine", "backend", "io", "spatial" }, "the file");

    AppConfig config;
    juce::String error;
    const auto engine = object.hasProperty ("engine") ? object.getProperty ("engine") : juce::var (new juce::DynamicObject());
    if (const auto parsed = a3::parseEngineConfig (engine, error, truthDecks))
        config.engine = *parsed;
    else
        refuse ("engine: " + error);
    for (const auto& key : a3::retiredEngineKeys())
        if (engine.hasProperty (juce::Identifier (key)))
            config.ignoredKeys.push_back ("engine." + key);
    checkTrims (config.engine.trims);

    if (object.hasProperty ("backend"))
        config.backend = parseBackend (object.getProperty ("backend"));
    if (object.hasProperty ("spatial"))
        parseSpatial (object.getProperty ("spatial"), base, config);

    const auto spatial = loadZonesSpatial (config, error);
    if (! spatial)
        refuse ("spatial (" + config.spatialDirectory.getFullPathName() + "): " + error);
    const auto io = object.hasProperty ("io") ? object.getProperty ("io") : juce::var (new juce::DynamicObject());
    const auto cleaned = withoutRetiredPorts (io, retiredDeckPorts (config.engine.decks), config.droppedLinks);
    if (const auto map = parseIoMap (cleaned, enginePortNames (config.engine, *spatial), error))
        config.io = *map;
    else
        refuse (error);
    return config;
}

// A9/A10: only a load from disk fills the slots the file links nowhere; a posted body keeps what the page made.
AppConfig parse (const juce::File& file, int truthDecks)
{
    auto config = parseRoot (parseFile (file), file.getParentDirectory(), truthDecks);
    linkUnnamedSlotsLocally (config.io, config.engine.decks);
    return config;
}
}

std::optional<AppConfig> parseAppConfig (const juce::var& root, const juce::File& base, juce::String& error, int truthDecks)
{
    try
    {
        error = {};
        return parseRoot (root, base, truthDecks);
    }
    catch (const Refusal& refusal)
    {
        error = refusal.reason;
        return std::nullopt;
    }
}

std::optional<AppConfig> loadAppConfig (const juce::File& file, juce::String& error, int truthDecks)
{
    try
    {
        error = {};
        return parse (file, truthDecks);
    }
    catch (const Refusal& refusal)
    {
        error = file.getFullPathName() + ": " + refusal.reason;
        return std::nullopt;
    }
}

juce::File zoneSpatialDirectory (const AppConfig& config, a3::SpeakerZone zone)
{
    if (zone == a3::SpeakerZone::booth && config.boothSpatialDirectory != juce::File())
        return config.boothSpatialDirectory;
    return config.spatialDirectory;
}

std::optional<a3::SpatialConfig> loadZonesSpatial (const AppConfig& config, juce::String& error)
{
    return a3::loadSpatialConfig (zoneSpatialDirectory (config, a3::SpeakerZone::main),
                                  zoneSpatialDirectory (config, a3::SpeakerZone::booth), error);
}

juce::StringArray droppedLinkLines (const AppConfig& config)
{
    juce::StringArray lines;
    for (const auto& entry : config.droppedLinks)
        lines.add ("dropped retired link " + entry);
    for (const auto& key : config.ignoredKeys)
        lines.add ("ignored retired key " + key);
    return lines;
}

juce::var toJson (const AppConfig& config)
{
    auto* backend = new juce::DynamicObject();
    backend->setProperty ("type", config.backend.type == BackendSettings::Type::jack ? "jack" : "device");
    backend->setProperty ("deviceType", config.backend.deviceType);
    backend->setProperty ("inputDevice", config.backend.inputDevice);
    backend->setProperty ("outputDevice", config.backend.outputDevice);
    backend->setProperty ("sampleRate", config.backend.sampleRate);
    backend->setProperty ("blockSize", config.backend.blockSize);

    auto* root = new juce::DynamicObject();
    root->setProperty ("engine", a3::toJson (config.engine));
    root->setProperty ("backend", juce::var (backend));
    root->setProperty ("io", toJson (config.io));
    auto* spatial = new juce::DynamicObject();
    spatial->setProperty ("main", zoneSpatialDirectory (config, a3::SpeakerZone::main).getFullPathName());
    spatial->setProperty ("booth", zoneSpatialDirectory (config, a3::SpeakerZone::booth).getFullPathName());
    root->setProperty ("spatial", juce::var (spatial));
    return juce::var (root);
}

juce::Result saveAppConfig (const AppConfig& config, const juce::File& file)
{
    const auto text = juce::JSON::toString (toJson (config), false);

    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create " + file.getParentDirectory().getFullPathName());
    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (text))
        return juce::Result::fail ("cannot write " + temporary.getFile().getFullPathName());
    // The file must be one the loader accepts: a refused write leaves the old file in place.
    juce::String refusal;
    if (! loadAppConfig (temporary.getFile(), refusal, config.engine.decks))   // a count left out is the one in use
        return juce::Result::fail ("not saved, " + refusal);
    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("cannot replace " + file.getFullPathName());
    return juce::Result::ok();
}

juce::File chooseConfigFile (const juce::String& argument, const juce::File& home, std::function<bool (const juce::File&)> exists,
                             const juce::File& packageConfig)
{
    if (argument.isNotEmpty())
        return juce::File (argument);
    const auto user = userConfigFile (home);
    return exists (user) ? user : packageConfig;
}

juce::File userConfigFile (const juce::File& home) { return home.getChildFile (".config/a3/engine/engine.json"); }

juce::File chooseSaveFile (const juce::String& argument, const juce::File& home)
{
    return argument.isNotEmpty() ? juce::File (argument) : userConfigFile (home);
}

std::vector<juce::File> spatialCandidates (const juce::File& configDir)
{
    const auto installed = AppConfig{}.spatialDirectory;
    return { configDir.getChildFile ("engine/spatial"), installed, installed.getSiblingFile (installed.getFileName() + "-reaper-booth-6.1") };
}

namespace
{
bool holdsAVariant (const juce::File& directory) { return directory.getChildFile ("main.decoder.json").existsAsFile(); }
}

std::vector<juce::File> spatialChoices (const juce::File& configDir)
{
    const auto candidates = spatialCandidates (configDir);
    std::vector<juce::File> choices;
    for (const auto& directory : candidates)
        if (holdsAVariant (directory))
            choices.push_back (directory);
    auto variants = candidates.front().findChildFiles (juce::File::findDirectories, false);
    variants.sort();
    for (const auto& variant : variants)
        if (holdsAVariant (variant) && ! variant.getFileName().startsWithChar ('.'))
            choices.push_back (variant);
    return choices;
}
}
