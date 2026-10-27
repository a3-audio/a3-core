// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OscWire.h"

#include "Utf8.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <locale.h>
#include <stdlib.h>

namespace a3core
{
namespace
{
// ---- reading: python-osc 1.10.2 pythonosc/parsing/osc_types.py -----------------------------------

struct Refused {};   // ParseError, or a UnicodeDecodeError python-osc lets through

class Reader
{
public:
    Reader (const uint8_t* d, size_t n) : data (d), size (n) {}

    bool atEnd (size_t index) const { return index >= size; }

    // get_string: up to the NUL, padded to a multiple of 4 that must fit.
    std::string string (size_t& index) const
    {
        const auto* nul = static_cast<const uint8_t*> (index < size ? std::memchr (data + index, 0, size - index) : nullptr);
        if (nul == nullptr)
            throw Refused {};
        const size_t length = (size_t) (nul - (data + index));
        const size_t total = (length + 1 + 3) / 4 * 4;
        if (index + total > size || ! utf8::valid (data + index, length))
            throw Refused {};
        std::string out (reinterpret_cast<const char*> (data + index), length);
        index += total;
        return out;
    }

    uint64_t bigEndian (size_t& index, size_t bytes) const
    {
        if (index > size || size - index < bytes)
            throw Refused {};
        uint64_t v = 0;
        for (size_t k = 0; k < bytes; ++k)
            v = (v << 8) | data[index + k];
        index += bytes;
        return v;
    }

    int32_t int32 (size_t& index) const { return (int32_t) (uint32_t) bigEndian (index, 4); }

    // get_float pads a short float with zeros ("Reaktor doesn't send the last bunch of \x00").
    float float32 (size_t& index) const
    {
        std::array<uint8_t, 4> padded {};
        for (size_t k = 0; k < 4 && index + k < size; ++k)
            padded[k] = data[index + k];
        const uint32_t bits = (uint32_t) padded[0] << 24 | (uint32_t) padded[1] << 16 | (uint32_t) padded[2] << 8 | padded[3];
        float out;
        std::memcpy (&out, &bits, sizeof out);
        index += 4;
        return out;
    }

    double float64 (size_t& index) const
    {
        const uint64_t bits = bigEndian (index, 8);
        double out;
        std::memcpy (&out, &bits, sizeof out);
        return out;
    }

    std::vector<uint8_t> blob (size_t& index) const
    {
        const size_t start = index;
        const int32_t length = int32 (index);
        if (length < 0)   // python-osc slices with a negative end here; no sender of ours does this
            throw Refused {};
        if ((size_t) length + 4 > size - start)
            throw Refused {};
        std::vector<uint8_t> out (data + index, data + index + length);
        index += ((size_t) length + 3) / 4 * 4;
        return out;
    }

    std::vector<uint8_t> raw (size_t& index, size_t bytes) const
    {
        if (index > size || size - index < bytes)
            throw Refused {};
        std::vector<uint8_t> out (data + index, data + index + bytes);
        index += bytes;
        return out;
    }

