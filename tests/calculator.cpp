#include "suites.h"
#include "solver.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <functional>
#include <numbers>
#include <stdexcept>

namespace {
using namespace solver;
constexpr int64_t reference = 1789480800;
struct MathCase {
    const char *text;
    double expected;
};
struct ErrorCase {
    const char *text;
    const char *code;
    size_t position;
};
struct CopyCase {
    const char *text;
    const char *expected;
    Format action = Format::Primary;
};
#include "calculator_cases.h"
void check(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
Solution success(const std::string &text, int64_t now = reference) {
    auto r = evaluate(text, now);
    if (auto e = std::get_if<Diagnostic>(&r)) throw std::runtime_error(text + ": " + e->code + ": " + e->message);
    return std::get<Solution>(r);
}
std::string copied(const std::string &text, Format action = Format::Primary, int64_t now = reference) {
    return format(success(text, now), action);
}
void expect(const std::string &text, const std::string &expected, Format action = Format::Primary,
            int64_t now = reference) {
    auto value = copied(text, action, now);
    check(value == expected, text + ": expected " + expected + "; got " + value);
}
void reject(const std::string &text, const std::string &code = "", size_t position = 0, int64_t now = reference) {
    auto r = evaluate(text, now);
    auto error = std::get_if<Diagnostic>(&r);
    check(error != nullptr, "Expected rejection: " + text);
    check(!error->message.empty() && !error->code.empty() && error->message.back() != '.', "Missing recovery error: " + text);
    if (!code.empty()) check(error->code == code, text + ": expected " + code + "; got " + error->code);
    if (position) check(error->span && error->span->begin == position - 1, text + ": wrong error position");
}
double scalar(const std::string &text) { return std::get<Scalar>(success(text).value).value; }
int64_t localTime(int y, int m, int d, int h = 0, int minute = 0, int second = 0, int dst = -1) {
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = dst;
    return _mktime64(&tm);
}
std::string localDate(int64_t timestamp) {
    std::tm tm{};
    __time64_t t = timestamp;
    check(!_localtime64_s(&tm, &t), "Local date conversion");
    return std::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}
std::string repeat(std::string_view text, int count) {
    std::string s;
    while (count--) s += text;
    return s;
}
} // namespace

int runCalculatorTests() {
    runCalculatorFormatTests();
    for (const auto &c : mathCases) {
        auto actual = scalar(c.text);
        auto tolerance = c.expected == 0 ? 1e-14 : std::abs(c.expected) * 1e-12;
        check(std::abs(actual - c.expected) <= tolerance, std::string(c.text) + ": numeric mismatch");
    }
    for (const auto &c : mathErrors) reject(c.text, c.code, c.position);
    for (const auto &c : quantityCases) {
        expect(c.text, c.expected);
        check(success(c.text).recognize, c.text);
    }
    for (const auto &c : invalidCases) reject(c);
    auto local = localTime(2026, 9, 15, 23, 30);
    for (const auto &c : localTimeCases) {
        auto value = copied(c.text, Format::Primary, local);
        check(value.substr(0, 19) == c.expected, std::string(c.text) + ": got " + value);
    }
    for (const auto &c : dateCases) expect(c.text, c.expected, Format::Primary, local);
    for (const auto &c : zoneCases) {
        std::string iso = c.expected;
        iso[10] = 'T';
        iso += 'Z';
        expect(c.text, iso, Format::ISO);
    }
    for (const auto &c : parityCases) expect(c.text, c.expected, c.action);
    expect("days until 25 Dec", "101 days", Format::Primary, local);
    expect("days since 2026-09-01", "14 days", Format::Primary, local);
    expect("time diff local", "0 seconds");
    expect("90m", "90 m");
    expect("tomorrow7am", std::to_string(localTime(2026, 9, 16, 7)), Format::Unix, local);
    expect("friday15:00", std::to_string(localTime(2026, 9, 18, 15)), Format::Unix, local);
    expect("August17", "2027-08-17", Format::Primary, local);
    expect("17August", "2027-08-17", Format::Primary, local);
    expect("in90m", std::to_string(reference + 5400), Format::Unix);
    expect("in 90m", std::to_string(reference + 5400), Format::Unix);
    expect("(2026 - 12) - 25", "1989");
    expect("2021-01-01", "2020-W53", Format::ISOWeek);
    expect("2024-12-30", "2025-W01", Format::ISOWeek);
    expect("2016-01-04", "2016-W01", Format::ISOWeek);
    for (const auto *text : {"0.1+0.2", "-0", "1/3", "2^53", "-(2^53)", "5!", "1e20"})
        check(!format(success(text)).empty(), text);
    expect("0.1+0.2", "0.3");
    expect("-0", "0");
    expect("1/3", "0.333333333333333");
    expect("2^53", "9007199254740992");
    expect("-(2^53)", "-9007199254740992");
    expect("1e20", "1e+20");
    check(scalar("9223372036854775807+1") == 9223372036854775808.0, "No integer wraparound");
    check(scalar("4294967296*4294967296") == 18446744073709551616.0, "Large multiplication");
    check(scalar("9007199254740992+2") == 9007199254740994.0 && std::isfinite(scalar("170!")), "Floating range");
    reject(std::string(1025, '1'), "input");
    reject(repeat("(", 65) + "1" + repeat(")", 65), "limit");
    reject(repeat("-", 65) + "1", "limit");
    reject(repeat("2^", 65) + "1", "limit");
    reject(repeat("abs(", 65) + "1" + repeat(")", 65), "limit");
    reject(repeat("1+", 256) + "1", "limit");
    expect(repeat("1+", 250) + "1", "251");
    expect("max(" + repeat("1,", 31) + "32)", "32");
    reject("max(" + repeat("1,", 32) + "33)", "limit");
    reject("now " + std::string(253, ' '), "input");
    for (const auto *text : {"1969-12-31", "3000-01-01", "1970-01-01 - 1d", "2999-12-31 + 1d"}) reject(text, "range");
    for (const auto *text : {"tomorrow + 8h", "today + 0s", "tomorrow EST", "in 1d ET"}) reject(text, "time_required");
    auto first = success("15% of 240");
    reject("1+");
    success("2+3");
    check(format(first) == "36", "Evaluations retain their values");
    expect("1+10%", "1.1");
    expect("1+10", "11");

    // Independent CRT calendar oracle, covering leap and century boundaries.
    for (int y : {1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2100})
        for (int m = 1; m <= 12; ++m)
            expect(std::format("{:04}-{:02}-28 + 1w", y, m), localDate(localTime(y, m, 35, 12)));
    for (const auto *zone : {"ET", "CT", "MT", "PT"}) {
        reject(std::string("2026-03-08 at 02:30 ") + zone, "nonexistent_time");
        reject(std::string("2026-11-01 at 01:30 ") + zone, "ambiguous_time");
    }
    reject("2026-03-07 at 02:30 ET + 1d", "nonexistent_time");
    reject("2026-10-31 at 01:30 ET + 1d", "ambiguous_time");
    for (const auto &c : std::vector<CopyCase>{{"today at 4pm ET", "2026-09-15T20:00:00Z"},
                                               {"tomorrow at 4pm ET", "2026-09-16T20:00:00Z"},
                                               {"4pm ET", "2026-09-16T20:00:00Z"},
                                               {"tuesday at 4pm ET", "2026-09-22T20:00:00Z"},
                                               {"September 15 at 4pm ET", "2027-09-15T20:00:00Z"}})
        expect(c.text, c.expected, Format::ISO, 1789520400);
    for (const auto *zone : {"UTC", "PT", "Adelaide", "Asia/Kathmandu", "UTC-00:44:30"}) {
        auto text = copied(std::string("unix 1789480800.123 to ") + zone);
        expect(text, "1789480800.123", Format::Unix);
    }
    // Local CRT DST rules are tested only when the process already uses Adelaide.
    // Named-zone tests above always run and never alter the process timezone.
    if (copied("now", Format::ISO, localTime(2026, 9, 15, 12)) == "2026-09-15T02:30:00Z" &&
        copied("now", Format::ISO, localTime(2026, 1, 15, 12)) == "2026-01-15T01:30:00Z") {
        reject("2026-10-04 at 02:30", "nonexistent_time");
        reject("2026-04-05 at 02:30", "ambiguous_time");
        auto night = localTime(2026, 10, 3, 23, 30);
        expect("now + 1d", std::to_string(night + 23 * 3600), Format::Unix, night);
        expect("now + 24h", std::to_string(night + 24 * 3600), Format::Unix, night);
        expect("now + 1d2h", std::to_string(night + 25 * 3600), Format::Unix, night);
        for (int dst : {0, 1}) {
            auto repeated = localTime(2026, 4, 5, 2, 30, 0, dst);
            expect("now", std::to_string(repeated), Format::Unix, repeated);
            expect("now + 0h", std::to_string(repeated), Format::Unix, repeated);
            expect("now + 30m", std::to_string(repeated + 1800), Format::Unix, repeated);
        }
        auto between = localTime(2026, 4, 5, 2, 45, 0, 1);
        for (auto text : {"2:30", "sunday at 2:30", "April 5 at 2:30"}) reject(text, "ambiguous_time", 0, between);
        auto after = localTime(2026, 4, 5, 3, 30);
        expect("2:30", "2026-04-06 02:30:00 UTC+09:30", Format::Primary, after);
        expect("sunday at 2:30", "2026-04-12 02:30:00 UTC+09:30", Format::Primary, after);
        expect("April 5 at 2:30", "2027-04-05 02:30:00 UTC+09:30", Format::Primary, after);
        after = localTime(2026, 10, 4, 3, 30);
        expect("2:30", "2026-10-05 02:30:00 UTC+10:30", Format::Primary, after);
        reject("today at 2:30", "nonexistent_time", 0, after);
    }

    // Shared duration vocabulary, composition and calendar semantics.
    for (const auto &c : std::vector<CopyCase>{
        {"1h30m to minutes", "90 min"}, {"1h and 30min to s", "5400 s"},
        {"1h,30min in s", "5400 s"}, {"1h 30min", "1.5 h"}, {"1h30m", "1.5 h"},
        {"-1h30m to minutes", "-90 min"}, {"+1h30m to minutes", "90 min"},
        {"2 * 1h30m to min", "180 min"}, {"1h30m / 2 to min", "45 min"},
        {"1h30m + 30min", "2 h"}, {"1h30m + 20%", "1.8 h"},
        {"1wks to days", "7 d"}, {"1yr6mo to months", "18 mo"},
        {"18 months to years", "1.5 yr"}, {"1.5 years to months", "18 mo"},
        {"1year + 6months", "1.5 yr"}, {"1 year to months", "12 mo"},
        {"1.5d to h", "36 h"}, {"1s500ms to s", "1.5 s"},
        {"0.1s1ms to ms", "101 ms"}, {"1h30m to timespan", "1 hour 30 minutes"},
        {"-1h30m to timespan", "-1 hour 30 minutes"},
        {"1000ms to timespan", "1 second"}, {"now + 1000ms", "1789480801", Format::Unix},
        {"now + 90m", "1789486200", Format::Unix}, {"90m", "90 m"}}) expect(c.text, c.expected, c.action);
    for (auto text : {"1 year to days", "1mo2d to hours", "1day to years", "1year to timespan"})
        reject(text, "calendar_conversion");
    for (auto text : {"1h30m to m", "1h1hr", "1h and", "1h,", "1h and -30m", "now + 1ms",
                      "1ms to timespan", "1.5d to timespan", "today + 1.5yr"}) reject(text);
    auto diagnostic = [&](const std::string &input, const std::string &token, const std::string &code) {
        auto result = evaluate(input, reference);
        auto error = std::get_if<Diagnostic>(&result);
        check(error && error->code == code && error->span, "Missing diagnostic span: " + input);
        check(input.substr(error->span->begin, error->span->end - error->span->begin) == token,
              "Wrong diagnostic token: " + input);
    };
    diagnostic("  SQRT \t ( -1 )", "SQRT", "domain");
    diagnostic("1 h to YAERS", "YAERS", "unknown_unit");
    diagnostic("  1h +   ", "", "syntax");
    diagnostic("1h to   ", "", "syntax");
    diagnostic(" today + 1.5 YEARS", "YEARS", "duration");
    diagnostic("time in UnknownCity", "UnknownCity", "timezone");
    diagnostic("now to Europe/Unknown", "Europe/Unknown", "timezone");
    diagnostic("  RGB( 256, 0, 0 )", "256", "color");
    diagnostic("#FFG", "G", "color");
    diagnostic(" RGB(255 0 0", "", "color");
    diagnostic("0xFF / 0", "/", "division_by_zero");
    diagnostic("2026-01-01 - February 30", "30", "date");
    diagnostic("  2026-09-15T14:00:00Z + 1.5 YEARS", "YEARS", "duration");

    int64_t seed = 781;
    auto random = [&](int n) {
        seed = seed * 48271 % 2147483647;
        return int(seed % n);
    };
    std::function<std::pair<std::string, double>(int)> generate = [&](int depth) -> std::pair<std::string, double> {
        if (!depth) {
            int n = random(19) - 9;
            return {"(" + std::to_string(n) + ")", double(n)};
        }
        auto [left, a] = generate(depth - 1);
        auto [right, b] = generate(depth - 1);
        auto op = random(3);
        return {"(" + left + std::string(1, "+-*"[op]) + right + ")", op == 0 ? a + b : op == 1 ? a - b : a * b};
    };
    for (int i = 0; i < 500; ++i) {
        auto [text, expected] = generate(3);
        check(scalar(text) == expected, text);
    }
    const std::vector<std::pair<int, std::vector<std::string>>> probes = {
        {5000,
         {"0", ".5", "1e", "-", "+", "*", "/", "^", "%", "!", "(", ")", ",", "pi", "e", "of", "sqrt", "min", "x"}},
        {1500, {"1", "2", "m", "min", "in", "to", "C", "MB", "(", ")", "+", "*", "off", "%"}},
        {3000, {"now", "tomorrow", "2026", "-", "+", "08", "17", "at", "7", "am", "h", ":", "noon", "in", ","}},
        {3000, {"4", "pm", "UTC", "+", "-", ":", "05", "30", "ET", "time", "EST", "1h", "tomorrow", "local"}},
        {1500,
         {"in", "to", "time", "diff", "days", "until", "since", "1", "now", "month", "-", "London", "timespan",
          "unix"}}};
    for (const auto &[count, pieces] : probes)
        for (int i = 0; i < count; ++i) {
            std::string text;
            int length = random(12) + 1;
            for (int n = 0; n < length; ++n) {
                if (n) text += ' ';
                text += pieces[random(int(pieces.size()))];
            }
            auto r = evaluate(text, reference);
            if (auto solution = std::get_if<Solution>(&r))
                check(!format(*solution).empty(), "Malformed input lost result: " + text);
            else {
                const auto &error = std::get<Diagnostic>(r);
                check(!error.code.empty() && !error.message.empty() && (!error.span ||
                    (error.span->begin <= error.span->end && error.span->end <= text.size())),
                    "Malformed input lost error: " + text);
            }
        }
    std::puts("Native calculator regression, calendar, DST, generated and malformed-input checks passed");
    return 0;
}
