#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace solver {
struct Span {
    size_t begin = 0, end = 0;
};
struct Diagnostic {
    std::string code, message;
    std::optional<Span> span;
};
struct Scalar {
    double value = 0;
    bool percentage = false;
};
struct Integer {
    uint64_t magnitude = 0;
    bool negative = false;
};
struct Color {
    double red = 0, green = 0, blue = 0, alpha = 1;
};
enum class PeriodField { None, Seconds, Days, Months };
struct Unit {
    std::string symbol, dimension;
    double scale = 1, offset = 0;
    PeriodField period = PeriodField::None;
};
struct Quantity {
    double value;
    const Unit *unit;
};
struct Date {
    std::chrono::year_month_day day;
};
struct CivilTime {
    std::chrono::year_month_day day;
    int hour = 0, minute = 0, second = 0, millisecond = 0, offset = 0;
    std::string zoneLabel;
};
struct Instant {
    std::chrono::sys_time<std::chrono::milliseconds> time;
    CivilTime destination;
    std::string sourceLabel;
    int64_t relativeDays = 0;
};
struct Duration {
    int64_t seconds = 0;
    bool calendarDays = false, positiveSign = false;
    std::string comparedZone;
};
using Value = std::variant<Scalar, Integer, Color, Quantity, Date, Instant, Duration>;
enum class Format {
    Primary,
    Binary,
    Octal,
    Decimal,
    Hex,
    RGB,
    HSL,
    ISO,
    Unix,
    Discord,
    DiscordRelative,
    FullDate,
    ISOWeek,
    ShortDate,
    Clock
};
struct Solution {
    Value value;
    bool recognize = true;
    Format requested = Format::Primary;
    std::string expression;
};
using Result = std::variant<Solution, Diagnostic>;

// Bounded, side-effect-free evaluation; the caller supplies the clock.
Result evaluate(std::string_view expression, int64_t referenceUnixSeconds);
// Represent the resolved value without evaluating again or reading the clock.
std::string format(const Solution &solution, Format format = Format::Primary);
std::span<const Format> formats(const Solution &solution);
} // namespace solver