    const uint8_t* data;
    size_t size;
};

bool startsWith (const uint8_t* data, size_t size, const char* prefix, size_t length)
{
    return size >= length && std::memcmp (data, prefix, length) == 0;
}

constexpr size_t bundlePrefixLength = 8;   // "#bundle" and its NUL

// osc_message.OscMessage._parse_datagram
OscMessage parseMessage (const uint8_t* data, size_t size)
{
    const Reader in (data, size);
    size_t index = 0;
    OscMessage message { in.string (index), {} };
    if (in.atEnd (index))
        return message;   // no parameters is legit
    const auto tags = in.string (index);
    if (tags.empty() || tags[0] != ',')
        throw Refused {};
    for (size_t t = 1; t < tags.size(); ++t)
    {
        const char tag = tags[t];
        switch (tag)
        {
            case 'i': message.args.push_back ({ tag, in.int32 (index) }); break;
            case 'h': message.args.push_back ({ tag, (int64_t) in.bigEndian (index, 8) }); break;
            case 'f': message.args.push_back ({ tag, in.float32 (index) }); break;
            case 'd': message.args.push_back ({ tag, in.float64 (index) }); break;
            case 's': message.args.push_back ({ tag, in.string (index) }); break;
            case 'b': message.args.push_back ({ tag, in.blob (index) }); break;
            case 'r': message.args.push_back ({ tag, (int64_t) in.bigEndian (index, 4) }); break;
            case 'm': message.args.push_back ({ tag, in.raw (index, 4) }); break;
            case 't': message.args.push_back ({ tag, (int64_t) in.bigEndian (index, 8) }); break;
            case 'T': case 'F': case 'N': message.args.push_back ({ tag, std::monostate {} }); break;
            case '[': case ']': throw Refused {};   // nested lists: see the header
            default: break;                        // "Unhandled parameter type": skipped, no bytes read
        }
    }
    return message;
}

// osc_bundle.OscBundle + osc_packet._timed_msg_of_bundle: the messages in document order.
void parseBundle (const uint8_t* data, size_t size, std::vector<OscMessage>& out)
{
    const Reader in (data, size);
    size_t index = bundlePrefixLength;
    in.bigEndian (index, 8);   // get_date; the timetag itself is ignored
    while (index < size)
    {
        const int32_t length = in.int32 (index);
        if (length < 0)   // python-osc would step backwards here (and may never finish)
            throw Refused {};
        const size_t take = std::min ((size_t) length, size - index);
        const uint8_t* content = data + index;
        if (isBundle (content, take))
            parseBundle (content, take, out);
        else if (take > 0 && content[0] == '/')
            out.push_back (parseMessage (content, take));
        // anything else: "Could not identify content type", skipped
        index += (size_t) length;
    }
}

// ---- writing: osc_message_builder.OscMessageBuilder.build ---------------------------------------

struct Unbuildable {};

void putString (std::vector<uint8_t>& out, const std::string& s)
{
    out.insert (out.end(), s.begin(), s.end());
    out.insert (out.end(), 4 - s.size() % 4, 0);   // always 1..4 NULs
}

void putBigEndian (std::vector<uint8_t>& out, uint64_t v, size_t bytes)
{
    for (size_t k = bytes; k-- > 0;)
        out.push_back ((uint8_t) (v >> (8 * k)));
}

uint32_t float32Bits (const OscArg& arg)
{
    float value;
    if (const auto* single = std::get_if<float> (&arg.value))
        value = *single;
    else
    {
        const double wide = std::get<double> (arg.value);
        value = (float) wide;
        if (std::isinf (value) && ! std::isinf (wide))
            throw Unbuildable {};   // struct.pack(">f") raises OverflowError
    }
    uint32_t bits;
    std::memcpy (&bits, &value, sizeof bits);
    return bits;
}

void putArg (std::vector<uint8_t>& out, const OscArg& arg)
{
    switch (arg.tag)
    {
        case 'i': putBigEndian (out, (uint32_t) std::get<int32_t> (arg.value), 4); break;
        case 'h': putBigEndian (out, (uint64_t) std::get<int64_t> (arg.value), 8); break;
        case 'f': putBigEndian (out, float32Bits (arg), 4); break;
        case 'd':
        {
            uint64_t bits;
            const double value = std::get<double> (arg.value);
            std::memcpy (&bits, &value, sizeof bits);
            putBigEndian (out, bits, 8);
            break;
        }
        case 's': putString (out, std::get<std::string> (arg.value)); break;
        case 'b':
        {
            const auto& bytes = std::get<std::vector<uint8_t>> (arg.value);
            if (bytes.empty())
                throw Unbuildable {};   // "Blob value cannot be empty"
            putBigEndian (out, bytes.size(), 4);
            out.insert (out.end(), bytes.begin(), bytes.end());
            out.insert (out.end(), (4 - bytes.size() % 4) % 4, 0);
            break;
        }
        case 'r':
        {
            const auto value = std::get<int64_t> (arg.value);
            if (value < 0 || value > 0xffffffffLL)
                throw Unbuildable {};
            putBigEndian (out, (uint64_t) value, 4);
            break;
        }
        case 'm':
        {
            const auto& bytes = std::get<std::vector<uint8_t>> (arg.value);
            if (bytes.size() != 4)
                throw Unbuildable {};
            out.insert (out.end(), bytes.begin(), bytes.end());
            break;
        }
        case 'T': case 'F': case 'N': break;
        default: throw Unbuildable {};   // "Incorrect parameter type found"
    }
}

// ---- Python's float() and int() of a str or bytes --------------------------------------------------

// The first code point of every Unicode decimal-digit run (category Nd, Unicode 16.0, Python 3.14's
// unicodedata): each run is that zero and the nine after it. ASCII '0' is handled apart.
constexpr std::array<char32_t, 75> decimalZeros {
    0x660, 0x6f0, 0x7c0, 0x966, 0x9e6, 0xa66, 0xae6, 0xb66, 0xbe6, 0xc66, 0xce6, 0xd66, 0xde6, 0xe50, 0xed0,
    0xf20, 0x1040, 0x1090, 0x17e0, 0x1810, 0x1946, 0x19d0, 0x1a80, 0x1a90, 0x1b50, 0x1bb0, 0x1c40, 0x1c50,
    0xa620, 0xa8d0, 0xa900, 0xa9d0, 0xa9f0, 0xaa50, 0xabf0, 0xff10, 0x104a0, 0x10d30, 0x10d40, 0x11066,
    0x110f0, 0x11136, 0x111d0, 0x112f0, 0x11450, 0x114d0, 0x11650, 0x116c0, 0x116d0, 0x116da, 0x11730,
    0x118e0, 0x11950, 0x11bf0, 0x11c50, 0x11d50, 0x11da0, 0x11f50, 0x16130, 0x16a60, 0x16ac0, 0x16b50,
    0x16d70, 0x1ccf0, 0x1d7ce, 0x1d7d8, 0x1d7e2, 0x1d7ec, 0x1d7f6, 0x1e140, 0x1e2f0, 0x1e4f0, 0x1e5f1,
    0x1e950, 0x1fbf0,
};

int unicodeDecimal (char32_t cp)
{
    for (const auto zero : decimalZeros)
        if (cp >= zero && cp < zero + 10)
            return (int) (cp - zero);
    return -1;
}

bool unicodeSpace (char32_t cp)   // str.isspace() above ASCII
{
    return cp == 0x85 || cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x2028
        || cp == 0x2029 || cp == 0x202f || cp == 0x205f || cp == 0x3000;
}

// _PyUnicode_TransformDecimalAndSpaceToASCII: ASCII stays, other whitespace becomes ' ', other
// decimal digits their ASCII digit, anything else '?'. nullopt: not UTF-8 (no Python str is).
std::optional<std::string> asciiOfText (const std::string& text)
{
    const auto points = utf8::codePoints (text);
    if (! points)
        return std::nullopt;
    std::string out;
    for (const char32_t cp : *points)
    {
        if (cp < 0x7f)
            out += (char) cp;
        else if (unicodeSpace (cp))
            out += ' ';
        else if (const int digit = unicodeDecimal (cp); digit >= 0)
            out += (char) ('0' + digit);
        else
            out += '?';
    }
    return out;
}

bool asciiSpace (char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }   // Py_ISSPACE
bool asciiDigit (char c) { return c >= '0' && c <= '9'; }

std::string stripped (const std::string& s)
{
    size_t begin = 0, end = s.size();
    while (begin < end && asciiSpace (s[begin])) ++begin;
    while (end > begin && asciiSpace (s[end - 1])) --end;
    return s.substr (begin, end - begin);
}

// _Py_string_to_number_with_underscores: every '_' between two digits, then dropped.
std::optional<std::string> withoutUnderscores (const std::string& s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] != '_')
        {
            out += s[i];
            continue;
        }
        if (i == 0 || ! asciiDigit (s[i - 1]) || i + 1 >= s.size() || ! asciiDigit (s[i + 1]))
            return std::nullopt;
    }
    return out;
}

