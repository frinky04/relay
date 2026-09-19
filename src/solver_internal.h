#pragma once
#include "solver.h"

namespace solver::detail {
[[noreturn]] void fail(std::string code, std::string message, size_t position = 0);
[[noreturn]] void failAt(std::string code, std::string message, Span span);
double checked(double value, size_t position);
std::string lower(std::string_view text);
std::string clean(std::string_view text);
// A normalized slice with positions in the root normalized expression. Rewrites
// preserve source locations instead of manufacturing offsets in generated text.
struct Source {
    std::string text;
    std::vector<Span> origins;
    size_t end = 0;
    Source() = default;
    explicit Source(std::string text);
    Source slice(size_t begin, size_t count = std::string::npos) const;
    Span span(size_t begin, size_t count = 1) const;
    void replace(size_t begin, size_t count, const Source &replacement);
    void insert(size_t begin, std::string_view text, Span origin);
};
struct PeriodToken {
    std::string text;
    Span span;
};
struct PeriodTerm {
    double value;
    const Unit *unit;
    Span span;
};
struct PeriodTerms {
    std::vector<PeriodTerm> terms;
};
struct CalendarPeriod {
    int64_t months = 0, days = 0, seconds = 0;
    bool hasTime = false;
};
const Unit *findUnit(std::string_view name);
const Unit *periodUnit(std::string_view name, bool minuteAlias = true);
PeriodTerms readPeriod(const std::vector<PeriodToken> &tokens, size_t &index);
CalendarPeriod calendarPeriod(const PeriodTerms &terms);
Quantity periodQuantity(const PeriodTerms &terms);
double convert(double value, const Unit *from, const Unit *to, size_t position);
Solution timespan(const Source &source);
Solution math(std::string_view text);
bool temporalForm(const std::string &text);
Solution datetime(Source source, int64_t reference);
bool baseForm(const std::string &text);
Solution bases(std::string_view text);
bool colorForm(const std::string &text);
Solution colors(const std::string &text);
std::string formatOffset(int offset);
std::string formatInteger(Integer value, int radix);
std::string formatColor(Color value, Format format);
} // namespace solver::detail
