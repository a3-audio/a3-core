// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Reproduces python-osc 1.10.2's pythonosc/dispatcher.py, Dispatcher.handlers_for_address: the
// incoming address pattern is turned into a regex ("*" -> "[^/]*", "?" -> "[^/]", "[!" -> "[^",
// "{a,b}" -> "(a|b)", other metacharacters escaped) and re.match'ed against every mapped address;
// a mapped address holding '*' is also read as the regex addr.replace("*", ".*?") + "$" and
// re.match'ed against the incoming one. Every match in map order, the default only when nothing
// matched; a pattern that does not compile reaches nothing, not even the default.
//
// The regexes are matched by a small engine (a Pike VM, below) for the part of Python's `re` syntax
// those two forms can produce: literals, escapes of non-alphanumerics, '.', classes (with Python's
// rules: a ']' first is literal, ranges, '^' negation), groups with '|', '*' '+' '?' (lazy or not),
// '^' and '$' ('$' also before a final newline). Anything else (\w, {m,n} repeats, (?...) groups)
// is refused.
#include "Dispatcher.h"

#include "Utf8.h"

#include <optional>
#include <stdexcept>

namespace a3core
{
namespace
{
struct Unsupported {};   // re.error, or syntax beyond the subset

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
//
// Compiled to a small program and run as a Pike VM: every live state advances together, one input
// character at a time. Nothing recurses per character, so neither a long address nor a long
// pattern can grow the stack, and the time is linear in the text (a 60000-character address in one
// datagram must neither crash Core nor stall it).

struct Instruction
{
    enum class Op { Char, Split, Jump, Start, End, Match } op = Op::Match;
    Node::Kind test = Node::Kind::Literal;   // Char: Literal, Any or Class
    char32_t literal = 0;
    bool negated = false;
    std::vector<std::pair<char32_t, char32_t>> ranges;
    size_t x = 0, y = 0;                     // Split: both; Jump: x
};

class Program
{
public:
    explicit Program (const std::vector<Sequence>& top)
    {
        alternation (top);
        add (Instruction::Op::Match);
    }

    bool matches (const std::u32string& s) const
    {
        std::vector<size_t> current, next;
        std::vector<size_t> seen (code.size(), (size_t) -1);
        if (closure (0, 0, s, current, seen))
            return true;
        for (size_t at = 0; at < s.size() && ! current.empty(); ++at)
        {
            next.clear();
            for (const auto pc : current)
                if (accepts (code[pc], s[at]) && closure (pc + 1, at + 1, s, next, seen))
                    return true;
            std::swap (current, next);
        }
        return false;
    }

private:
    size_t add (Instruction::Op op)
    {
        Instruction instruction;
        instruction.op = op;
        code.push_back (std::move (instruction));
        return code.size() - 1;
    }

    void alternation (const std::vector<Sequence>& alts)
    {
        std::vector<size_t> jumpsToEnd;
        for (size_t a = 0; a + 1 < alts.size(); ++a)
        {
            const auto split = add (Instruction::Op::Split);
            code[split].x = code.size();
            sequence (alts[a]);
            jumpsToEnd.push_back (add (Instruction::Op::Jump));
            code[split].y = code.size();
        }
        sequence (alts.back());
        for (const auto jump : jumpsToEnd)
            code[jump].x = code.size();
    }

    void sequence (const Sequence& seq)
    {
        for (const auto& node : seq)
            item (node);
    }

    // x? : split body, after.  x* : loop: split body, after; body; jump loop.  x+ : body; split body, after.
    void item (const Node& node)
    {
        if (node.min == 1 && node.max == 1)
            return atom (node);
        if (node.max == 1)
        {
            const auto split = add (Instruction::Op::Split);
            code[split].x = code.size();
            atom (node);
            code[split].y = code.size();
            return;
        }
        if (node.min == 0)
        {
            const auto loop = add (Instruction::Op::Split);
            code[loop].x = code.size();
            atom (node);
            code[add (Instruction::Op::Jump)].x = loop;
            code[loop].y = code.size();
            return;
        }
        const auto body = code.size();
        atom (node);
        const auto split = add (Instruction::Op::Split);
        code[split].x = body;
        code[split].y = code.size();
    }