bool equalsIgnoringCase (const std::string& a, const char* b)
{
    if (a.size() != std::strlen (b)) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower ((unsigned char) a[i]) != b[i]) return false;
    return true;
}

// float_from_string_inner + PyOS_string_to_double (no overflow exception: out of range is +-inf).
std::optional<double> floatOfAscii (const std::string& raw)
{
    const auto joined = withoutUnderscores (raw);
    if (! joined)
        return std::nullopt;
    const auto s = stripped (*joined);
    size_t i = 0;
    const bool negative = ! s.empty() && s[0] == '-';
    if (! s.empty() && (s[0] == '+' || s[0] == '-')) i = 1;
    const auto word = s.substr (i);
    if (equalsIgnoringCase (word, "inf") || equalsIgnoringCase (word, "infinity"))
        return negative ? -HUGE_VAL : HUGE_VAL;
    if (equalsIgnoringCase (word, "nan"))
        return negative ? -std::nan ("") : std::nan ("");

    size_t intDigits = 0, fracDigits = 0;
    while (i < s.size() && asciiDigit (s[i])) { ++i; ++intDigits; }
    if (i < s.size() && s[i] == '.')
        for (++i; i < s.size() && asciiDigit (s[i]); ++i) ++fracDigits;
    if (intDigits + fracDigits == 0)
        return std::nullopt;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
    {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        size_t expDigits = 0;
        while (i < s.size() && asciiDigit (s[i])) { ++i; ++expDigits; }
        if (expDigits == 0)
            return std::nullopt;
    }
    if (i != s.size())
        return std::nullopt;
    static const locale_t c = newlocale (LC_NUMERIC_MASK, "C", (locale_t) 0);
    return strtod_l (s.c_str(), nullptr, c);   // correctly rounded, as _Py_dg_strtod
}

