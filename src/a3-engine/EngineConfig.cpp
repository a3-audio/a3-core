// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "EngineConfig.h"
#include "JsonParse.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace a3
{
const std::vector<juce::String>& headphoneEqModels()
{
    static const std::vector<juce::String> models {
        "AKG-K141MK2", "AKG-K240DF", "AKG-K240MK2", "AKG-K271MK2", "AKG-K271STUDIO", "AKG-K601", "AKG-K701",
        "AKG-K702", "AKG-K1000-Closed", "AKG-K1000-Open", "AudioTechnica-ATH-M50", "Beyerdynamic-DT250",
        "Beyerdynamic-DT770PRO-250Ohms", "Beyerdynamic-DT880", "Beyerdynamic-DT990PRO", "Presonus-HD7",
        "Sennheiser-HD430", "Sennheiser-HD480", "Sennheiser-HD560ovationII", "Sennheiser-HD565ovation",
        "Sennheiser-HD600", "Sennheiser-HD650", "SHURE-SRH940" };
    return models;
}

std::optional<int> headphoneEqChoice (const juce::String& name)
{
    if (name == "off")
        return 0;
    const auto& models = headphoneEqModels();
    const auto it = std::find (models.begin(), models.end(), name);
    if (it == models.end())
        return std::nullopt;
    return (int) (it - models.begin()) + 1;
}

bool operator== (const Trims& a, const Trims& b)
{
    return a.main == b.main && a.booth == b.booth && a.phones == b.phones && a.rec == b.rec;
}

bool operator== (const ModulatedParameter& a, const ModulatedParameter& b)
{
    return a.parameter == b.parameter && a.base == b.base && a.depth == b.depth
        && a.rateHz == b.rateHz && a.shape == b.shape;
}

bool operator== (const EngineConfig& a, const EngineConfig& b)
{
    return a.channels == b.channels && a.decks == b.decks && a.decksNamed == b.decksNamed && a.analogInputs == b.analogInputs
        && a.analogPairs == b.analogPairs
        && a.trims == b.trims && a.phonesHeadphoneEq == b.phonesHeadphoneEq && a.filterKind == b.filterKind && a.delayModulation == b.delayModulation;
}

namespace
{
using jsonparse::Refusal;
using jsonparse::refuse;
using jsonparse::objectOf;
using jsonparse::rejectUnknownKeys;

// The analog return's pair (analog 11/12), gone with P45: accepted, never read, never written.
constexpr const char* retiredReturnPair = "returnAnalogPair";

bool isNumber (const juce::var& v) { return v.isInt() || v.isInt64() || v.isDouble(); }

double finiteNumber (const juce::var& v, const juce::String& what)
{
    if (! isNumber (v))
        refuse (what + " must be a number");
    const double d = (double) v;
    if (! std::isfinite (d))
        refuse (what + " must be finite");
    return d;
}

int wholeNumber (const juce::var& v, const juce::String& what, int minimum)
{
    const double d = finiteNumber (v, what);
    if (d != std::floor (d) || d < minimum || d > 1.0e6)
        refuse (what + " must be a whole number >= " + juce::String (minimum));
    return (int) d;
}

std::optional<float> optionalTrim (const juce::DynamicObject& trims, const char* key)
{
    if (! trims.hasProperty (key))
        return std::nullopt;
    return (float) finiteNumber (trims.getProperty (key), juce::String ("trimsDb.") + key);
}

Trims parseTrims (const juce::var& v)
{
    const auto& object = objectOf (v, "trimsDb");
    rejectUnknownKeys (object, { "main", "booth", "phones", "rec" }, "trimsDb");
    Trims trims;
    trims.main = optionalTrim (object, "main");
    trims.booth = optionalTrim (object, "booth");
    trims.phones = optionalTrim (object, "phones");
    if (const auto rec = optionalTrim (object, "rec"))
        trims.rec = *rec;
    return trims;
}

juce::String parseHeadphoneEq (const juce::var& v)
{
    if (! v.isString())
        refuse ("phonesHeadphoneEq must be \"off\" or a headphone model name");
    const auto name = v.toString();
    if (! headphoneEqChoice (name))
        refuse ("phonesHeadphoneEq: unknown headphone model '" + name + "'");
    return name;
}

FilterKind parseFilterKind (const juce::var& v)
{
    if (v.isString() && v.toString() == "svf")
        return FilterKind::svf;
    if (v.isString() && v.toString() == "ladder")
        return FilterKind::ladder;
    refuse ("filterKind must be \"svf\" or \"ladder\"");
}

float entryNumber (const juce::DynamicObject& entry, const char* key, const juce::String& name)
{
    if (! entry.hasProperty (key))
        refuse ("delayModulation entry " + name + ": " + key + " is missing");
    return (float) finiteNumber (entry.getProperty (key), "delayModulation entry " + name + ": " + key);
}

ModulatedParameter parseModulation (const juce::var& v, int index)
{
    const juce::String position = "#" + juce::String (index);
    const auto& entry = objectOf (v, "delayModulation entry " + position);
    rejectUnknownKeys (entry, { "parameter", "base", "depth", "rateHz", "shape" }, "delayModulation entry " + position);
    if (! entry.getProperty ("parameter").isString() || entry.getProperty ("parameter").toString().isEmpty())
        refuse ("delayModulation entry " + position + ": parameter is missing");

    ModulatedParameter m;
    m.parameter = entry.getProperty ("parameter").toString();
    m.base = entryNumber (entry, "base", m.parameter);
    m.depth = entryNumber (entry, "depth", m.parameter);
    m.rateHz = entryNumber (entry, "rateHz", m.parameter);
    if (m.rateHz < 0.0f)
        refuse ("delayModulation entry " + m.parameter + ": rateHz must be >= 0");
    if (! entry.hasProperty ("shape"))
        refuse ("delayModulation entry " + m.parameter + ": shape is missing");
    m.shape = entry.getProperty ("shape").toString();
    if (m.shape != "sine" && m.shape != "triangle" && m.shape != "random")
        refuse ("delayModulation entry " + m.parameter + ": unknown shape '" + m.shape + "'");
    return m;
}

std::vector<ModulatedParameter> parseModulations (const juce::var& v)
{
    const auto* list = v.getArray();
    if (list == nullptr)
        refuse ("delayModulation must be a list");
    std::vector<ModulatedParameter> entries;
    for (int i = 0; i < list->size(); ++i)
        entries.push_back (parseModulation ((*list)[i], i));
    return entries;
}

// Absent: the default four analog channels, the rest deck-only. Present: as listed,
// the channels beyond the list are deck-only.
std::vector<int> parsePairs (const juce::DynamicObject& section, const EngineConfig& config)
{
    std::vector<int> pairs (static_cast<size_t> (config.channels), 0);
    if (! section.hasProperty ("analogPairs"))
    {
        const EngineConfig defaults;
        for (size_t i = 0; i < pairs.size() && i < defaults.analogPairs.size(); ++i)
            pairs[i] = defaults.analogPairs[i];
        return pairs;
    }
    const auto* list = section.getProperty ("analogPairs").getArray();
    if (list == nullptr)
        refuse ("analogPairs must be a list");
    if ((size_t) list->size() > pairs.size())
        refuse ("analogPairs lists " + juce::String (list->size()) + " channels, channels is "
                + juce::String (config.channels));
    for (int i = 0; i < list->size(); ++i)
        pairs[(size_t) i] = wholeNumber ((*list)[i], "analogPairs[" + juce::String (i) + "]", 0);
    return pairs;
}

void checkPairs (const EngineConfig& config)
{
    std::set<int> used;
    for (size_t i = 0; i < config.analogPairs.size(); ++i)
    {
        const int pair = config.analogPairs[i];
        if (pair == 0)
            continue;
        const auto channel = "analogPairs: channel " + juce::String ((int) i + 1);
        if (pair * 2 > config.analogInputs)
            refuse (channel + " is on pair " + juce::String (pair) + ", beyond the " + juce::String (config.analogInputs) + " analog inputs");
        if (! used.insert (pair).second)
            refuse (channel + " shares pair " + juce::String (pair) + " with another channel");
    }
}

EngineConfig parse (const juce::var& v, int truthDecks)
{
    const auto& section = objectOf (v, "the engine section");
    rejectUnknownKeys (section, { "channels", "decks", "analogInputs", "analogPairs", retiredReturnPair,
                                  "trimsDb", "phonesHeadphoneEq", "filterKind", "delayModulation" }, "the engine section");
    EngineConfig config;
    if (section.hasProperty ("channels"))
        config.channels = wholeNumber (section.getProperty ("channels"), "channels", 1);
    config.decksNamed = section.hasProperty ("decks");
    config.decks = config.decksNamed ? wholeNumber (section.getProperty ("decks"), "decks", 0) : truthDecks;
    if (section.hasProperty ("analogInputs"))
        config.analogInputs = wholeNumber (section.getProperty ("analogInputs"), "analogInputs", 0);
    config.analogPairs = parsePairs (section, config);
    checkPairs (config);
    if (section.hasProperty ("trimsDb"))
        config.trims = parseTrims (section.getProperty ("trimsDb"));
    if (section.hasProperty ("phonesHeadphoneEq"))
        config.phonesHeadphoneEq = parseHeadphoneEq (section.getProperty ("phonesHeadphoneEq"));
    if (section.hasProperty ("filterKind"))
        config.filterKind = parseFilterKind (section.getProperty ("filterKind"));
    if (section.hasProperty ("delayModulation"))
        config.delayModulation = parseModulations (section.getProperty ("delayModulation"));
    return config;
}

void setTrim (juce::DynamicObject& trims, const char* key, const std::optional<float>& value)
{
    if (value)
        trims.setProperty (key, (double) *value);
}
}

const std::vector<std::string>& retiredEngineKeys()
{
    static const std::vector<std::string> keys { retiredReturnPair };
    return keys;
}

std::optional<EngineConfig> parseEngineConfig (const juce::var& engineSection, juce::String& error, int truthDecks)
{
    try
    {
        error = {};
        return parse (engineSection, truthDecks);
    }
    catch (const Refusal& refusal)
    {
        error = refusal.reason;
        return std::nullopt;
    }
}

juce::var toJson (const EngineConfig& config)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("channels", config.channels);
    if (config.decksNamed)
        root->setProperty ("decks", config.decks);
    root->setProperty ("analogInputs", config.analogInputs);
    juce::Array<juce::var> pairs;
    for (const int pair : config.analogPairs)
        pairs.add (pair);
    root->setProperty ("analogPairs", pairs);

    auto* trims = new juce::DynamicObject();
    setTrim (*trims, "main", config.trims.main);
    setTrim (*trims, "booth", config.trims.booth);
    setTrim (*trims, "phones", config.trims.phones);
    trims->setProperty ("rec", (double) config.trims.rec);
    root->setProperty ("trimsDb", juce::var (trims));

    root->setProperty ("phonesHeadphoneEq", config.phonesHeadphoneEq);
    root->setProperty ("filterKind", config.filterKind == FilterKind::ladder ? "ladder" : "svf");
    juce::Array<juce::var> modulations;
    for (const auto& m : config.delayModulation)
    {
        auto* entry = new juce::DynamicObject();
        entry->setProperty ("parameter", m.parameter);
        entry->setProperty ("base", (double) m.base);
        entry->setProperty ("depth", (double) m.depth);
        entry->setProperty ("rateHz", (double) m.rateHz);
        entry->setProperty ("shape", m.shape);
        modulations.add (juce::var (entry));
    }
    root->setProperty ("delayModulation", modulations);
    return juce::var (root);
}
}
