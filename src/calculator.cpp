#include "calculator.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <numbers>
#include <sstream>
#include <locale>
#include <iomanip>

namespace calculator {
namespace detail {
[[noreturn]] void fail(std::string code, std::string message, size_t position) {
    throw Error{std::move(code), std::move(message), position};
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
    for (const unsigned char c : text) {
        if (c == ' ' || (c >= '\t' && c <= '\r')) space = !result.empty();
        else {
            if (space) result += ' ';
            result += char(c);
            space = false;
        }
    }
    return result;
}
} // namespace detail
using namespace detail;
namespace {
bool digit(char c) { return c >= '0' && c <= '9'; }
bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
struct Token {
    std::string kind, text;
    double value = 0;
    size_t position = 1;
};
std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < text.size()) {
        const auto c = text[i];
        if (c == ' ' || (c >= '\t' && c <= '\r')) {
            ++i;
            continue;
        }
        const auto start = i;
        Token token;
        token.position = start + 1;
        if (digit(c) || (c == '.' && i + 1 < text.size() && digit(text[i + 1]))) {
            while (i < text.size() && digit(text[i])) ++i;
            if (i < text.size() && text[i] == '.') {
                ++i;
                while (i < text.size() && digit(text[i])) ++i;
            }
            if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
                const auto exponent = i++;
                if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
                if (i == text.size() || !digit(text[i]))
                    fail("number", "Add exponent digits, such as 1e3; separate the constant e with a space",
                         exponent + 1);
                while (i < text.size() && digit(text[i])) ++i;
            }
            token.kind = "number";
            auto part = text.substr(start, i - start);
            auto parsed = std::from_chars(part.data(), part.data() + part.size(), token.value);
            if (parsed.ec == std::errc::result_out_of_range) {
                // from_chars rejects both overflow and underflow. Match the old
                // floating parser's subnormal/zero behavior without using locale.
                std::istringstream stream{std::string(part)};
                stream.imbue(std::locale::classic());
                stream >> token.value;
                if (stream.fail() && token.value != 0) fail("overflow", "Use smaller numeric values", start + 1);
            }
            checked(token.value, start + 1);
        } else if (alpha(c)) {
            while (i < text.size() && (alpha(text[i]) || digit(text[i]))) ++i;
            if (i < text.size() && text[i] == '/') {
                auto end = i + 1;
                while (end < text.size() && alpha(text[end])) ++end;
                if (findUnit(lower(text.substr(start, end - start)))) i = end;
            }
            token.kind = "name";
            token.text = lower(text.substr(start, i - start));
        } else {
            if (std::string_view("+-*/^%!(),").find(c) == std::string_view::npos)
                fail("character", "Remove this character; use numbers, math operators, and supported names", start + 1);
            token.kind = std::string(1, c);
            ++i;
        }
        out.push_back(std::move(token));
        if (out.size() > 512) fail("limit", "Shorten the expression to at most 512 tokens", start + 1);
    }
    out.push_back({"end", {}, 0, text.size() + 1});
    return out;
}
struct Number {
    double value = 0;
    bool percentage = false;
    const Unit *unit = nullptr;
};
double scalar(Number value, const Token &t) {
    if (value.unit) fail("unit", "Use a number without units for this operation", t.position);
    return value.value;
}
double power(double a, double b, size_t pos) {
    if (a == 0 && b < 0)
        fail("division_by_zero", "Zero cannot have a negative exponent; change the base or exponent", pos);
    if (a < 0 && std::trunc(b) != b)
        fail("domain", "Use a nonnegative base or an integer exponent for a real result", pos);
    return checked(std::pow(a, b), pos);
}
double factorial(double value, size_t pos) {
    if (value < 0 || std::trunc(value) != value) fail("domain", "Use a nonnegative integer for factorial", pos);
    if (value > 170) fail("overflow", "Use a factorial argument no larger than 170", pos);
    double result = 1;
    for (int i = 2; i <= value; ++i) result *= i;
    return result;
}
const std::map<std::string, std::pair<size_t, size_t>> functions{
    {"abs", {1, 1}},  {"floor", {1, 1}}, {"ceil", {1, 1}}, {"sin", {1, 1}},  {"cos", {1, 1}},  {"tan", {1, 1}},
    {"asin", {1, 1}}, {"acos", {1, 1}},  {"atan", {1, 1}}, {"exp", {1, 1}},  {"rad", {1, 1}},  {"deg", {1, 1}},
    {"sqrt", {1, 1}}, {"atan2", {2, 2}}, {"ln", {1, 1}},   {"log", {1, 2}},  {"log2", {1, 1}}, {"log10", {1, 1}},
    {"pow", {2, 2}},  {"fac", {1, 1}},   {"min", {1, 32}}, {"max", {1, 32}}, {"mod", {2, 2}},  {"round", {1, 2}}};
