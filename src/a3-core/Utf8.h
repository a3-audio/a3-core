// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// UTF-8 as Python reads it: the strict decoder's yes/no, and the code points of valid text.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace a3core::utf8
{
// bytes.decode("utf-8") succeeds: no overlongs, no surrogates, nothing above U+10FFFF.
inline bool valid (const uint8_t* s, size_t n)
{
    size_t i = 0;
    while (i < n)
    {
        const uint8_t c = s[i];
        if (c < 0x80) { ++i; continue; }
        size_t extra;
        uint32_t cp, min;
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; cp = c & 0x1fu; min = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; cp = c & 0x0fu; min = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; cp = c & 0x07u; min = 0x10000; }
        else return false;
        if (n - i <= extra) return false;
        for (size_t k = 1; k <= extra; ++k)
        {
            if ((s[i + k] & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (s[i + k] & 0x3fu);
        }
        if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
        i += extra + 1;
    }
    return true;
}

// The code points of a Python str; nullopt for bytes that are no UTF-8 (no Python str is).
inline std::optional<std::u32string> codePoints (const std::string& text)
{
    const auto* s = reinterpret_cast<const uint8_t*> (text.data());
    if (! valid (s, text.size()))
        return std::nullopt;
    std::u32string out;
    for (size_t i = 0; i < text.size();)
    {
        const uint8_t c = s[i];
        const size_t extra = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        char32_t cp = extra == 0 ? c : (char32_t) (c & (0x3f >> extra));
        for (size_t k = 1; k <= extra; ++k)
            cp = (cp << 6) | (s[i + k] & 0x3fu);
        out += cp;
        i += extra + 1;
    }
    return out;
}
}
