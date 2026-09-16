#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace calculator {
struct Scalar {
    double value = 0;
    bool percentage = false;
};
struct Unit {
    std::string symbol, dimension;
    double scale = 1, offset = 0;
};
struct Quantity {
    double value;
    const Unit *unit;
};
struct Date {
    std::chrono::year_month_day day;
};
struct Instant {
    std::chrono::sys_time<std::chrono::milliseconds> time;
};
struct Duration {
    int64_t seconds = 0;
    bool calendarDays = false;
};
using Value = std::variant<Scalar, Quantity, Date, Instant, Duration>;
struct Error {
    std::string code, message;
    size_t position = 0;
};
struct Output {
    std::string action, title, subtitle, copy;
};
enum class Status { Success, Error };
struct Result {
    Status status = Status::Error;
    std::optional<Value> value;
    Error error;
    bool recognize = false;
    std::vector<Output> outputs;
};

// Pure, bounded evaluation. The caller supplies Unix seconds; actions never
// evaluate again or read a newer clock.
Result evaluate(std::string_view text, int64_t reference);

namespace detail {
const Unit *findUnit(std::string_view name);
double convert(double value, const Unit *from, const Unit *to, size_t position);
double checked(double value, size_t position);
[[noreturn]] void fail(std::string code, std::string message, size_t position = 0);
std::string lower(std::string_view text);
std::string clean(std::string_view text);
// Calendar parsing receives lowercase text with collapsed ASCII whitespace.
bool temporalForm(const std::string &text);
Result datetime(std::string text, int64_t reference);
} // namespace detail
} // namespace calculator
