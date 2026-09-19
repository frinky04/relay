#include "solver_internal.h"
#include <array>
#include <cmath>
#include <format>
#include <iomanip>
#include <locale>
#include <sstream>

namespace solver {
namespace {
using namespace std::chrono;
std::string number(double value) {
    if (value == 0) return "0";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (std::trunc(value) == value && std::abs(value) <= 9007199254740992.0)
        out << std::fixed << std::setprecision(0) << value;
    else
        out << std::setprecision(15) << value;
    return out.str();
}
std::string dateText(year_month_day day, Format format) {
    static const std::array<std::string, 12> months{"January",   "February", "March",    "April",
                                                    "May",       "June",     "July",     "August",
                                                    "September", "October",  "November", "December"};
    static const std::array<std::string, 7> weekdays{"Monday", "Tuesday",  "Wednesday", "Thursday",
                                                     "Friday", "Saturday", "Sunday"};
    auto weekday = std::chrono::weekday{sys_days{day}}.iso_encoding();
    if (format == Format::ISOWeek) {
        auto thursday = sys_days{day} + days{4 - int(weekday)};
        auto year = year_month_day{thursday}.year();
        return std::format("{:04}-W{:02}", int(year), (thursday - sys_days{year / January / 1}).count() / 7 + 1);
    }
    if (format == Format::FullDate || format == Format::ShortDate) {
        auto week = weekdays[weekday - 1], month = months[unsigned(day.month()) - 1];
        if (format == Format::ShortDate) {
            week.resize(3);
            month.resize(3);
        }
        return std::format("{}, {} {} {}", week, unsigned(day.day()), month, int(day.year()));
    }
    return std::format("{:04}-{:02}-{:02}", int(day.year()), unsigned(day.month()), unsigned(day.day()));
}
std::string fraction(int ms) { return ms ? std::format(".{:03}", ms) : ""; }
std::string clockText(const CivilTime &d, bool full) {
    auto text = std::format("{:02}:{:02}", d.hour, d.minute);
    if (full || d.second || d.millisecond) text += std::format(":{:02}", d.second);
    return text + fraction(d.millisecond);
}
std::string durationText(const Duration &d) {
    auto n = std::abs(d.seconds);
    if (d.calendarDays) {
        auto count = d.seconds / 86400;
        return std::to_string(count) + (std::abs(count) == 1 ? " day" : " days");
    }
    std::string text = d.seconds < 0 ? "-" : d.positiveSign && d.seconds > 0 ? "+" : "";
    bool any = false;
    for (auto [scale, label] : {std::pair{86400, "day"}, {3600, "hour"}, {60, "minute"}, {1, "second"}}) {
        auto count = n / scale;
        n %= scale;
        if (!count) continue;
        if (any) text += ' ';
        any = true;
        text += std::to_string(count) + " " + label + (count == 1 ? "" : "s");
    }
    return any ? text : "0 seconds";
}
} // namespace

namespace detail {
std::string formatOffset(int offset) {
    auto n = std::abs(offset);
    return std::format("UTC{}{:02}:{:02}", offset < 0 ? "-" : "+", n / 3600, n / 60 % 60) +
           (n % 60 ? std::format(":{:02}", n % 60) : "");
}
} // namespace detail

std::span<const Format> formats(const Solution &s) {
    static constexpr Format primary[] = {Format::Primary};
    static constexpr Format integer[] = {Format::Primary, Format::Binary, Format::Octal, Format::Decimal, Format::Hex};
    static constexpr Format color[] = {Format::Primary, Format::Hex, Format::RGB, Format::HSL};
    static constexpr Format date[] = {Format::Primary, Format::FullDate, Format::ISOWeek};
    static constexpr Format instant[] = {Format::Primary, Format::Discord, Format::DiscordRelative, Format::ISO,
                                         Format::Unix};
    if (std::holds_alternative<Integer>(s.value)) return integer;
    if (std::holds_alternative<Color>(s.value)) return color;
    if (std::holds_alternative<Date>(s.value)) return date;
    if (std::holds_alternative<Instant>(s.value)) return instant;
    return primary;
}
std::string format(const Solution &s, Format requested) {
    auto f = requested == Format::Primary ? s.requested : requested;
    if (auto n = std::get_if<Scalar>(&s.value)) return number(n->value);
    if (auto n = std::get_if<Quantity>(&s.value)) return number(n->value) + " " + n->unit->symbol;
    if (auto n = std::get_if<Integer>(&s.value))
        return detail::formatInteger(*n, f == Format::Binary ? 2 : f == Format::Octal ? 8 : f == Format::Hex ? 16 : 10);
    if (auto c = std::get_if<Color>(&s.value)) return detail::formatColor(*c, f);
    if (auto d = std::get_if<Date>(&s.value)) return dateText(d->day, f);
    if (auto d = std::get_if<Duration>(&s.value)) return durationText(*d);
    const auto &instant = std::get<Instant>(s.value);
    auto timestamp = floor<seconds>(instant.time).time_since_epoch().count();
    const auto &d = instant.destination;
    if (f == Format::Unix) return std::to_string(timestamp) + fraction(d.millisecond);
    if (f == Format::Discord || f == Format::DiscordRelative)
        return "<t:" + std::to_string(timestamp) + (f == Format::Discord ? ":f>" : ":R>");
    if (f == Format::ShortDate || f == Format::FullDate || f == Format::ISOWeek) return dateText(d.day, f);
    if (f == Format::Clock) return clockText(d, false);
    if (f == Format::ISO) {
        auto day = floor<days>(instant.time);
        hh_mm_ss clock{instant.time - day};
        CivilTime utc{year_month_day{day}, int(clock.hours().count()), int(clock.minutes().count()),
                      int(clock.seconds().count()), d.millisecond};
        return dateText(utc.day, Format::Primary) + "T" + clockText(utc, true) + "Z";
    }
    return dateText(d.day, Format::Primary) + " " + clockText(d, true) + " " + detail::formatOffset(d.offset);
}
} // namespace solver
