#include "fuzzy.h"
#include <algorithm>
#include <cctype>

namespace fuzzy {

std::string lower(std::string_view s) {
    std::string r(s);
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

static bool isBoundary(std::string_view t, size_t i) {
    if (i == 0) return true;
    unsigned char prev = t[i - 1], cur = t[i];
    if (!std::isalnum(prev)) return true;                        // after space, '-', '.', etc.
    if (std::islower(prev) && std::isupper(cur)) return true;    // camelCase
    return false;
}

// Greedy forward match with a single backtrack-free pass, then a second
// pass that prefers boundary starts. Cheap and good enough for launcher use.
int score(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) return 1;
    if (text.empty()) return 0;

    const std::string p = lower(pattern);
    const std::string t = lower(text);

    // Exact prefix / substring fast paths.
    if (t.compare(0, p.size(), p) == 0) return 1000 - (int)std::min<size_t>(t.size() - p.size(), 200);
    if (auto pos = t.find(p); pos != std::string::npos) {
        int s = 600 - (int)std::min<size_t>(pos, 200);
        if (isBoundary(text, pos)) s += 150;
        return s;
    }

    // Subsequence scoring.
    int best = 0;
    // Try starting at each occurrence of the first char (bounded) to favor boundary starts.
    int tries = 0;
    for (size_t start = 0; start < t.size() && tries < 8; ++start) {
        if (t[start] != p[0]) continue;
        ++tries;
        int s = 0;
        size_t pi = 0, ti = start;
        int consecutive = 0;
        size_t lastMatch = std::string::npos;
        while (pi < p.size() && ti < t.size()) {
            if (t[ti] == p[pi]) {
                int add = 10;
                if (isBoundary(text, ti)) add += 30;
                if (lastMatch != std::string::npos && ti == lastMatch + 1) { ++consecutive; add += 15 * consecutive; }
                else consecutive = 0;
                if (lastMatch != std::string::npos) add -= (int)std::min<size_t>(ti - lastMatch - 1, 10); // gap penalty
                s += add;
                lastMatch = ti;
                ++pi;
            }
            ++ti;
        }
        if (pi == p.size()) {
            s -= (int)std::min<size_t>(start, 20);
            s -= (int)std::min<size_t>(t.size(), 100) / 10; // slight preference for shorter
            best = std::max(best, s);
        }
    }
    return best > 0 ? best : 0;
}

} // namespace fuzzy
