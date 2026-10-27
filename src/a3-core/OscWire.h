// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The OSC wire as python-osc 1.10.2 speaks it: what a Python value becomes on the wire, what a
// datagram becomes as Python values, and Python's float()/int() of those values.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace a3core
{
// One argument with its type tag. Value by tag: i int32; h, r, t int64 (r unsigned 32-bit, t the
// raw NTP timetag); f float, or double for a Python float not yet packed (pyFloat); d double;
// s string (UTF-8); b, m bytes (m: port, status, data1, data2); T, F, N monostate.
struct OscArg
{
    char tag;
    std::variant<std::monostate, int32_t, int64_t, float, double, std::string, std::vector<uint8_t>> value;

    bool operator== (const OscArg& other) const { return tag == other.tag && value == other.value; }
};

struct OscMessage
{
    std::string address;
    std::vector<OscArg> args;

    bool operator== (const OscMessage& other) const { return address == other.address && args == other.args; }
};

// osc_packet.OscPacket(dgram).messages: a bundle's messages flattened in order (timetags ignored),
// nullopt where python-osc refuses the packet (ParseError) or raises (bad UTF-8). An unknown type
// tag is skipped as python-osc skips it (no value, no bytes). Arrays ('[', ']') are refused: Core
// receives none, and a flat argument list cannot carry python-osc's nested list.
std::optional<std::vector<OscMessage>> decodePacket (const uint8_t* data, size_t size);

// osc_bundle.OscBundle.dgram_is_bundle: starts with "#bundle" and its NUL.
bool isBundle (const uint8_t* data, size_t size);

// osc_message.OscMessage(dgram): one message, read without asking for a leading '/'.
std::optional<OscMessage> decodeMessage (const uint8_t* data, size_t size);

// OscMessageBuilder.build().dgram. Empty where python-osc raises: an empty address or blob, a
// Python float beyond float32 (struct.pack(">f") raises OverflowError); and for a value that does
// not fit its tag.
std::vector<uint8_t> encodeMessage (const OscMessage& message);

// The tag python-osc's builder infers for a Python value (OscMessageBuilder._get_arg_type).
OscArg pyInt (long long value);       // 'i' while bit_length() <= 31, else 'h'
OscArg pyFloat (double value);        // 'f', holding the double; rounded to float32 when encoded
OscArg pyStr (std::string utf8);      // 's'
OscArg pyBool (bool value);           // 'T' / 'F'

// Python's float(x) of the value a handler receives; nullopt where Python raises.
std::optional<double> pythonFloat (const OscArg& arg);
// Python's int(x); nullopt where Python raises, and where the result does not fit a long long.
std::optional<long long> pythonInt (const OscArg& arg);
}