double callFunction(const Token &t, const std::vector<double> &a) {
    const auto &n = t.text;
    const double x = a[0];
    const auto domain = [&](bool valid, std::string message) {
        if (!valid) fail("domain", std::move(message), t.position);
    };
    if (n == "abs") return std::abs(x);
    if (n == "floor") return std::floor(x);
    if (n == "ceil") return std::ceil(x);
    if (n == "sin") return std::sin(x);
    if (n == "cos") return std::cos(x);
    if (n == "tan") return std::tan(x);
    if (n == "atan") return std::atan(x);
    if (n == "exp") return std::exp(x);
    if (n == "rad") return x * std::numbers::pi / 180;
    if (n == "deg") return x * 180 / std::numbers::pi;
    if (n == "sqrt") {
        domain(x >= 0, "Use a nonnegative number for sqrt");
        return std::sqrt(x);
    }
    if (n == "asin" || n == "acos") {
        domain(std::abs(x) <= 1, "Use a number between -1 and 1 for " + n);
        return n == "asin" ? std::asin(x) : std::acos(x);
    }
    if (n == "atan2") {
        domain(x != 0 || a[1] != 0, "Use at least one nonzero coordinate for atan2");
        return std::atan2(x, a[1]);
    }
    if (n == "ln" || n == "log" || n == "log2" || n == "log10") {
        domain(x > 0, "Use a positive number for a logarithm");
        if (n == "ln" || (n == "log" && a.size() == 1)) return std::log(x);
        const double base = n == "log2" ? 2 : n == "log10" ? 10 : a[1];
        domain(base > 0 && base != 1, "Use a positive logarithm base other than 1");
        return std::log(x) / std::log(base);
    }
    if (n == "pow") return power(x, a[1], t.position);
    if (n == "fac") return factorial(x, t.position);
    if (n == "min") return *std::min_element(a.begin(), a.end());
    if (n == "max") return *std::max_element(a.begin(), a.end());
    if (n == "mod") {
        if (a[1] == 0) fail("division_by_zero", "Use a nonzero divisor for mod", t.position);
        double value = std::fmod(x, a[1]);
        if (value != 0 && std::signbit(value) != std::signbit(a[1])) value += a[1];
        return value;
    }
    const double digits = a.size() == 2 ? a[1] : 0;
    domain(std::trunc(digits) == digits && digits >= -15 && digits <= 15,
           "Use an integer from -15 to 15 for rounding digits");
    const double scale = std::pow(10, digits), scaled = checked(std::abs(x) * scale, t.position);
    double whole;
    const double fraction = std::modf(scaled, &whole);
    if (fraction >= .5) whole += 1;
    return (x < 0 ? -whole : whole) / scale;
}
class MathParser {
    std::vector<Token> tokens;
    size_t index = 0, depth = 0;
    const Token &peek(size_t ahead = 0) const { return tokens[std::min(index + ahead, tokens.size() - 1)]; }
    Token take() {
        auto t = peek();
        if (index < tokens.size() - 1) ++index;
        return t;
    }
    void expect(std::string_view kind, std::string message) {
        if (peek().kind != kind) fail("syntax", std::move(message), peek().position);
        take();
    }
    Number prefix() {
        const auto t = take();
        if (t.kind == "number") return {t.value};
        if (t.kind == "name") {
            if (t.text == "pi") return {std::numbers::pi};
            if (t.text == "e") return {std::exp(1.0)};
            const auto fn = functions.find(t.text);
            if (fn == functions.end())
                fail("name", "Unknown name '" + t.text + "'; use pi, e, a supported function or a unit after a number",
                     t.position);
            operation = true;
            expect("(", "Add parentheses to call " + t.text + ", such as " + t.text + "(1)");
            std::vector<double> args;
            if (peek().kind != ")")
                for (;;) {
                    if (args.size() >= 32) fail("limit", "Use at most 32 function arguments", peek().position);
                    args.push_back(scalar(expression(0), t));
                    if (peek().kind != ",") break;
                    take();
                }
            expect(")", "Close the function call with ) and separate arguments with commas");
            if (args.size() < fn->second.first || args.size() > fn->second.second)
                fail("arity",
                     "Use " + std::to_string(fn->second.first) + " to " + std::to_string(fn->second.second) +
                         " argument(s) for " + t.text,
                     t.position);
            return {checked(callFunction(t, args), t.position)};
        }
        if (t.kind == "+" || t.kind == "-") {
            auto value = expression(30);
            if (t.kind == "-") value.value = -value.value;
            return value;
        }
        if (t.kind == "(") {
            auto value = expression(0);
            expect(")", "Close the grouped expression with )");
            return value;
        }
        fail("syntax", "Add a number, constant, function call, or grouped expression here", t.position);
    }
    Number binary(std::string_view op, Number left, Number right, const Token &t) {
        auto a = left.value, b = right.value;
        const Unit *unit = left.unit ? left.unit : right.unit;
        if ((left.unit && left.unit->dimension == "temperature") ||
            (right.unit && right.unit->dimension == "temperature"))
            fail("unit", "Convert temperatures directly; temperature arithmetic is not supported", t.position);
        if (op == "+" || op == "-") {
            if ((left.unit || right.unit) && !(left.unit && right.percentage && !right.unit))
                b = convert(b, right.unit, left.unit, t.position);
        } else if (op == "*" || op == "of" || op == "off") {
            if (left.unit && right.unit)
                fail("unit", "Multiply a quantity by a scalar, or convert compatible units", t.position);
        } else if (op == "/" && right.unit) {
            b = convert(b, right.unit, left.unit, t.position);
            unit = nullptr;
        } else if (op == "^") {
            scalar(left, t);
            scalar(right, t);
        }
        double value = 0;
        bool percentage = false;
        if (op == "+" || op == "-") {
            if (right.percentage && !left.percentage) b = checked(a * b, t.position);
            value = op == "+" ? a + b : a - b;
            percentage = left.percentage && right.percentage;
        } else if (op == "*") value = a * b;
        else if (op == "of" || op == "off") {
            if (!left.percentage) fail("percentage", "Put a percentage before of, such as 15% of 240", t.position);
            value = (op == "off" ? 1 - a : a) * b;
        } else if (op == "/") {
            if (b == 0) fail("division_by_zero", "Use a nonzero divisor", t.position);
            value = a / b;
        } else value = power(a, b, t.position);
        return {checked(value, t.position), percentage, unit};
    }
    Number expression(int minimum) {
        if (++depth > 64) fail("limit", "Reduce expression nesting to at most 64 levels", peek().position);
        auto left = prefix();
        for (;;) {
            const auto t = peek();
            const Unit *suffix = t.kind == "name" ? findUnit(t.text) : nullptr;
            if (functions.contains(t.text) && peek(1).kind == "(") suffix = nullptr;
            if (t.text == "in" && left.unit && findUnit(peek(1).text)) suffix = nullptr;
            if (suffix && 30 > minimum) {
                if (left.unit || left.percentage) fail("unit", "Use one unit after a numeric quantity", t.position);
                take();
                left.unit = suffix;
                operation = true;
            } else if ((t.kind == "!" || t.kind == "%") && 50 > minimum) {
                take();
                scalar(left, t);
                operation = true;
                left = t.kind == "!" ? Number{factorial(left.value, t.position)} : Number{left.value / 100, true};
                if (peek().kind == t.kind)
                    fail("syntax", "Use this postfix operator once, or group the intermediate result explicitly",
                         peek().position);
            } else {
                std::string op = t.kind;
                int binding = 0;
                bool implicit = false;
                if (t.kind == "+" || t.kind == "-") binding = 10;
                else if (t.kind == "*" || t.kind == "/") binding = 20;
                else if (t.kind == "^") binding = 40;
                else if (t.kind == "name" && (t.text == "to" || t.text == "in")) break;
                else if (t.kind == "name" && (t.text == "of" || t.text == "off")) {
                    op = t.text;
                    binding = 20;
                } else if (t.kind == "name" && t.text == "power") {
                    op = "^";
                    binding = 40;
                } else if (t.kind == "(" || t.kind == "name") {
                    op = "*";
                    binding = 20;
                    implicit = true;
                }
                if (binding <= minimum) break;
                operation = true;
                if (!implicit) take();
                left = binary(op, left, expression(op == "^" ? binding - 1 : binding), t);
            }
        }
        --depth;
        return left;
    }

