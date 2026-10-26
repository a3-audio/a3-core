// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Json.h"

#include <juce_cryptography/juce_cryptography.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <locale.h>
#include <stdlib.h>
#include <utility>
#include <vector>

namespace a3core
{
namespace
{
struct Style
{
    int indent;          // < 0: compact, separators "," and ":"
    bool sortKeys;
    bool asciiOnly;
};

void appendHex4 (std::string& out, unsigned unit)
{
    static const char* digits = "0123456789abcdef";
    out += "\\u";
    for (int shift = 12; shift >= 0; shift -= 4)
        out += digits[(unit >> shift) & 0xF];
}

// Decodes one UTF-8 sequence at `i` (the input comes from juce::String, so it is well-formed).
unsigned nextCodePoint (const std::string& text, size_t& i)
{
    const auto lead = static_cast<unsigned char> (text[i++]);
    if (lead < 0x80)
        return lead;

    const int extra = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : 1;
    unsigned code = lead & (0x3F >> extra);
    for (int k = 0; k < extra && i < text.size(); ++k)
        code = (code << 6) | (static_cast<unsigned char> (text[i++]) & 0x3F);
    return code;
}

void appendString (std::string& out, const juce::String& value, bool asciiOnly)
{
    const std::string text = value.toStdString();
    out += '"';
    for (size_t i = 0; i < text.size();)
    {
        const size_t start = i;
        const unsigned code = nextCodePoint (text, i);
        switch (code)
        {
            case '"':  out += "\\\""; continue;
            case '\\': out += "\\\\"; continue;
            case '\n': out += "\\n";  continue;
            case '\r': out += "\\r";  continue;
            case '\t': out += "\\t";  continue;
            case '\b': out += "\\b";  continue;
            case '\f': out += "\\f";  continue;
            default: break;
        }
        if (code < 0x20)
            appendHex4 (out, code);
        else if (asciiOnly && code > 0x7E)
        {
            if (code >= 0x10000)
            {
                const unsigned offset = code - 0x10000;
                appendHex4 (out, 0xD800 + (offset >> 10));
                appendHex4 (out, 0xDC00 + (offset & 0x3FF));
            }
            else
                appendHex4 (out, code);
        }
        else
            out.append (text, start, i - start);
    }
    out += '"';
}

std::string reprOfFinite (double value)
{
    char buffer[64];
    const auto result = std::to_chars (buffer, buffer + sizeof buffer, value, std::chars_format::scientific);
    const std::string sci (buffer, result.ptr);

    const size_t ePos = sci.find ('e');
    std::string mantissa = sci.substr (0, ePos);
    const int exponent = std::stoi (sci.substr (ePos + 1));

    std::string sign;
    if (mantissa[0] == '-')
    {
        sign = "-";
        mantissa.erase (0, 1);
    }
    std::string digits;
    for (char c : mantissa)
        if (c != '.')
            digits += c;

    if (exponent >= -4 && exponent < 16)
    {
        std::string fixed;
        if (exponent < 0)
            fixed = "0." + std::string (static_cast<size_t> (-exponent - 1), '0') + digits;
        else if (digits.size() <= static_cast<size_t> (exponent) + 1)
            fixed = digits + std::string (static_cast<size_t> (exponent) + 1 - digits.size(), '0') + ".0";
        else
            fixed = digits.substr (0, static_cast<size_t> (exponent) + 1) + "." + digits.substr (static_cast<size_t> (exponent) + 1);
        return sign + fixed;
    }

    std::string out = sign + digits.substr (0, 1);
    if (digits.size() > 1)
        out += "." + digits.substr (1);
    const int magnitude = std::abs (exponent);
    out += exponent < 0 ? "e-" : "e+";
    if (magnitude < 10)
        out += '0';
    return out + std::to_string (magnitude);
}

// json.dumps writes the non-finite floats as bare words, unlike repr().
std::string jsonNumber (double value)
{
    if (std::isnan (value))
        return "NaN";
    if (std::isinf (value))
        return value < 0 ? "-Infinity" : "Infinity";
    return reprOfFinite (value);
}

class Writer
{
public:
    explicit Writer (Style styleToUse) : style (styleToUse) {}

    std::string write (const juce::var& value)
    {
        out.clear();
        append (value, 0);
        return std::move (out);
    }

private:
    using Member = std::pair<juce::String, juce::var>;

    void newline (int depth)
    {
        if (style.indent < 0)
            return;
        out += '\n';
        out.append (static_cast<size_t> (depth * style.indent), ' ');
    }

