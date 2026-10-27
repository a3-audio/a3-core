// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CorePython.h"

#include <algorithm>

#include "Json.h"

namespace a3core
{
namespace
{
std::string bytesRepr (const std::vector<uint8_t>& bytes)
{
    const bool single = std::find (bytes.begin(), bytes.end(), '\'') == bytes.end()
                     || std::find (bytes.begin(), bytes.end(), '"') != bytes.end();
    const char quote = single ? '\'' : '"';
    std::string out = std::string ("b") + quote;
    for (const auto byte : bytes)
    {
        if (byte == '\\' || byte == quote)
            out += std::string ("\\") + static_cast<char> (byte);
        else if (byte == '\t') out += "\\t";
        else if (byte == '\n') out += "\\n";
        else if (byte == '\r') out += "\\r";
        else if (byte < 32 || byte >= 127)
        {
            static const char* hex = "0123456789abcdef";
            out += std::string ("\\x") + hex[byte >> 4] + hex[byte & 15];
        }
        else
            out += static_cast<char> (byte);
    }
    return out + quote;
}
}

const OscArg& firstOf (const OscMessage& message)
{
    if (message.args.empty())
        throw PythonError ("IndexError");
    return message.args.front();
}

const OscArg* firstOrNone (const OscMessage& message) { return message.args.empty() ? nullptr : &message.args.front(); }

double floatOf (const OscArg& arg)
{
    if (const auto number = pythonFloat (arg))
        return *number;
    throw PythonError (arg.tag == 's' || arg.tag == 'b' ? "ValueError" : "TypeError");
}

std::string pythonStr (const OscArg& arg)
{
    switch (arg.tag)
    {
        case 's': return std::get<std::string> (arg.value);
        case 'i': return std::to_string (std::get<int32_t> (arg.value));
        case 'h': case 'r': case 't': return std::to_string (std::get<int64_t> (arg.value));
        case 'f': case 'd':
            if (const auto* single = std::get_if<float> (&arg.value))
                return pythonRepr (static_cast<double> (*single));
            return pythonRepr (std::get<double> (arg.value));
        case 'T': return "True";
        case 'F': return "False";
        case 'b': return bytesRepr (std::get<std::vector<uint8_t>> (arg.value));
        default: return "None";
    }
}

std::string firstCharacters (const std::string& text, int count)
{
    return juce::String::fromUTF8 (text.c_str()).substring (0, count).toStdString();
}
}
