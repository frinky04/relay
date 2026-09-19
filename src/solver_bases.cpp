#include "solver_internal.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <regex>

namespace solver::detail {
namespace {
constexpr auto maximum = std::numeric_limits<uint64_t>::max();
[[noreturn]] void overflow(size_t position) {
    fail("overflow", "Keep integer magnitudes at or below 18446744073709551615; use smaller values", position);
}
Integer normalized(Integer value) {
    if (!value.magnitude) value.negative = false;
    return value;
}
Integer negate(Integer value) {
    value.negative = !value.negative;
    return normalized(value);
}
Integer add(Integer a, Integer b, size_t position) {
    if (a.negative == b.negative) {
        if (a.magnitude > maximum - b.magnitude) overflow(position);
        return {a.magnitude + b.magnitude, a.negative};
    }
    if (a.magnitude < b.magnitude) std::swap(a, b);
    return normalized({a.magnitude - b.magnitude, a.negative});
}
Integer multiply(Integer a, Integer b, size_t position) {
    if (b.magnitude && a.magnitude > maximum / b.magnitude) overflow(position);
    return normalized({a.magnitude * b.magnitude, a.negative != b.negative});
}
Integer power(Integer a, Integer b, size_t position) {
    if (b.negative) fail("integer", "Use a nonnegative integer exponent for base arithmetic", position);
    Integer value{1};
    for (auto exponent = b.magnitude; exponent; exponent >>= 1) {
        if (exponent & 1) value = multiply(value, a, position);
        if (exponent > 1) a = multiply(a, a, position);
    }
    return value;
}
int base(std::string_view name) {
    if (name == "bin" || name == "binary") return 2;
    if (name == "oct" || name == "octal") return 8;
    if (name == "dec" || name == "decimal") return 10;
    if (name == "hex" || name == "hexadecimal") return 16;
    return 0;
}
std::string integerText(Integer value, int radix) {
    char digits[65];
    auto end = std::to_chars(digits, digits + sizeof(digits), value.magnitude, radix).ptr;
    std::string prefix = radix == 2 ? "0b" : radix == 8 ? "0o" : radix == 16 ? "0x" : "";
    return (value.negative ? "-" : "") + prefix + std::string(digits, end);
}
class Parser {
    std::string text;
    size_t index = 0, tokens = 0, depth = 0;
    void spaces() {
        while (index < text.size() && (text[index] == ' ' || (text[index] >= '\t' && text[index] <= '\r')))
            ++index;
    }
    char peek() {
        spaces();
        return index < text.size() ? text[index] : '\0';
    }
    void consume() {
        if (++tokens > 512) fail("limit", "Shorten the expression to at most 512 tokens", index + 1);
    }
    void take() {
        consume();
        ++index;
    }
    [[noreturn]] void syntax() const {
        fail("integer", "Use whole numbers, parentheses and + - * / ^; finish with to hex, decimal, octal or binary",
             index + 1);
    }
    Integer prefix() {
        char c = peek();
        if (c == '+' || c == '-') {
            take();
            auto value = expression(30);
            return c == '-' ? negate(value) : value;
        }
        if (c == '(') {
            take();
            auto value = expression(0);
            if (peek() != ')') syntax();
            take();
            return value;
        }
        if (c < '0' || c > '9') syntax();
        consume();
        int radix = 10;
        if (index + 1 < text.size() && c == '0') {
            char p = text[index + 1];
            radix = p == 'x' ? 16 : p == 'b' ? 2 : p == 'o' ? 8 : 10;
            if (radix != 10) index += 2;
        }
        size_t first = index;
        uint64_t value = 0;
        while (index < text.size()) {
            char digit = text[index];
            int n = digit >= '0' && digit <= '9' ? digit - '0' : digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : -1;
            if (n < 0 || n >= radix) break;
            if (value > (maximum - n) / radix) overflow(index + 1);
            value = value * radix + n;
            ++index;
        }
        if (first == index) fail("integer", "Add digits valid for the selected base", index + 1);
        return {value};
    }
    Integer expression(int minimum) {
        if (++depth > 64) fail("limit", "Reduce expression nesting to at most 64 levels", index + 1);
        auto left = prefix();
        for (;;) {
            char op = peek();
            int binding = op == '+' || op == '-' ? 10 : op == '*' || op == '/' ? 20 : op == '^' ? 40 : 0;
            if (binding <= minimum) break;
            size_t position = index + 1;
            take();
            auto right = expression(op == '^' ? binding - 1 : binding);
            if (op == '+')
                left = add(left, right, position);
            else if (op == '-')
                left = add(left, negate(right), position);
            else if (op == '*')
                left = multiply(left, right, position);
            else if (op == '^')
                left = power(left, right, position);
            else {
                if (!right.magnitude) fail("division_by_zero", "Use a nonzero divisor", position);
                if (left.magnitude % right.magnitude)
                    fail("integer", "Use a division with a whole-number result for base arithmetic", position);
                left = normalized({left.magnitude / right.magnitude, left.negative != right.negative});
            }
        }
        --depth;
        return left;
    }
    std::string_view word() {
        spaces();
        size_t first = index;
        while (index < text.size() && text[index] >= 'a' && text[index] <= 'z')
            ++index;
        return std::string_view(text).substr(first, index - first);
    }

  public:
    explicit Parser(std::string_view input) : text(input) {}
    std::pair<Integer, int> parse() {
        auto value = expression(0);
        int target = 10;
        spaces();
        if (index < text.size()) {
            auto conversion = word();
            if (conversion != "in" && conversion != "to") syntax();
            target = base(word());
            if (!target) fail("base", "Choose binary, octal, decimal or hex as the destination", index + 1);
        }
        spaces();
        if (index < text.size()) syntax();
        return {value, target};
    }
};
} // namespace
bool baseForm(const std::string &text) {
    static const std::regex prefix(R"(\b0[xob])");
    static const std::regex suffix(R"( (to|in) (bin(ary)?|oct(al)?|dec(imal)?|hex(adecimal)?)$)");
    return std::regex_search(text, prefix) || std::regex_search(text, suffix);
}
std::string formatInteger(Integer value, int radix) { return integerText(value, radix); }
Solution bases(std::string_view text) {
    auto [value, target] = Parser(text).parse();
    auto requested = target == 2    ? Format::Binary
                     : target == 8  ? Format::Octal
                     : target == 16 ? Format::Hex
                                    : Format::Decimal;
    return {value, true, requested};
}
} // namespace solver::detail
