#pragma once
#include <string>
#include <string_view>

namespace fuzzy {

// Returns score > 0 on match, 0 on no match. Case-insensitive, subsequence
// match with bonuses for word boundaries, camelCase, consecutive runs and
// prefix. Pattern and text are UTF-8 but scoring is byte-based (fine for
// ASCII-dominant app names).
int score(std::string_view pattern, std::string_view text);

std::string lower(std::string_view s);

} // namespace fuzzy
