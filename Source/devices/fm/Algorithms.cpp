#include "Algorithms.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace mb::fm
{
namespace
{
/** "65 54 21 / 1 3 / 6": edges "ab" mean OP a modulates OP b, then carriers, then the feedback op. */
Algorithm parse (const char* edges, const char* carriers, int fb)
{
    Algorithm a;
    for (const char* p = edges; *p; ++p)
    {
        if (*p == ' ') continue;
        const int from = p[0] - '1', to = p[1] - '1';
        a.mods[(size_t) to] |= (uint8_t) (1u << from);
        ++p;
    }
    for (const char* p = carriers; *p; ++p)
        if (*p != ' ') a.carriers |= (uint8_t) (1u << (*p - '1'));
    a.feedbackOp = fb - 1;
    return a;
}
} // namespace

const std::array<Algorithm, 32>& algorithms()
{
    static const std::array<Algorithm, 32> table = {
        parse ("65 54 43 21", "13", 6), parse ("65 54 43 21", "13", 2), parse ("65 54 32 21", "14", 6),
        parse ("65 54 32 21", "14", 4), parse ("65 43 21", "135", 6), parse ("65 43 21", "135", 5),
        parse ("65 53 43 21", "13", 6), parse ("65 53 43 21", "13", 4), parse ("65 53 43 21", "13", 2),
        parse ("64 54 32 21", "14", 3), parse ("64 54 32 21", "14", 6), parse ("63 53 43 21", "13", 2),
        parse ("63 53 43 21", "13", 6), parse ("64 54 43 21", "13", 6), parse ("64 54 43 21", "13", 2),
        parse ("65 43 31 51 21", "1", 6), parse ("65 43 31 51 21", "1", 2), parse ("65 54 41 31 21", "1", 3),
        parse ("64 65 32 21", "145", 6), parse ("64 54 31 32", "124", 3), parse ("64 65 31 32", "1245", 3),
        parse ("63 64 65 21", "1345", 6), parse ("64 65 32", "1245", 6), parse ("63 64 65", "12345", 6),
        parse ("64 65", "12345", 6), parse ("64 54 32", "124", 6), parse ("64 54 32", "124", 3),
        parse ("54 43 21", "136", 5), parse ("65 43", "1235", 6), parse ("54 43", "1236", 5),
        parse ("65", "12345", 6), parse ("", "123456", 6) };
    return table;
}

AlgoLayout layoutFor (int algorithm)
{
    const auto& a = algorithms()[(size_t) std::clamp (algorithm, 1, 32) - 1];
    AlgoLayout L {};
    bool placed[6] {};
    // Each carrier starts a stack; walk up through its first unplaced modulator chain.
    for (int c = 0; c < 6; ++c)
    {
        if (! (a.carriers & (1u << c))) continue;
        int chain[6], len = 0, op = c;
        while (op >= 0 && len < 6)
        {
            chain[len++] = op; placed[op] = true;
            int next = -1;
            for (int m = 5; m >= 0; --m) if ((a.mods[(size_t) op] & (1u << m)) && ! placed[m]) { next = m; break; }
            op = next;
        }
        auto& s = L.stacks[L.numStacks];
        for (int i = 0; i < len; ++i) s[i] = chain[len - 1 - i];
        L.stackLen[L.numStacks++] = len;
    }
    // Operators shared by several stacks (one modulator feeding many) get their own column.
    for (int op = 5; op >= 0; --op)
        if (! placed[op] && L.numStacks < 6) { L.stacks[L.numStacks][0] = op; L.stackLen[L.numStacks++] = 1; placed[op] = true; }
    return L;
}
} // namespace mb::fm