// PyLong_FromString(base 10): space, sign, digits with single '_' between them, space.
std::optional<long long> intOfAscii (const std::string& raw)
{
    const auto s = stripped (raw);
    size_t i = 0;
    const bool negative = ! s.empty() && s[0] == '-';
    if (! s.empty() && (s[0] == '+' || s[0] == '-')) i = 1;
    if (i >= s.size() || ! asciiDigit (s[i]))
        return std::nullopt;
    unsigned long long magnitude = 0;
    const unsigned long long limit = negative ? 9223372036854775808ULL : 9223372036854775807ULL;
    for (; i < s.size(); ++i)
    {
        if (s[i] == '_')
        {
            if (i + 1 >= s.size() || ! asciiDigit (s[i + 1]))
                return std::nullopt;
            continue;
        }
        if (! asciiDigit (s[i]))
            return std::nullopt;
        const unsigned digit = (unsigned) (s[i] - '0');
        if (magnitude > (limit - digit) / 10)
            return std::nullopt;   // a Python int beyond a long long
        magnitude = magnitude * 10 + digit;
    }
    return negative ? (long long) (0 - magnitude) : (long long) magnitude;
}

std::optional<long long> intOfDouble (double value)   // int(float): truncation
{
    if (! std::isfinite (value))
        return std::nullopt;   // ValueError (nan) or OverflowError (inf)
    const double truncated = std::trunc (value);
    if (truncated >= 9223372036854775808.0 || truncated < -9223372036854775808.0)
        return std::nullopt;
    return (long long) truncated;
}

std::string textOfBytes (const OscArg& arg)
{
    const auto& bytes = std::get<std::vector<uint8_t>> (arg.value);
    return { bytes.begin(), bytes.end() };
}
}

bool isBundle (const uint8_t* data, size_t size) { return startsWith (data, size, "#bundle", bundlePrefixLength); }

std::optional<std::vector<OscMessage>> decodePacket (const uint8_t* data, size_t size)
{
    try
    {
        std::vector<OscMessage> out;
        if (isBundle (data, size))
            parseBundle (data, size, out);
        else if (size > 0 && data[0] == '/')
            out.push_back (parseMessage (data, size));
        else
            return std::nullopt;   // "should at least contain an OscMessage or an OscBundle"
        return out;
    }
    catch (const Refused&)
    {
        return std::nullopt;
    }
}

std::optional<OscMessage> decodeMessage (const uint8_t* data, size_t size)
{
    try
    {
        return parseMessage (data, size);
    }
    catch (const Refused&)
    {
        return std::nullopt;
    }
}

std::vector<uint8_t> encodeMessage (const OscMessage& message)
{
    if (message.address.empty())
        return {};   // "OSC addresses cannot be empty"
    try
    {
        std::vector<uint8_t> out;
        putString (out, message.address);
        std::string tags = ",";
        for (const auto& arg : message.args)
            tags += arg.tag;
        putString (out, tags);
        for (const auto& arg : message.args)
            putArg (out, arg);
        return out;
    }
    catch (const Unbuildable&)
    {
        return {};
    }
    catch (const std::bad_variant_access&)   // a value that does not fit its tag
    {
        return {};
    }
}

OscArg pyInt (long long value)
{
    if (value > -2147483648LL && value < 2147483648LL)
        return { 'i', (int32_t) value };
    return { 'h', (int64_t) value };
}

OscArg pyFloat (double value) { return { 'f', value }; }
OscArg pyStr (std::string utf8) { return { 's', std::move (utf8) }; }
OscArg pyBool (bool value) { return { value ? 'T' : 'F', std::monostate {} }; }

std::optional<double> pythonFloat (const OscArg& arg)
{
    switch (arg.tag)
    {
        case 'i': return (double) std::get<int32_t> (arg.value);
        case 'h': case 'r': return (double) std::get<int64_t> (arg.value);
        case 'f':
            if (const auto* single = std::get_if<float> (&arg.value))
                return (double) *single;
            return std::get<double> (arg.value);
        case 'd': return std::get<double> (arg.value);
        case 'T': return 1.0;
        case 'F': return 0.0;
        case 's':
        {
            const auto ascii = asciiOfText (std::get<std::string> (arg.value));
            return ascii ? floatOfAscii (*ascii) : std::nullopt;
        }
        case 'b': return floatOfAscii (textOfBytes (arg));
        default: return std::nullopt;   // None, a MIDI tuple, a timetag: TypeError
    }
}

std::optional<long long> pythonInt (const OscArg& arg)
{
    switch (arg.tag)
    {
        case 'i': return std::get<int32_t> (arg.value);
        case 'h': case 'r': return std::get<int64_t> (arg.value);
        case 'f': case 'd': return intOfDouble (*pythonFloat (arg));
        case 'T': return 1;
        case 'F': return 0;
        case 's':
        {
            const auto ascii = asciiOfText (std::get<std::string> (arg.value));
            return ascii ? intOfAscii (*ascii) : std::nullopt;
        }
        case 'b': return intOfAscii (textOfBytes (arg));
        default: return std::nullopt;
    }
}
}
