#include "app_command.h"
#include "calculator.h"
#include "calculator_command.h"
#include "suites.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <stdexcept>

namespace {
using namespace calculator;
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
Result success(const std::string &text) {
    auto result = evaluate(text, 1789480800);
    check(result.status == Status::Success && result.value && result.recognize, text + ": " + result.error.message);
    return result;
}
std::string copy(const std::string &text, const std::string &action = "Copy") {
    auto result = success(text);
    for (const auto &output : result.outputs)
        if (output.action == action)
            return output.copy;
    throw std::runtime_error("Missing " + action + " for " + text);
}
void expect(const std::string &text, const std::string &expected, const std::string &action = "Copy") {
    auto actual = copy(text, action);
    check(actual == expected, text + ": expected " + expected + "; got " + actual);
}
void reject(const std::string &text) {
    auto result = evaluate(text, 1789480800);
    check(result.status == Status::Error && !result.value && result.outputs.empty() && !result.recognize &&
              !result.error.code.empty() && !result.error.message.empty() && result.error.position <= text.size() + 1,
          "Expected bounded rejection: " + text);
}
} // namespace

int runCalculatorFormatTests() {
    for (auto [text, expected] : {std::pair{"255 to hex", "0xff"},
                                  {"0XFF to decimal", "255"},
                                  {"0b1010 + 5", "15"},
                                  {"0o755", "493"},
                                  {"-255 in hexadecimal", "-0xff"},
                                  {"0xff + 1 to binary", "0b100000000"},
                                  {"255 to octal", "0o377"},
                                  {"(0xff - 15) / 16", "15"},
                                  {"-0xff / -3", "85"},
                                  {"0x2^3^2", "512"},
                                  {"-0x2^2", "-4"},
                                  {"(-0x2)^2", "4"},
                                  {"0x0^0", "1"},
                                  {"0x1^18446744073709551615", "1"},
                                  {"-0x0", "0"},
                                  {"0x0 - 0b0", "0"},
                                  {"0xffffffffffffffff", "18446744073709551615"},
                                  {"18446744073709551615 to hex", "0xffffffffffffffff"},
                                  {"-18446744073709551615 to hex", "-0xffffffffffffffff"},
                                  {"0xffffffffffffffff - 0xfffffffffffffffe", "1"},
                                  {"9007199254740993 to hex", "0x20000000000001"},
                                  {"0x20000000000001 + 2", "9007199254740995"},
                                  {"0x100000000 * 0xffffffff", "18446744069414584320"},
                                  {"0xff + -0x100", "-1"},
                                  {"-0xff + 0x100", "1"}})
        expect(text, expected);
    for (auto text : {"0x",
                      "0b2",
                      "0o8",
                      "0xgg",
                      "0xff junk",
                      "0xff to",
                      "0xff to rgb",
                      "0xff to hex junk",
                      "18446744073709551616 to hex",
                      "0x10000000000000000",
                      "0xffffffffffffffff + 1",
                      "-0xffffffffffffffff - 1",
                      "0xffffffffffffffff * 2",
                      "0x2^64",
                      "0x2^-1",
                      "0xff / 2",
                      "0xff / 0",
                      "1.5 to hex",
                      "1e3 to hex",
                      "sqrt(0xff)",
                      "0xff & 1",
                      "0xff << 1",
                      "0xff 5",
                      "0xff%",
                      "0xff +"})
        reject(text);
    reject(std::string("0xff\0ignored", 12));
    reject(std::string(65, '(') + "0xff" + std::string(65, ')'));
    std::string tooMany = "0x0";
    for (int i = 0; i < 256; ++i)
        tooMany += "+1";
    reject(tooMany);
    check(std::holds_alternative<Scalar>(*evaluate("255", 1789480800).value) && !evaluate("255", 1789480800).recognize,
          "Plain decimal literals retain scalar/search semantics");

    for (auto [text, expected] : {std::pair{"#ff8800 to rgb", "rgb(255, 136, 0)"},
                                  {"#f80", "#ff8800"},
                                  {"#F80 to hsl", "hsl(32, 100%, 50%)"},
                                  {"rgb(255, 136, 0) to hex", "#ff8800"},
                                  {"rgb(100% 0% 0%)", "#ff0000"},
                                  {"  RGB ( 255 , 136 , 0 ) IN HSL  ", "hsl(32, 100%, 50%)"},
                                  {"hsl(32, 100%, 50%)", "#ff8800"},
                                  {"hsl(392deg 100% 50%)", "#ff8800"},
                                  {"hsl(-120, 100%, 50%)", "#0000ff"},
                                  {"#000 to hsl", "hsl(0, 0%, 0%)"},
                                  {"#fff to hsl", "hsl(0, 0%, 100%)"},
                                  {"#808080 to hsl", "hsl(0, 0%, 50.196%)"},
                                  {"rgba(255, 136, 0, 0.5)", "#ff880080"},
                                  {"rgb(255 136 0 / 50%)", "#ff880080"},
                                  {"#f808 to rgb", "rgba(255, 136, 0, 0.533)"},
                                  {"#ff880080 to hsl", "hsla(32, 100%, 50%, 0.502)"},
                                  {"hsla(32, 100%, 50%, .5) to rgb", "rgba(255, 136, 0, 0.5)"},
                                  {"hsl(32 100% 50% / 0%)", "#ff880000"},
                                  {"#00000000 to rgb", "rgba(0, 0, 0, 0)"},
                                  {"#ff0000ff", "#ff0000"},
                                  {"rgb(0 0 0 / 100%)", "#000000"},
                                  {"rgb(1e-100 0 0) to hsl", "hsl(0, 100%, 0%)"}})
        expect(text, expected);
    for (auto text : {"#",
                      "#12",
                      "#12345",
                      "#1234567",
                      "#123456789",
                      "#ggg",
                      "#fff junk",
                      "#fff to",
                      "#fff to binary",
                      "rgb()",
                      "rgb(1,2)",
                      "rgb(1,2,3,4,5)",
                      "rgb(256 0 0)",
                      "rgb(-1 0 0)",
                      "rgb(101% 0% 0%)",
                      "rgb(1, 2 3)",
                      "rgb(1 2, 3)",
                      "rgb(1,2,3 / .5)",
                      "rgb(1 2 3 .5)",
                      "rgb(1 2 3 / 1.1)",
                      "rgb(nan 0 0)",
                      "rgb(inf 0 0)",
                      "rgb(1e309 0 0)",
                      "rgb(1m 2 3)",
                      "rgb(1,2,3) junk",
                      "rgb(+-0 0 0)",
                      "hsl(0, 50, 50)",
                      "hsl(0, 101%, 50%)",
                      "hsl(0, 50%, -1%)",
                      "hsl(0rad 50% 50%)",
                      "hsl(0% 50% 50%)",
                      "hsla(0, 50%, 50%, -0.1)",
                      "#fff to hsl to rgb"})
        reject(text);
    reject("#fff" + std::string(253, ' '));

    // Check full-width integers without converting their values to floating point.
    uint64_t random = 953;
    for (int i = 0; i < 500; ++i) {
        random = random * 6364136223846793005ULL + 1;
        auto input = std::to_string(random) + " to hex";
        auto result = success(input);
        for (const auto &out : result.outputs) {
            auto parsed = success(out.copy + " to decimal");
            auto number = std::get<Integer>(*parsed.value);
            check(number.magnitude == random && !number.negative, "Exact base round trip: " + out.copy);
        }
        int64_t a = int64_t(random % 2000001) - 1000000, b = int64_t((random >> 32) % 2000001) - 1000000;
        auto left = copy(std::to_string(a) + " to hex"), right = copy(std::to_string(b) + " to hex");
        expect("(" + left + ") + (" + right + ")", std::to_string(a + b));
        expect("(" + left + ") - (" + right + ")", std::to_string(a - b));
        expect("(" + left + ") * (" + right + ")", std::to_string(a * b));
    }
    // Every alpha byte and a deterministic color sample round-trip through each format.
    for (unsigned i = 0; i < 1024; ++i) {
        random = random * 6364136223846793005ULL + 1;
        auto input = std::format("#{:06x}{:02x}", unsigned(random & 0xffffff), i % 256);
        auto canonical = copy(input);
        for (auto action : {"Copy RGB", "Copy HSL"})
            expect(copy(input, action), canonical);
    }
    std::vector<std::string> pieces{"#", "rgb(", "hsl(", ")",  "0x",  "0b", "0o", "1",  "f",
                                    ",", "/",    "%",    "to", "hex", "-",  "+",  "nan"};
    for (int i = 0; i < 3000; ++i) {
        std::string text;
        for (int j = 0; j < 8; ++j) {
            random = random * 6364136223846793005ULL + 1;
            text += pieces[random % pieces.size()] + " ";
        }
        auto result = evaluate(text, 1789480800);
        check((result.status == Status::Success && result.value && !result.outputs.empty()) ||
                  (result.status == Status::Error && !result.error.message.empty()),
              "Malformed format input lost result/error");
    }

    std::string copied, copyError;
    int copies = 0;
    auto cmd = calculatorCommand([&](const std::string &value) {
        ++copies;
        copied = value;
        return copyError;
    });
    check(command::validate(cmd).empty(), "Extended calculator declaration validates");
    std::vector<command::Command> catalog{
        cmd, appCommand({{"red", "fake-red"}, {"#ff8800", "fake-color"}, {"0xff", "fake-integer"}},
                        [](const auto &, auto) { return std::string{}; })};
    auto query = [&](const std::string &text) { return command::evaluate(catalog, text, {1789480800}); };
    auto color = query("/calc #ff880080");
    check(copies == 0 && color.view.rows.size() == 4 && color.view.rows[0].colorSwatch == 0xff880080U &&
              color.view.rows[1].actionLabel == "Copy Hex" && color.view.rows[2].actionLabel == "Copy RGB" &&
              color.view.rows[3].actionLabel == "Copy HSL",
          "Color previews expose formats and packed swatch without copying");
    check(!color.view.rows[1].colorSwatch && color.view.rows[0].completion.empty(),
          "Swatch accompanies primary answer; Tab stays unchanged");
    auto integer = query("/calc 0xff");
    check(integer.view.rows.size() == 5 && integer.view.rows[1].actionLabel == "Copy Binary" &&
              integer.view.rows[4].actionLabel == "Copy Hex" && !integer.view.rows[0].colorSwatch,
          "Base formats retain order and omit color swatches");
    for (size_t i = 0; i < integer.actions.size(); ++i) {
        check(integer.actions[i]().empty(), "Integer copy succeeds");
        check(copied == (i ? integer.view.rows[i].subtitle : integer.view.rows[i].title),
              "Integer action copies displayed exact value");
    }
    query("/calc #fff");
    copyError = "Clipboard busy; try again";
    check(color.actions[2]() == copyError, "Color copy reports clipboard failure");
    copyError.clear();
    catalog.clear();
    check(color.actions[2]().empty() && copied == "rgba(255, 136, 0, 0.502)",
          "Color copy survives queries, retries and catalog replacement");
    catalog = {cmd, appCommand({{"red", "fake-red"}, {"#ff8800", "fake-color"}, {"0xff", "fake-integer"}},
                               [](const auto &, auto) { return std::string{}; })};
    for (auto text : {"#ff8800", "0xff"}) {
        auto bare = query(text);
        check(bare.view.rows.size() == 2 && bare.view.rows[0].context == "/calc" && bare.view.rows[1].title == text,
              "Explicit numeric/color prefixes preserve matching apps");
    }
    check(query("red").view.rows.size() == 1 && query("red").view.rows[0].title == "red",
          "Named colors remain app searches");
    for (auto text : {"#ffg", "0xgg", "rgb(1 2)", "hsl(0 0 0)", "0xff / 2", "#fff Copy"}) {
        check(query(text).view.rows.empty(), "Invalid bare formats remain search");
        auto explicitResult = query(std::string("/calc ") + text);
        check(explicitResult.view.rows.size() == 1 && explicitResult.view.rows[0].kind == "Error" &&
                  !explicitResult.actions[0] && !explicitResult.view.rows[0].colorSwatch,
              "Invalid scoped formats have one recovery row");
    }
    return 0;
}