    void append (const juce::var& value, int depth)
    {
        if (value.isVoid() || value.isUndefined())
            out += "null";
        else if (value.isBool())
            out += static_cast<bool> (value) ? "true" : "false";
        else if (value.isInt() || value.isInt64())
            out += std::to_string (static_cast<juce::int64> (value));
        else if (value.isDouble())
            out += jsonNumber (static_cast<double> (value));
        else if (value.isString())
            appendString (out, value.toString(), style.asciiOnly);
        else if (auto* array = value.getArray())
            appendArray (*array, depth);
        else if (auto* object = value.getDynamicObject())
            appendObject (*object, depth);
        else
            out += "null";
    }

    void appendArray (const juce::Array<juce::var>& array, int depth)
    {
        if (array.isEmpty())
        {
            out += "[]";
            return;
        }
        out += '[';
        for (int i = 0; i < array.size(); ++i)
        {
            if (i > 0)
                out += ',';
            newline (depth + 1);
            append (array.getReference (i), depth + 1);
        }
        newline (depth);
        out += ']';
    }

    void appendObject (const juce::DynamicObject& object, int depth)
    {
        std::vector<Member> members;
        for (const auto& property : object.getProperties())
            members.emplace_back (property.name.toString(), property.value);
        if (members.empty())
        {
            out += "{}";
            return;
        }
        if (style.sortKeys)
            std::stable_sort (members.begin(), members.end(), [] (const Member& a, const Member& b)
                              { return a.first.toStdString() < b.first.toStdString(); });

        out += '{';
        for (size_t i = 0; i < members.size(); ++i)
        {
            if (i > 0)
                out += ',';
            newline (depth + 1);
            appendString (out, members[i].first, style.asciiOnly);
            out += style.indent < 0 ? ":" : ": ";
            append (members[i].second, depth + 1);
        }
        newline (depth);
        out += '}';
    }

    Style style;
    std::string out;
};
}

namespace
{
struct ParseError
{
    std::string message;
    size_t offset;
};

// JUCE's own parser reads floats with a hand-rolled accumulator: 17-digit values and denormals
// come out a few ulp off, which would change the fingerprint. This one hands the number text to
// std::from_chars, so a double is exactly what Python's json reads. Strictness follows Python's.
class Parser
{
public:
    explicit Parser (const std::string& textToParse) : text (textToParse) {}

    juce::var parseDocument()
    {
        auto value = parseValue();
        skipSpace();
        if (pos != text.size())
            fail ("Extra data");
        return value;
    }

private:
    [[noreturn]] void fail (const std::string& message) const { throw ParseError { message, pos }; }

    void skipSpace()
    {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
            ++pos;
    }

    bool take (char expected)
    {
        if (pos >= text.size() || text[pos] != expected)
            return false;
        ++pos;
        return true;
    }

    bool takeWord (const char* word)
    {
        const std::string wanted (word);
        if (text.compare (pos, wanted.size(), wanted) != 0)
            return false;
        pos += wanted.size();
        return true;
    }

    juce::var parseValue()
    {
        skipSpace();
        if (pos >= text.size())
            fail ("Expecting value");
        const char c = text[pos];
        if (c == '{')
            return parseObject();
        if (c == '[')
            return parseArray();
        if (c == '"')
            return juce::var (juce::String::fromUTF8 (parseString().c_str()));
        if (takeWord ("true"))
            return juce::var (true);
        if (takeWord ("false"))
            return juce::var (false);
        if (takeWord ("null"))
            return juce::var();
        if (takeWord ("NaN"))
            return juce::var (std::nan (""));
        if (takeWord ("Infinity"))
            return juce::var (HUGE_VAL);
        if (takeWord ("-Infinity"))
            return juce::var (-HUGE_VAL);
        if (c == '-' || (c >= '0' && c <= '9'))
            return parseNumber();
        fail ("Expecting value");
    }

    juce::var parseObject()
    {
        ++pos;
        auto* object = new juce::DynamicObject();
        juce::var result (object);
        skipSpace();
        if (take ('}'))
            return result;
        for (;;)
        {
            skipSpace();
            if (pos >= text.size() || text[pos] != '"')
                fail ("Expecting property name enclosed in double quotes");
            const auto key = parseString();
            skipSpace();
            if (! take (':'))
                fail ("Expecting ':' delimiter");
            object->setProperty (juce::Identifier (juce::String::fromUTF8 (key.c_str())), parseValue());
            skipSpace();
            if (take ('}'))
                return result;
            if (! take (','))
                fail ("Expecting ',' delimiter");
        }
    }

    juce::var parseArray()
    {
        ++pos;
        juce::Array<juce::var> items;
        skipSpace();
        if (take (']'))
            return juce::var (items);
        for (;;)
        {
            items.add (parseValue());
            skipSpace();
            if (take (']'))
                return juce::var (items);
            if (! take (','))
                fail ("Expecting ',' delimiter");
        }
    }

