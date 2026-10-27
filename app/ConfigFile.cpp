// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ConfigFile.h"
#include "JsonParse.h"
#include "SpatialConfig.h"
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

AppConfig parse (const juce::File& file)
{
    const auto root = parseFile (file);
    const auto& object = objectOf (root, "the file");
    rejectUnknownKeys (object, { "engine", "backend", "io", "spatial" }, "the file");

    AppConfig config;
    juce::String error;
    const auto engine = object.hasProperty ("engine") ? object.getProperty ("engine") : juce::var (new juce::DynamicObject());
    if (const auto parsed = a3::parseEngineConfig (engine, error))
        config.engine = *parsed;
    else
        refuse ("engine: " + error);
    checkTrims (config.engine.trims);

    if (object.hasProperty ("backend"))
        config.backend = parseBackend (object.getProperty ("backend"));
    if (object.hasProperty ("spatial"))
    {
        const auto path = object.getProperty ("spatial");
        if (! path.isString() || path.toString().isEmpty())
            refuse ("spatial must be a directory path");
        config.spatialDirectory = file.getParentDirectory().getChildFile (path.toString());
    }

    const auto spatial = a3::loadSpatialConfig (config.spatialDirectory, error);
    if (! spatial)
        refuse ("spatial (" + config.spatialDirectory.getFullPathName() + "): " + error);
    const auto io = object.hasProperty ("io") ? object.getProperty ("io") : juce::var (new juce::DynamicObject());
    if (const auto map = parseIoMap (io, enginePortNames (config.engine, *spatial), error))
        config.io = *map;
    else
        refuse (error);
    return config;
}
}

std::optional<AppConfig> loadAppConfig (const juce::File& file, juce::String& error)
{
    try
    {
        error = {};
        return parse (file);
    }
    catch (const Refusal& refusal)
    {
        error = file.getFullPathName() + ": " + refusal.reason;
        return std::nullopt;
    }
}

juce::Result saveAppConfig (const AppConfig& config, const juce::File& file)
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
    root->setProperty ("spatial", config.spatialDirectory.getFullPathName());
    const auto text = juce::JSON::toString (juce::var (root), false);

    if (! file.getParentDirectory().createDirectory())
        return juce::Result::fail ("cannot create " + file.getParentDirectory().getFullPathName());
    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (text))
        return juce::Result::fail ("cannot write " + temporary.getFile().getFullPathName());
    // The file must be one the loader accepts: a refused write leaves the old file in place.
    juce::String refusal;
    if (! loadAppConfig (temporary.getFile(), refusal))
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
}
