// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Reproduces python-osc 1.10.2's pythonosc/dispatcher.py, Dispatcher.handlers_for_address: the
// incoming address pattern is turned into a regex ("*" -> "[^/]*", "?" -> "[^/]", "[!" -> "[^",
// "{a,b}" -> "(a|b)", other metacharacters escaped) and re.match'ed against every mapped address;
// a mapped address holding '*' is also read as the regex addr.replace("*", ".*?") + "$" and
// re.match'ed against the incoming one. Every match in map order, the default only when nothing
// matched; a pattern that does not compile reaches nothing, not even the default.
//
// The regexes are matched by a small engine for the part of Python's `re` syntax those two forms
// can produce: literals, escapes of non-alphanumerics, '.', classes (with Python's rules: a ']' first
// is literal, ranges, '^' negation), groups with '|', '*' '+' '?' (lazy or not), '^' and '$' ('$'
// also before a final newline). Anything else (\w, {m,n} repeats, (?...) groups) is refused.
#include "Dispatcher.h"

#include <stdexcept>

namespace a3core
{
namespace
{
struct Unsupported {};   // re.error, or syntax beyond the subset

std::u32string codePoints (const std::string& utf8)
{
    std::u32string out;
    const auto* s = reinterpret_cast<const unsigned char*> (utf8.data());
    for (size_t i = 0; i < utf8.size();)
    {
        const unsigned char c = s[i];
        const size_t extra = c < 0x80 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        char32_t cp = extra == 0 ? c : (char32_t) (c & (0x3f >> extra));
        for (size_t k = 1; k <= extra && i + k < utf8.size(); ++k)
            cp = (cp << 6) | (s[i + k] & 0x3fu);
        out += cp;
        i += extra + 1;
    }
    return out;
}

bool isAsciiAlnum (char32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// ---- the regex: parse into nodes ---------------------------------------------------------------

struct Node;
using Sequence = std::vector<Node>;

struct Node
{
    enum class Kind { Literal, Any, Class, Group, Start, End } kind = Kind::Literal;
    char32_t literal = 0;
    bool negated = false;
    std::vector<std::pair<char32_t, char32_t>> ranges;   // Class
    std::vector<Sequence> alternatives;                   // Group
    int min = 1, max = 1;                                  // max -1: unbounded
};

Node nodeOf (Node::Kind kind)
{
    Node node;
    node.kind = kind;
    return node;
}

class Parser
{
public:
    explicit Parser (std::u32string text) : s (std::move (text)) {}

    std::vector<Sequence> parse()
    {
        auto top = alternation();
        if (i != s.size())
            throw Unsupported {};   // "unbalanced parenthesis"
        return top;
    }

private:
    std::vector<Sequence> alternation()
    {
        std::vector<Sequence> alternatives { sequence() };
        while (i < s.size() && s[i] == '|')
        {
            ++i;
            alternatives.push_back (sequence());
        }
        return alternatives;
    }

    Sequence sequence()
    {
        Sequence out;
        while (i < s.size() && s[i] != '|' && s[i] != ')')
        {
            const char32_t c = s[i];
            if (c == '*' || c == '+' || c == '?')
            {
                if (out.empty() || out.back().kind == Node::Kind::Start || out.back().kind == Node::Kind::End
                    || out.back().min != 1 || out.back().max != 1)
                    throw Unsupported {};   // "nothing to repeat" / "multiple repeat"
                ++i;
                out.back().min = c == '+' ? 1 : 0;
                out.back().max = c == '?' ? 1 : -1;
                if (i < s.size() && s[i] == '?')
                    ++i;   // lazy: the same yes/no for a match
                else if (i < s.size() && s[i] == '+')
                    throw Unsupported {};   // possessive
                continue;
            }
            out.push_back (atom());
        }
        return out;
    }

    Node atom()
    {
        const char32_t c = s[i++];
        switch (c)
        {
            case '^': return nodeOf (Node::Kind::Start);
            case '$': return nodeOf (Node::Kind::End);
            case '.': return nodeOf (Node::Kind::Any);
            case '\\': return literalNode (escaped());
            case '[': return characterClass();
            case '(':
            {
                if (i < s.size() && s[i] == '?')
                    throw Unsupported {};
                Node group = nodeOf (Node::Kind::Group);
                group.alternatives = alternation();
                if (i >= s.size() || s[i] != ')')
                    throw Unsupported {};   // "missing ), unterminated subpattern"
                ++i;
                return group;
            }
            case '{':
                if (looksLikeRepeat())
                    throw Unsupported {};
                return literalNode (c);
            default: return literalNode (c);
        }
    }

    static Node literalNode (char32_t c)
    {
        Node node = nodeOf (Node::Kind::Literal);
        node.literal = c;
        return node;
    }

    char32_t escaped()
    {
        if (i >= s.size() || isAsciiAlnum (s[i]))
            throw Unsupported {};   // "bad escape (end of pattern)", or \w, \d, ...
        return s[i++];
    }

    // sre_parse: "{m}", "{m,}", "{,n}", "{m,n}" repeat; any other '{' is a literal.
    bool looksLikeRepeat() const
    {
        size_t k = i;
        bool digits = false;
        while (k < s.size() && s[k] >= '0' && s[k] <= '9') { ++k; digits = true; }
        if (k < s.size() && s[k] == ',')
            for (++k; k < s.size() && s[k] >= '0' && s[k] <= '9'; ++k) digits = true;
        return digits && k < s.size() && s[k] == '}';
    }

    Node characterClass()
    {
        Node node = nodeOf (Node::Kind::Class);
        if (i < s.size() && s[i] == '^')
        {
            node.negated = true;
            ++i;
        }
        const size_t start = i;
        while (true)
        {
            if (i >= s.size())
                throw Unsupported {};   // "unterminated character set"
            char32_t c = s[i++];
            if (c == ']' && i - 1 != start)
                break;
            if (c == '\\')
                c = escaped();
            if (i < s.size() && s[i] == '-')
            {
                ++i;
                if (i >= s.size())
                    throw Unsupported {};
                char32_t hi = s[i++];
                if (hi == ']')
                {
                    node.ranges.push_back ({ c, c });
                    node.ranges.push_back ({ '-', '-' });
                    break;
                }
                if (hi == '\\')
                    hi = escaped();
                if (hi < c)
                    throw Unsupported {};   // "bad character range"
                node.ranges.push_back ({ c, hi });
                continue;
            }
            node.ranges.push_back ({ c, c });
        }
        return node;
    }

    std::u32string s;
    size_t i = 0;
};

// ---- the regex: re.match (anchored at the start, not at the end) ------------------------------

class Matcher
{
public:
    Matcher (const std::vector<Sequence>& top, const std::u32string& text) : regex (top), s (text) {}

    bool matches() const
    {
        return alternatives (regex, 0, [] (size_t) { return true; });
    }

private:
    using Rest = std::function<bool (size_t)>;

    bool alternatives (const std::vector<Sequence>& alts, size_t at, const Rest& rest) const
    {
        for (const auto& seq : alts)
            if (sequence (seq, 0, at, rest))
                return true;
        return false;
    }

    bool sequence (const Sequence& seq, size_t node, size_t at, const Rest& rest) const
    {
        if (node == seq.size())
            return rest (at);
        const auto next = [&] (size_t after) { return sequence (seq, node + 1, after, rest); };
        return repeated (seq[node], 0, at, next);
    }

    // Any count from min to max; greedy or lazy gives the same answer to "is there a match".
    bool repeated (const Node& n, int count, size_t at, const Rest& rest) const
    {
        if (count >= n.min && rest (at))
            return true;
        if (n.max != -1 && count >= n.max)
            return false;
        return once (n, at, [&] (size_t after) {
            if (after == at && count >= n.min)
                return false;   // an empty pass would loop forever
            return repeated (n, count + 1, after, rest);
        });
    }

    bool once (const Node& n, size_t at, const Rest& rest) const
    {
        switch (n.kind)
        {
            case Node::Kind::Literal: return at < s.size() && s[at] == n.literal && rest (at + 1);
            case Node::Kind::Any: return at < s.size() && s[at] != '\n' && rest (at + 1);
            case Node::Kind::Class:
            {
                if (at >= s.size())
                    return false;
                bool inside = false;
                for (const auto& [lo, hi] : n.ranges)
                    inside = inside || (s[at] >= lo && s[at] <= hi);
                return inside != n.negated && rest (at + 1);
            }
            case Node::Kind::Group: return alternatives (n.alternatives, at, rest);
            case Node::Kind::Start: return at == 0 && rest (at);
            case Node::Kind::End: return (at == s.size() || (at + 1 == s.size() && s[at] == '\n')) && rest (at);
        }
        return false;
    }

    const std::vector<Sequence>& regex;
    const std::u32string& s;
};

// handlers_for_address's conversion of the incoming address pattern, character for character.
std::u32string regexOfAddressPattern (const std::u32string& p)
{
    static const std::u32string metaInClass = U"\\^$.|()+*?";
    static const std::u32string metaInGroup = U"\\^$.|()[]+*?";
    static const std::u32string metaOutside = U"\\^$.|()[]+?";
    std::u32string out = U"^";
    for (size_t i = 0; i < p.size(); ++i)
    {
        const char32_t c = p[i];
        if (c == '*')
            out += U"[^/]*";
        else if (c == '?')
            out += U"[^/]";
        else if (c == '[')
        {
            out += U"[";
            ++i;
            if (i < p.size() && p[i] == '!')
            {
                out += U"^";
                ++i;
            }
            for (; i < p.size() && p[i] != ']'; ++i)
            {
                if (metaInClass.find (p[i]) != std::u32string::npos)
                    out += U"\\";
                out += p[i];
            }
            out += U"]";
        }
        else if (c == '{')
        {
            out += U"(";
            for (++i; i < p.size() && p[i] != '}'; ++i)
            {
                if (p[i] == ',')
                    out += U"|";
                else
                {
                    if (metaInGroup.find (p[i]) != std::u32string::npos)
                        out += U"\\";
                    out += p[i];
                }
            }
            out += U")";
        }
        else
        {
            if (metaOutside.find (c) != std::u32string::npos)
                out += U"\\";
            out += c;
        }
    }
    return out + U"$";
}

std::u32string regexOfMappedWildcard (const std::u32string& mapped)
{
    std::u32string out;
    for (const char32_t c : mapped)
        out += c == '*' ? std::u32string (U".*?") : std::u32string (1, c);
    return out + U"$";
}
}

struct Dispatcher::Entry
{
    std::string pattern;
    std::u32string text;
    std::optional<std::vector<Sequence>> wildcard;   // the mapped address read as a regex, if it has '*'
    std::vector<Handler> handlers;
};

Dispatcher::Dispatcher() = default;
Dispatcher::~Dispatcher() = default;

void Dispatcher::map (std::string pattern, Handler handler)
{
    for (auto& entry : entries)
        if (entry->pattern == pattern)
        {
            entry->handlers.push_back (std::move (handler));
            return;
        }
    auto entry = std::make_unique<Entry>();
    entry->text = codePoints (pattern);
    if (pattern.find ('*') != std::string::npos)
    {
        try
        {
            entry->wildcard = Parser (regexOfMappedWildcard (entry->text)).parse();
        }
        catch (const Unsupported&)
        {
            throw std::invalid_argument ("python-osc cannot read the mapped address as a regex: " + pattern);
        }
    }
    entry->pattern = std::move (pattern);
    entry->handlers.push_back (std::move (handler));
    entries.push_back (std::move (entry));
}

void Dispatcher::setDefault (Handler handler) { fallback = std::move (handler); }

void Dispatcher::dispatch (const Endpoint& from, const OscMessage& message) const
{
    const auto address = codePoints (message.address);
    std::vector<Sequence> incoming;
    try
    {
        incoming = Parser (regexOfAddressPattern (address)).parse();
    }
    catch (const Unsupported&)
    {
        return;   // re.error: "it won't match anything" -- and the generator ends before the default
    }
    bool matched = false;
    for (const auto& entry : entries)
    {
        if (! Matcher (incoming, entry->text).matches() && ! (entry->wildcard && Matcher (*entry->wildcard, address).matches()))
            continue;
        matched = true;
        for (const auto& handler : entry->handlers)
            handler (from, message);
    }
    if (! matched && fallback)
        fallback (from, message);
}
}