    unsigned parseHex4()
    {
        if (pos + 4 > text.size())
            fail ("Invalid \\uXXXX escape");
        unsigned code = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text[pos++];
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0)
                fail ("Invalid \\uXXXX escape");
            code = code * 16 + static_cast<unsigned> (digit);
        }
        return code;
    }

    static void appendUtf8 (std::string& out, unsigned code)
    {
        if (code < 0x80)
            out += static_cast<char> (code);
        else if (code < 0x800)
        {
            out += static_cast<char> (0xC0 | (code >> 6));
            out += static_cast<char> (0x80 | (code & 0x3F));
        }
        else if (code < 0x10000)
        {
            out += static_cast<char> (0xE0 | (code >> 12));
            out += static_cast<char> (0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (code & 0x3F));
        }
        else
        {
            out += static_cast<char> (0xF0 | (code >> 18));
            out += static_cast<char> (0x80 | ((code >> 12) & 0x3F));
            out += static_cast<char> (0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char> (0x80 | (code & 0x3F));
        }
    }

    std::string parseString()
    {
        ++pos;
        std::string out;
        for (;;)
        {
            if (pos >= text.size())
                fail ("Unterminated string");
            const char c = text[pos++];
            if (c == '"')
                return out;
            if (static_cast<unsigned char> (c) < 0x20)
                fail ("Invalid control character in string");
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos >= text.size())
                fail ("Unterminated string");
            const char escape = text[pos++];
            switch (escape)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': appendUnicodeEscape (out); break;
                default: --pos; fail ("Invalid \\escape");
            }
        }
    }

    void appendUnicodeEscape (std::string& out)
    {
        unsigned code = parseHex4();
        if (code >= 0xD800 && code < 0xDC00 && text.compare (pos, 2, "\\u") == 0)
        {
            const size_t afterHigh = pos;
            pos += 2;
            const unsigned low = parseHex4();
            if (low >= 0xDC00 && low < 0xE000)
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
            else
                pos = afterHigh;
        }
        if (code >= 0xD800 && code < 0xE000)
            fail ("Unpaired surrogate in string");   // juce::String cannot hold one
        if (code == 0)
            fail ("NUL in string");   // nor a NUL: it would end the string early
        appendUtf8 (out, code);
    }

    juce::var parseNumber()
    {
        const size_t start = pos;
        take ('-');
        if (take ('0'))
        {
        }
        else if (! digits())
            fail ("Expecting value");

        bool isFloat = false;
        if (pos < text.size() && text[pos] == '.')
        {
            ++pos;
            if (! digits())
                fail ("Expecting digits after '.'");
            isFloat = true;
        }
        if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E'))
        {
            ++pos;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
                ++pos;
            if (! digits())
                fail ("Expecting digits in exponent");
            isFloat = true;
        }

        const char* first = text.data() + start;
        const char* last = text.data() + pos;
        if (isFloat)
        {
            double number = 0;
            if (std::from_chars (first, last, number).ec == std::errc::result_out_of_range)   // leaves `number` untouched
                number = readOutOfRange (std::string (first, last));
            return juce::var (number);
        }
        juce::int64 integer = 0;
        if (std::from_chars (first, last, integer).ec != std::errc())
            fail ("Integer outside 64 bits");
        if (integer >= INT32_MIN && integer <= INT32_MAX)
            return juce::var (static_cast<int> (integer));
        return juce::var (integer);
    }

    // strtod saturates to +-HUGE_VAL or +-0.0 by the number's magnitude, as Python's float() does.
    static double readOutOfRange (const std::string& token)
    {
        static const locale_t cLocale = newlocale (LC_ALL_MASK, "C", nullptr);
        return strtod_l (token.c_str(), nullptr, cLocale);
    }

    bool digits()
    {
        const size_t before = pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
            ++pos;
        return pos > before;
    }

    const std::string& text;
    size_t pos = 0;
};
}

juce::var parseJson (const juce::String& text, juce::String& error)
{
    error = juce::String();
    const std::string utf8 = text.toStdString();
    try
    {
        return Parser (utf8).parseDocument();
    }
    catch (const ParseError& problem)
    {
        error = juce::String (problem.message) + " at byte " + juce::String (static_cast<juce::int64> (problem.offset));
        return juce::var();
    }
}

std::string canonicalBytes (const juce::var& value)
{
    return Writer ({ -1, true, false }).write (value);
}

std::string sha256Hex (const std::string& bytes)
{
    return juce::SHA256 (bytes.data(), bytes.size()).toHexString().toStdString();
}

std::string pythonRepr (double value)
{
    if (std::isnan (value))
        return "nan";
    if (std::isinf (value))
        return value < 0 ? "-inf" : "inf";
    return reprOfFinite (value);
}

std::string dumpIndented (const juce::var& value, int indent)
{
    return Writer ({ indent, false, true }).write (value);
}
}
