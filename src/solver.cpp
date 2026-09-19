#include "solver_internal.h"
#include <algorithm>
#include <cmath>

namespace solver::detail {
[[noreturn]] void fail(std::string code, std::string message, size_t position) {
    throw Diagnostic{std::move(code), std::move(message),
                     position ? std::optional{Span{position - 1, position}} : std::nullopt};
}
[[noreturn]] void failAt(std::string code, std::string message, Span span) {
    throw Diagnostic{std::move(code), std::move(message), span};
}
double checked(double value, size_t position) {
    if (!std::isfinite(value))
        fail("overflow", "The result is outside the finite numeric range; use smaller values", position);
    return value;
}
std::string lower(std::string_view text) {
    std::string result(text);
    for (auto &c : result)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}
std::string clean(std::string_view text) {
    std::string result;
    bool space = false;
    for (unsigned char c : text) {
        if (c == ' ' || (c >= '\t' && c <= '\r'))
            space = !result.empty();
        else {
            if (space) result += ' ';
            result += char(c);
            space = false;
        }
    }
    return result;
}
Source::Source(std::string value) : text(std::move(value)), end(text.size()) {
    for (size_t i = 0; i < text.size(); ++i)
        origins.push_back({i, i + 1});
}
Span Source::span(size_t begin, size_t count) const {
    if (begin >= text.size()) return {end, end};
    if (!count) return {origins[begin].begin, origins[begin].begin};
    return {origins[begin].begin, origins[std::min(begin + count, text.size()) - 1].end};
}
Source Source::slice(size_t begin, size_t count) const {
    begin = std::min(begin, text.size());
    count = std::min(count, text.size() - begin);
    Source result;
    result.text = text.substr(begin, count);
    result.origins.assign(origins.begin() + begin, origins.begin() + begin + count);
    result.end = begin + count == text.size() ? end : origins[begin + count].begin;
    return result;
}
void Source::replace(size_t begin, size_t count, const Source &replacement) {
    count = std::min(count, text.size() - begin);
    text.replace(begin, count, replacement.text);
    origins.erase(origins.begin() + begin, origins.begin() + begin + count);
    origins.insert(origins.begin() + begin, replacement.origins.begin(), replacement.origins.end());
}
void Source::insert(size_t begin, std::string_view value, Span origin) {
    text.insert(begin, value);
    origins.insert(origins.begin() + begin, value.size(), origin);
}
} // namespace solver::detail

namespace solver {
Result evaluate(std::string_view expression, int64_t reference) {
    using namespace detail;
    if (expression.size() > 1024) return Diagnostic{"input", "Use an expression of at most 1024 bytes"};
    std::string normalized;
    std::vector<Span> origins;
    size_t spaceStart = 0;
    bool space = false;
    for (size_t i = 0; i < expression.size(); ++i) {
        unsigned char c = expression[i];
        if (c == ' ' || (c >= '\t' && c <= '\r')) {
            if (!space) spaceStart = i;
            space = !normalized.empty();
        } else {
            if (space) {
                normalized += ' ';
                origins.push_back({spaceStart, i});
            }
            normalized += c >= 'A' && c <= 'Z' ? char(c + 'a' - 'A') : char(c);
            origins.push_back({i, i + 1});
            space = false;
        }
    }
    try {
        Solution result;
        if (colorForm(normalized)) {
            if (expression.size() > 256) fail("input", "Use a color expression of at most 256 bytes");
            result = colors(normalized);
            result.expression = normalized;
        } else if (temporalForm(normalized)) {
            if (expression.size() > 256) fail("input", "Use a date/time expression of at most 256 bytes");
            Source source(normalized);
            result = normalized.ends_with(" to timespan") || normalized.ends_with(" in timespan")
                         ? timespan(source)
                         : datetime(std::move(source), reference);
            if (result.expression.empty()) result.expression = normalized;
        } else {
            result = baseForm(normalized) ? bases(normalized) : math(normalized);
            result.expression = clean(expression);
        }
        return result;
    } catch (Diagnostic error) {
        if (error.span) {
            auto span = *error.span;
            if (span.begin >= origins.size())
                error.span = Span{expression.size(), expression.size()};
            else
                error.span = Span{origins[span.begin].begin, span.end <= span.begin
                                                                 ? origins[span.begin].begin
                                                                 : origins[std::min(span.end, origins.size()) - 1].end};
        }
        return error;
    }
}
} // namespace solver