  public:
    bool operation = false;
    explicit MathParser(std::string_view text) : tokens(tokenize(text)) {}
    Number parse() {
        auto value = expression(0);
        if (peek().kind == "name" && (peek().text == "to" || peek().text == "in")) {
            auto t = take();
            auto target = take();
            auto unit = target.kind == "name" ? findUnit(target.text) : nullptr;
            value = {convert(value.value, value.unit, unit, t.position), false, unit};
            operation = true;
        }
        if (peek().kind != "end")
            fail("syntax", "Remove extra input or add an operator; commas only separate function arguments",
                 peek().position);
        return value;
    }
};
std::string formatNumber(double value) {
    if (!std::isfinite(value)) fail("value", "Supply a finite numeric result");
    if (value == 0) return "0";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (std::trunc(value) == value && std::abs(value) <= 9007199254740992.0)
        out << std::fixed << std::setprecision(0) << value;
    else out << std::setprecision(15) << value;
    return out.str();
}
} // namespace
Result evaluate(std::string_view text, int64_t reference) {
    try {
        if (text.size() > 1024) fail("input", "Use an expression of at most 1024 bytes");
        auto display = clean(text);
        auto normalized = lower(display);
        if (temporalForm(normalized)) {
            if (text.size() > 256) fail("input", "Use a date/time expression of at most 256 bytes");
            return datetime(std::move(normalized), reference);
        }
        MathParser parser(text);
        const auto n = parser.parse();
        Result result;
        result.status = Status::Success;
        result.recognize = parser.operation;
        if (n.unit) result.value = Quantity{n.value, n.unit};
        else result.value = Scalar{n.value, n.percentage};
        const auto title = formatNumber(n.value) + (n.unit ? " " + n.unit->symbol : "");
        result.outputs.push_back({"Copy", title, std::move(display), title});
        return result;
    } catch (const Error &error) {
        Result result;
        result.status = Status::Error;
        result.error = error;
        return result;
    }
}
} // namespace calculator
