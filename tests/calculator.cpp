#include "suites.h"
#include "calculator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <format>
#include <functional>
#include <numbers>
#include <stdexcept>

namespace {
using namespace calculator;
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
    const char *action = "Copy";
};
#include "calculator_cases.h"
void check(bool value, const std::string &message) {
    if (!value) throw std::runtime_error(message);
}
Result success(const std::string &text, int64_t now = reference) {
    auto r = evaluate(text, now);
    check(r.status == Status::Success, text + ": " + r.error.code + ": " + r.error.message);
    check(r.value.has_value() && !r.outputs.empty(), text + ": missing value or output");
    return r;
}
std::string copied(const std::string &text, const std::string &action = "Copy", int64_t now = reference) {
    auto r = success(text, now);
    for (const auto &out : r.outputs)
        if (out.action == action) return out.copy;
    throw std::runtime_error(text + ": missing " + action);
}
void expect(const std::string &text, const std::string &expected, const std::string &action = "Copy",
            int64_t now = reference) {
    auto value = copied(text, action, now);
    check(value == expected, text + ": expected " + expected + "; got " + value);
}
void reject(const std::string &text, const std::string &code = "", size_t position = 0, int64_t now = reference) {
    auto r = evaluate(text, now);
    check(r.status == Status::Error && !r.recognize && !r.value && r.outputs.empty(), "Expected rejection: " + text);
    check(!r.error.message.empty() && !r.error.code.empty() && r.error.message.back() != '.',
          "Missing recovery error: " + text);
    if (!code.empty()) check(r.error.code == code, text + ": expected " + code + "; got " + r.error.code);
    if (position) check(r.error.position == position, text + ": wrong error position");
}
double scalar(const std::string &text) { return std::get<Scalar>(*success(text).value).value; }
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
        auto value = copied(c.text, "Copy", local);
        check(value.substr(0, 19) == c.expected, std::string(c.text) + ": got " + value);
    }
    for (const auto &c : dateCases) expect(c.text, c.expected, "Copy", local);
    for (const auto &c : zoneCases) {
        std::string iso = c.expected;
        iso[10] = 'T';
        iso += 'Z';
        expect(c.text, iso, "Copy ISO");
    }
    for (const auto &c : parityCases) expect(c.text, c.expected, c.action);
    expect("days until 25 Dec", "101 days", "Copy", local);
    expect("days since 2026-09-01", "14 days", "Copy", local);
    expect("time diff local", "0 seconds");
    expect("90m", "90 m");
    expect("tomorrow7am", std::to_string(localTime(2026, 9, 16, 7)), "Copy Unix", local);
    expect("friday15:00", std::to_string(localTime(2026, 9, 18, 15)), "Copy Unix", local);
    expect("August17", "2027-08-17", "Copy", local);
    expect("17August", "2027-08-17", "Copy", local);
    expect("in90m", std::to_string(reference + 5400), "Copy Unix");
    expect("in 90m", std::to_string(reference + 5400), "Copy Unix");
    expect("(2026 - 12) - 25", "1989");
    expect("2021-01-01", "2020-W53", "Copy ISO Week");
    expect("2024-12-30", "2025-W01", "Copy ISO Week");
    expect("2016-01-04", "2016-W01", "Copy ISO Week");
    for (const auto *text : {"0.1+0.2", "-0", "1/3", "2^53", "-(2^53)", "5!", "1e20"})
        check(!success(text).outputs[0].copy.empty(), text);
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
    check(first.outputs[0].copy == "36", "Evaluations retain their values");
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
        expect(c.text, c.expected, "Copy ISO", 1789520400);
    for (const auto *zone : {"UTC", "PT", "Adelaide", "Asia/Kathmandu", "UTC-00:44:30"}) {
        auto text = copied(std::string("unix 1789480800.123 to ") + zone);
        expect(text, "1789480800.123", "Copy Unix");
    }
    // Local CRT DST rules are tested only when the process already uses Adelaide.
    // Named-zone tests above always run and never alter the process timezone.
    if (copied("now", "Copy ISO", localTime(2026, 9, 15, 12)) == "2026-09-15T02:30:00Z" &&
        copied("now", "Copy ISO", localTime(2026, 1, 15, 12)) == "2026-01-15T01:30:00Z") {
        reject("2026-10-04 at 02:30", "nonexistent_time");
        reject("2026-04-05 at 02:30", "ambiguous_time");
        auto night = localTime(2026, 10, 3, 23, 30);
        expect("now + 1d", std::to_string(night + 23 * 3600), "Copy Unix", night);
        expect("now + 24h", std::to_string(night + 24 * 3600), "Copy Unix", night);
        expect("now + 1d2h", std::to_string(night + 25 * 3600), "Copy Unix", night);
        for (int dst : {0, 1}) {
            auto repeated = localTime(2026, 4, 5, 2, 30, 0, dst);
            expect("now", std::to_string(repeated), "Copy Unix", repeated);
            expect("now + 0h", std::to_string(repeated), "Copy Unix", repeated);
            expect("now + 30m", std::to_string(repeated + 1800), "Copy Unix", repeated);
        }
        auto between = localTime(2026, 4, 5, 2, 45, 0, 1);
        for (auto text : {"2:30", "sunday at 2:30", "April 5 at 2:30"}) reject(text, "ambiguous_time", 0, between);
        auto after = localTime(2026, 4, 5, 3, 30);
        expect("2:30", "2026-04-06 02:30:00 UTC+09:30", "Copy", after);
        expect("sunday at 2:30", "2026-04-12 02:30:00 UTC+09:30", "Copy", after);
        expect("April 5 at 2:30", "2027-04-05 02:30:00 UTC+09:30", "Copy", after);
        after = localTime(2026, 10, 4, 3, 30);
        expect("2:30", "2026-10-05 02:30:00 UTC+10:30", "Copy", after);
        reject("today at 2:30", "nonexistent_time", 0, after);
    }

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
            if (r.status == Status::Success)
                check(r.value && !r.outputs.empty(), "Malformed input lost result: " + text);
            else
                check(!r.error.code.empty() && !r.error.message.empty() && r.error.position <= text.size() + 1,
                      "Malformed input lost error: " + text);
        }
    std::puts("Native calculator regression, calendar, DST, generated and malformed-input checks passed");
    return 0;
}