    void atom (const Node& node)
    {
        switch (node.kind)
        {
            case Node::Kind::Group: alternation (node.alternatives); return;
            case Node::Kind::Start: add (Instruction::Op::Start); return;
            case Node::Kind::End: add (Instruction::Op::End); return;
            case Node::Kind::Literal: case Node::Kind::Any: case Node::Kind::Class:
            {
                auto& c = code[add (Instruction::Op::Char)];
                c.test = node.kind;
                c.literal = node.literal;
                c.negated = node.negated;
                c.ranges = node.ranges;
                return;
            }
        }
    }

    static bool accepts (const Instruction& c, char32_t ch)
    {
        switch (c.test)
        {
            case Node::Kind::Literal: return ch == c.literal;
            case Node::Kind::Any: return ch != '\n';
            case Node::Kind::Class:
            {
                bool inside = false;
                for (const auto& [lo, hi] : c.ranges)
                    inside = inside || (ch >= lo && ch <= hi);
                return inside != c.negated;
            }
            case Node::Kind::Group: case Node::Kind::Start: case Node::Kind::End: return false;
        }
        return false;
    }

    // Follows splits, jumps and assertions from `start` at position `at` with an explicit stack;
    // adds the Char states reached to `out`. True when Match is reached: re.match has a match.
    bool closure (size_t start, size_t at, const std::u32string& s, std::vector<size_t>& out, std::vector<size_t>& seen) const
    {
        std::vector<size_t> stack { start };
        while (! stack.empty())
        {
            const auto pc = stack.back();
            stack.pop_back();
            if (seen[pc] == at)
                continue;
            seen[pc] = at;
            const auto& c = code[pc];
            switch (c.op)
            {
                case Instruction::Op::Match: return true;
                case Instruction::Op::Char: out.push_back (pc); break;
                case Instruction::Op::Jump: stack.push_back (c.x); break;
                case Instruction::Op::Split: stack.push_back (c.y); stack.push_back (c.x); break;
                case Instruction::Op::Start: if (at == 0) stack.push_back (pc + 1); break;
                case Instruction::Op::End:
                    if (at == s.size() || (at + 1 == s.size() && s[at] == '\n'))
                        stack.push_back (pc + 1);
                    break;
            }
        }
        return false;
    }

    std::vector<Instruction> code;
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
    std::optional<Program> wildcard;   // the mapped address read as a regex, if it has '*'
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
    const auto text = utf8::codePoints (pattern);
    if (! text)
        throw std::invalid_argument ("a mapped address is no UTF-8: " + pattern);
    auto entry = std::make_unique<Entry>();
    entry->text = *text;
    if (pattern.find ('*') != std::string::npos)
    {
        try
        {
            entry->wildcard.emplace (Parser (regexOfMappedWildcard (entry->text)).parse());
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
    const auto address = utf8::codePoints (message.address);
    if (! address)
        return;   // no Python str: python-osc never gets this far (decoding raised)
    std::optional<Program> incoming;
    try
    {
        incoming.emplace (Parser (regexOfAddressPattern (*address)).parse());
    }
    catch (const Unsupported&)
    {
        return;   // re.error: "it won't match anything" -- and the generator ends before the default
    }
    bool matched = false;
    for (const auto& entry : entries)
    {
        if (! incoming->matches (entry->text) && ! (entry->wildcard && entry->wildcard->matches (*address)))
            continue;
        matched = true;
        for (const auto& handler : entry->handlers)
            handler (from, message);
    }
    if (! matched && fallback)
        fallback (from, message);
}
}
