// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Truth.h"

#include <algorithm>
#include <climits>
#include <cstdint>

#include "Json.h"

namespace a3core
{
namespace
{
// Where the package puts the file (a3_osc.DEFAULT_PATH).
const char* const installedTruth = "/usr/share/a3/a3-osc.json";
const char* const networkUnderHome = ".config/a3/network.json";

// Blocks a3_osc_join takes from the maintainer's file, and the one that may ride beside them.
const char* const networkKeys[] = { "hosts", "network" };
const char* const localOptionalKey = "meters";

[[noreturn]] void refuse (const std::string& what) { throw TruthError { what }; }

std::string utf8 (const juce::String& text) { return text.toStdString(); }

juce::var deepCopy (const juce::var& value)
{
    if (auto* object = value.getDynamicObject())
    {
        auto* copy = new juce::DynamicObject();
        for (const auto& property : object->getProperties())
            copy->setProperty (property.name, deepCopy (property.value));
        return juce::var (copy);
    }
    if (auto* items = value.getArray())
    {
        juce::Array<juce::var> copy;
        for (const auto& item : *items)
            copy.add (deepCopy (item));
        return juce::var (copy);
    }
    return value;
}

bool isObject (const juce::var& value) { return value.getDynamicObject() != nullptr; }

bool isNumber (const juce::var& value) { return value.isInt() || value.isInt64() || value.isDouble(); }

// repr() of a parsed JSON value, for messages.
std::string pythonValue (const juce::var& value)
{
    if (value.isVoid())
        return "None";
    if (value.isBool())
        return static_cast<bool> (value) ? "True" : "False";
    if (value.isInt() || value.isInt64())
        return std::to_string (static_cast<juce::int64> (value));
    if (value.isDouble())
        return pythonRepr (static_cast<double> (value));
    if (value.isString())
        return "'" + utf8 (value.toString()) + "'";
    if (auto* items = value.getArray())
    {
        std::string text = "[";
        for (int i = 0; i < items->size(); ++i)
            text += (i > 0 ? ", " : "") + pythonValue ((*items)[i]);
        return text + "]";
    }
    if (auto* object = value.getDynamicObject())
    {
        std::string text = "{";
        bool first = true;
        for (const auto& property : object->getProperties())
        {
            text += (first ? "'" : ", '") + utf8 (property.name.toString()) + "': " + pythonValue (property.value);
            first = false;
        }
        return text + "}";
    }
    return "?";
}

// Reads UTF-8 text the way Python's read_text does: bytes that are not UTF-8 are an error.
bool readText (const juce::File& file, juce::String& text, std::string& problem)
{
    if (file.isDirectory())
    {
        problem = "Is a directory";
        return false;
    }
    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
    {
        problem = "cannot be read";
        return false;
    }
    const auto* chars = static_cast<const char*> (bytes.getData());
    const auto size = static_cast<int> (bytes.getSize());
    if (! juce::CharPointer_UTF8::isValidString (chars, size))
    {
        problem = "is not UTF-8";
        return false;
    }
    text = juce::String::fromUTF8 (chars, size);
    return true;
}

// {**packaged, **local}: a local value that is not an object is kept whole, so the reader
// refuses it with a reason instead of the join hiding it.
juce::var keyByKey (const juce::var& packaged, const juce::var& local)
{
    if (! isObject (local))
        return local;
    auto* merged = new juce::DynamicObject();
    if (auto* base = packaged.getDynamicObject())
        for (const auto& property : base->getProperties())
            merged->setProperty (property.name, property.value);
    for (const auto& property : local.getDynamicObject()->getProperties())
        merged->setProperty (property.name, property.value);
    return juce::var (merged);
}

// Decimal digits as Python's \d and int() take them: the zero of each Unicode Nd run of ten
// (Unicode 16, as recorded from the B0 interpreter).
const juce::juce_wchar digitZeros[] = {
    0x30, 0x660, 0x6f0, 0x7c0, 0x966, 0x9e6, 0xa66, 0xae6, 0xb66, 0xbe6, 0xc66, 0xce6, 0xd66, 0xde6,
    0xe50, 0xed0, 0xf20, 0x1040, 0x1090, 0x17e0, 0x1810, 0x1946, 0x19d0, 0x1a80, 0x1a90, 0x1b50,
    0x1bb0, 0x1c40, 0x1c50, 0xa620, 0xa8d0, 0xa900, 0xa9d0, 0xa9f0, 0xaa50, 0xabf0, 0xff10, 0x104a0,
    0x10d30, 0x10d40, 0x11066, 0x110f0, 0x11136, 0x111d0, 0x112f0, 0x11450, 0x114d0, 0x11650,
    0x116c0, 0x116d0, 0x116da, 0x11730, 0x118e0, 0x11950, 0x11bf0, 0x11c50, 0x11d50, 0x11da0,
    0x11f50, 0x16130, 0x16a60, 0x16ac0, 0x16b50, 0x16d70, 0x1ccf0, 0x1d7ce, 0x1d7d8, 0x1d7e2,
    0x1d7ec, 0x1d7f6, 0x1e140, 0x1e2f0, 0x1e4f0, 0x1e5f1, 0x1e950, 0x1fbf0
};

// The digit's value, or -1 if it is not a decimal digit.
int digitValue (juce::juce_wchar c)
{
    const auto* end = std::end (digitZeros);
    const auto* after = std::upper_bound (std::begin (digitZeros), end, c);
    if (after == std::begin (digitZeros))
        return -1;
    const auto offset = static_cast<int> (c - *(after - 1));
    return offset <= 9 ? offset : -1;
}

bool isWordChar (juce::juce_wchar c) { return c == '_' || c > 127 || juce::CharacterFunctions::isLetterOrDigit (c); }

// pattern.format(**fields) and re.sub(r"\{(\w+)\}", ...) both see "{name}" as a placeholder.
struct PatternPart
{
    bool isField;
    std::string text;
};

std::vector<PatternPart> splitPattern (const std::string& pattern)
{
    std::vector<PatternPart> parts;
    const juce::String text (juce::CharPointer_UTF8 (pattern.c_str()));
    juce::String literal;
    const int length = text.length();
    for (int i = 0; i < length;)
    {
        if (text[i] == '{')
        {
            int end = i + 1;
            while (end < length && isWordChar (text[end]))
                ++end;
            if (end > i + 1 && end < length && text[end] == '}')
            {
                if (literal.isNotEmpty())
                    parts.push_back ({ false, utf8 (literal) });
                literal.clear();
                parts.push_back ({ true, utf8 (text.substring (i + 1, end)) });
                i = end + 1;
                continue;
            }
        }
        literal += text.substring (i, i + 1);
        ++i;
    }
    if (literal.isNotEmpty())
        parts.push_back ({ false, utf8 (literal) });
    return parts;
}

using Captures = std::vector<std::pair<std::string, std::string>>;   // name -> digits as written

// re.fullmatch of the escaped literals with (\d+) per placeholder, with the backtracking a
// regex does: a placeholder takes as many digits as it can and gives back what the rest needs.
template <class Parts>
bool matchFrom (const Parts& parts, size_t index, const juce::Array<juce::juce_wchar>& text,
                int position, Captures& captures)
{
    if (index == parts.size())
        return position == text.size();
    const auto& part = parts[index];
    if (! part.isField)
    {
        const juce::String literal (juce::CharPointer_UTF8 (part.text.c_str()));
        if (position + literal.length() > text.size())
            return false;
        for (int i = 0; i < literal.length(); ++i)
            if (text[position + i] != literal[i])
                return false;
        return matchFrom (parts, index + 1, text, position + literal.length(), captures);
    }
    int end = position;
    while (end < text.size() && digitValue (text[end]) >= 0)
        ++end;
    for (; end > position; --end)
    {
        const auto slot = captures.size();
        captures.emplace_back (part.text, std::string());
        if (matchFrom (parts, index + 1, text, end, captures))
        {
            juce::String digits;
            for (int i = position; i < end; ++i)
                digits += juce::String::charToString (text[i]);
            captures[slot].second = utf8 (digits);
            return true;
        }
        captures.resize (slot);
    }
    return false;
}

// int() of a string of Unicode decimal digits, saturating far above any range in the file.
juce::int64 integerOf (const std::string& digits)
{
    juce::int64 value = 0;
    const juce::String text (juce::CharPointer_UTF8 (digits.c_str()));
    for (int i = 0; i < text.length(); ++i)
        value = std::min<juce::int64> (value * 10 + digitValue (text[i]), 1000000000000LL);
    return value;
}

const juce::var& member (const juce::var& object, const char* name, const std::string& what)
{
    auto* dynamic = object.getDynamicObject();
    if (dynamic == nullptr || ! dynamic->hasProperty (name))
        refuse (what);
    return dynamic->getProperty (name);
}

// name -> (lowest allowed, whether the lowest itself is allowed)
struct MeterRule
{
    const char* name;
    double floor;
    bool floorAllowed;
    double defaultValue;
};
const MeterRule meterRules[] = { { "attack_ms", 0, true, 0 },
                                 { "release_db_per_second", 0, false, 20 },
                                 { "peak_hold_seconds", 0, true, 1.5 } };

double meterValue (const MeterRule& rule, const juce::var& value)
{
    const std::string name = std::string ("meters.") + rule.name;
    if (value.isBool() || ! isNumber (value))
        refuse (name + " is not a number: " + pythonValue (value));
    const double number = value;
    if (number < rule.floor || (number == rule.floor && ! rule.floorAllowed))
        refuse (name + " must be " + (rule.floorAllowed ? ">=" : ">") + " " + std::to_string (static_cast<int> (rule.floor))
                + ", is " + pythonValue (value));
    return number;
}
}

NetworkRead readNetwork (const juce::File& file)
{
    if (! file.exists())
        return {};
    const std::string where = utf8 (file.getFullPathName());
    juce::String text;
    std::string problem;
    if (! readText (file, text, problem))
        return { std::nullopt, where + ": " + problem };
    juce::String error;
    const auto data = parseJson (text, error);
    if (error.isNotEmpty())
        return { std::nullopt, where + " is not JSON: " + utf8 (error) };
    if (! isObject (data))
        return { std::nullopt, where + " is not an object" };
    for (const char* key : networkKeys)
        if (! isObject (data.getProperty (key, juce::var())))
            return { std::nullopt, where + " has no '" + key + "' object" };
    auto* blocks = new juce::DynamicObject();
    for (const char* key : networkKeys)
        blocks->setProperty (key, data[key]);
    if (data.hasProperty (localOptionalKey))
        blocks->setProperty (localOptionalKey, data[localOptionalKey]);
    return { juce::var (blocks), std::nullopt };
}

juce::var joinTruth (const juce::var& contract, const std::optional<juce::var>& network)
{
    auto* contractObject = contract.getDynamicObject();
    if (contractObject == nullptr)
        return contract;
    auto* joined = new juce::DynamicObject();
    for (const auto& property : contractObject->getProperties())
        joined->setProperty (property.name, property.value);
    if (network.has_value())
    {
        std::vector<const char*> keys (std::begin (networkKeys), std::end (networkKeys));
        keys.push_back (localOptionalKey);
        for (const char* key : keys)
            if (network->hasProperty (key))
                joined->setProperty (key, keyByKey (contract.getProperty (key, juce::var (new juce::DynamicObject())), (*network)[key]));
    }
    return juce::var (joined);
}

Truth::Truth (juce::var data) : truth (deepCopy (data))
{
    const auto addresses = truth.getProperty ("addresses", juce::var());
    auto* object = addresses.getDynamicObject();
    if (object == nullptr)
        return;
    for (const auto& property : object->getProperties())
    {
        if (! property.value.hasProperty ("pattern"))
            continue;
        Matcher matcher { utf8 (property.name.toString()), {} };
        for (const auto& part : splitPattern (utf8 (property.value["pattern"].toString())))
            matcher.parts.push_back ({ part.isField, part.text });
        matchers.push_back (std::move (matcher));
    }
}

std::variant<Truth, TruthError> Truth::load (const juce::File& contractFile, std::optional<juce::File> networkFile)
{
    const std::string where = utf8 (contractFile.getFullPathName());
    if (! contractFile.existsAsFile())
        return TruthError { where + ": no such file" };
    juce::String text;
    std::string problem;
    if (! readText (contractFile, text, problem))
        return TruthError { where + ": " + problem };
    juce::String error;
    const auto contract = parseJson (text, error);
    if (error.isNotEmpty())
        return TruthError { where + " is not JSON: " + utf8 (error) };
    if (! isObject (contract))
        return TruthError { where + " is not an object" };

    NetworkRead read;
    if (networkFile.has_value())
        read = readNetwork (*networkFile);

    Truth truth (joinTruth (contract, read.network));
    std::optional<std::string> metersProblem;
    if (read.network.has_value() && read.network->hasProperty (localOptionalKey))
    {
        try
        {
            truth.meters();
        }
        catch (const TruthError& refused)
        {
            metersProblem = utf8 (networkFile->getFullPathName()) + ": " + refused.what;
            auto* local = new juce::DynamicObject();
            for (const auto& property : read.network->getDynamicObject()->getProperties())
                if (property.name.toString() != localOptionalKey)
                    local->setProperty (property.name, property.value);
            truth = Truth (joinTruth (contract, juce::var (local)));
        }
    }
    truth.networkProblem = read.problem;
    truth.metersProblem = metersProblem;
    return truth;
}

std::string Truth::canonical() const { return canonicalBytes (truth); }

std::string Truth::fingerprint() const { return sha256Hex (canonical()); }

juce::var Truth::data() const { return deepCopy (truth); }

juce::var Truth::network() const
{
    return deepCopy (member (truth, "network", "no network in the truth"));
}

std::string Truth::host (const std::string& name) const
{
    const auto& hosts = truth.getProperty ("hosts", juce::var());
    auto* object = hosts.getDynamicObject();
    if (object == nullptr || ! object->hasProperty (name.c_str()))
        refuse ("no host '" + name + "' in the truth");
    return utf8 (object->getProperty (name.c_str()).toString());
}

juce::var Truth::listener (const std::string& program, const std::string& role) const
{
    if (auto* listeners = truth.getProperty ("listeners", juce::var()).getArray())
        for (const auto& candidate : *listeners)
            if (candidate["program"].toString() == juce::String (juce::CharPointer_UTF8 (program.c_str()))
                && candidate["role"].toString() == juce::String (juce::CharPointer_UTF8 (role.c_str())))
                return deepCopy (candidate);
    refuse ("nobody listens as " + program + "." + role);
}

int Truth::port (const std::string& program, const std::string& role) const
{
    return static_cast<int> (member (listener (program, role), "port", program + "." + role + " has no port"));
}

Endpoint Truth::endpoint (const std::string& program, const std::string& role) const
{
    const auto found = listener (program, role);
    std::string name = utf8 (member (found, "host", program + "." + role + " has no host").toString());
    if (name == "any")
        name = "local";
    return { host (name), static_cast<int> (member (found, "port", program + "." + role + " has no port")) };
}

std::vector<juce::var> Truth::routes() const
{
    std::vector<juce::var> routes;
    if (auto* items = truth.getProperty ("routes", juce::var()).getArray())
        for (const auto& item : *items)
            routes.push_back (deepCopy (item));
    return routes;
}

juce::var Truth::entry (const std::string& key) const
{
    const auto addresses = truth.getProperty ("addresses", juce::var());
    auto* object = addresses.getDynamicObject();
    if (object == nullptr || ! object->hasProperty (key.c_str()))
        refuse ("no address '" + key + "' in the truth");
    return object->getProperty (key.c_str());
}

bool Truth::addressHas (const std::string& key, const std::string& property) const
{
    const auto found = entry (key);
    auto* object = found.getDynamicObject();
    return object != nullptr && object->hasProperty (property.c_str());
}

std::string Truth::pattern (const std::string& key) const
{
    return utf8 (member (entry (key), "pattern", key + " has no pattern").toString());
}

std::pair<int, int> Truth::indexRange (const std::string& key, const std::string& field) const
{
    const auto range = member (entry (key), field.c_str(), key + " has no " + field);
    return { static_cast<int> (range[0]), static_cast<int> (range[1]) };
}

std::string Truth::address (const std::string& key, const std::map<std::string, int>& fields) const
{
    const auto found = entry (key);
    for (const auto& [field, value] : fields)
    {
        if (! found.hasProperty (field.c_str()))
            continue;
        const auto range = indexRange (key, field);
        if (value < range.first || value > range.second)
            refuse (key + ": " + field + "=" + std::to_string (value) + " outside " + std::to_string (range.first)
                    + ".." + std::to_string (range.second));
    }
    std::string text;
    for (const auto& part : splitPattern (pattern (key)))
    {
        if (! part.isField)
        {
            text += part.text;
            continue;
        }
        const auto given = fields.find (part.text);
        if (given == fields.end())
            refuse (key + ": no value for {" + part.text + "}");
        text += std::to_string (given->second);
    }
    return text;
}

std::optional<std::pair<std::string, std::map<std::string, int>>> Truth::match (const std::string& address) const
{
    juce::Array<juce::juce_wchar> text;
    for (auto c = juce::CharPointer_UTF8 (address.c_str()); ! c.isEmpty();)
        text.add (c.getAndAdvance());

    for (const auto& matcher : matchers)
    {
        Captures captures;
        if (! matchFrom (matcher.parts, 0, text, 0, captures))
            continue;
        std::map<std::string, int> fields;
        bool inRange = true;
        for (const auto& [name, digits] : captures)
        {
            const auto value = integerOf (digits);
            const auto range = indexRange (matcher.key, name);
            inRange = inRange && range.first <= value && value <= range.second;
            fields[name] = static_cast<int> (std::min<juce::int64> (value, INT_MAX));
        }
        if (inRange)
            return std::make_pair (matcher.key, fields);
    }
    return std::nullopt;
}

std::vector<std::string> Truth::vuMeters() const
{
    std::vector<std::string> names;
    if (auto* items = truth.getProperty ("vu_meters", juce::var()).getArray())
        for (const auto& item : *items)
            names.push_back (utf8 (item.toString()));
    return names;
}

Meters Truth::meters() const
{
    juce::var block (new juce::DynamicObject());
    if (truth.hasProperty (localOptionalKey))
        block = truth[localOptionalKey];
    auto* object = block.getDynamicObject();
    if (object == nullptr)
        refuse ("meters is not an object: " + pythonValue (block));

    std::vector<std::string> unknown;
    for (const auto& property : object->getProperties())
    {
        const auto name = utf8 (property.name.toString());
        const bool known = std::any_of (std::begin (meterRules), std::end (meterRules),
                                        [&] (const MeterRule& rule) { return name == rule.name; });
        if (! known && name.rfind ("_", 0) != 0)
            unknown.push_back (name);
    }
    if (! unknown.empty())
    {
        std::sort (unknown.begin(), unknown.end());
        std::string list;
        for (const auto& name : unknown)
            list += (list.empty() ? "" : ", ") + name;
        refuse ("meters has unknown keys: " + list);
    }

    double values[3];
    for (size_t i = 0; i < 3; ++i)
    {
        const auto& rule = meterRules[i];
        values[i] = object->hasProperty (rule.name) ? meterValue (rule, object->getProperty (rule.name))
                                                    : meterValue (rule, juce::var (rule.defaultValue));
    }
    return { values[0], values[1], values[2] };
}

juce::File truthPath (std::optional<juce::File> given, const char* envTruth)
{
    if (given.has_value())
        return *given;
    if (envTruth != nullptr && *envTruth != '\0')
        return juce::File (juce::String::fromUTF8 (envTruth));
    return juce::File (installedTruth);
}

std::optional<juce::File> networkPath (bool truthGiven, const juce::File& home, const char* envNetwork)
{
    if (envNetwork != nullptr && *envNetwork != '\0')
        return juce::File (juce::String::fromUTF8 (envNetwork));
    if (! truthGiven)
        return home.getChildFile (networkUnderHome);
    return std::nullopt;
}
}
