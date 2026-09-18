#include "calculator.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <regex>

namespace calculator::detail {
namespace {
[[noreturn]] void syntax() {
    fail("color",
         "Use #rgb, #rgba, #rrggbb, #rrggbbaa, rgb(...) or hsl(...); optionally finish with to hex, rgb or hsl");
}
struct Channel {
    double value;
    std::string_view unit;
};
class Channels {
    std::string_view text;
    size_t index = 0;

  public:
    explicit Channels(std::string_view input) : text(input) {}
    bool spaces() {
        size_t first = index;
        while (index < text.size() && text[index] == ' ')
            ++index;
        return index != first;
    }
    bool take(char c) {
        spaces();
        if (index == text.size() || text[index] != c)
            return false;
        ++index;
        return true;
    }
    bool end() {
        spaces();
        return index == text.size();
    }
    Channel number() {
        spaces();
        if (index < text.size() && text[index] == '+') {
            ++index;
            if (index < text.size() && text[index] == '-')
                syntax();
        }
        double n = 0;
        auto parsed = std::from_chars(text.data() + index, text.data() + text.size(), n);
        if (parsed.ec != std::errc{} || !std::isfinite(n))
            syntax();
        index = parsed.ptr - text.data();
        size_t first = index;
        while (index < text.size() && ((text[index] >= 'a' && text[index] <= 'z') || text[index] == '%'))
            ++index;
        return {n, text.substr(first, index - first)};
    }
};
double range(double n, double maximum) {
    if (n < 0 || n > maximum)
        fail("color", "Keep RGB channels within 0–255, percentages within 0–100%, and alpha within 0–1");
    return n / maximum;
}
double alpha(Channel c) {
    if (c.unit == "%")
        return range(c.value, 100);
    if (!c.unit.empty())
        syntax();
    return range(c.value, 1);
}
Color fromHsl(double h, double s, double l, double a) {
    // HSL is defined on sRGB channels, without linear-light conversion.
    h = std::fmod(h, 360);
    if (h < 0)
        h += 360;
    double chroma = (1 - std::abs(2 * l - 1)) * s;
    double x = chroma * (1 - std::abs(std::fmod(h / 60, 2) - 1)), m = l - chroma / 2;
    std::array<std::array<double, 3>, 6> sectors{
        {{chroma, x, 0}, {x, chroma, 0}, {0, chroma, x}, {0, x, chroma}, {x, 0, chroma}, {chroma, 0, x}}};
    auto rgb = sectors[std::min(5, int(h / 60))];
    return {rgb[0] + m, rgb[1] + m, rgb[2] + m, a};
}
std::array<double, 3> toHsl(Color c) {
    double high = std::max({c.red, c.green, c.blue}), low = std::min({c.red, c.green, c.blue});
    double delta = high - low, lightness = (high + low) / 2;
    if (delta == 0)
        return {0, 0, lightness * 100};
    double hue = high == c.red     ? (c.green - c.blue) / delta
                 : high == c.green ? (c.blue - c.red) / delta + 2
                                   : (c.red - c.green) / delta + 4;
    hue *= 60;
    if (hue < 0)
        hue += 360;
    double span = lightness <= .5 ? high + low : (1 - high) + (1 - low);
    return {hue, delta / span * 100, lightness * 100};
}
int hexDigit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
Color parseHex(std::string_view text) {
    text.remove_prefix(1);
    if (text.size() != 3 && text.size() != 4 && text.size() != 6 && text.size() != 8)
        syntax();
    bool shortForm = text.size() <= 4;
    size_t count = shortForm ? text.size() : text.size() / 2;
    std::array<double, 4> values{0, 0, 0, 1};
    for (size_t i = 0; i < count; ++i) {
        int high = hexDigit(text[i * (shortForm ? 1 : 2)]);
        int low = shortForm ? high : hexDigit(text[i * 2 + 1]);
        if (high < 0 || low < 0)
            syntax();
        values[i] = (high * 16 + low) / 255.;
    }
    return {values[0], values[1], values[2], values[3]};
}
Color parseFunction(const std::string &text) {
    auto open = text.find('(');
    if (open == text.npos || !text.ends_with(')'))
        syntax();
    auto name = clean(std::string_view(text).substr(0, open));
    bool hsl = name == "hsl" || name == "hsla";
    if (!hsl && name != "rgb" && name != "rgba")
        syntax();
    auto body = std::string_view(text).substr(open + 1, text.size() - open - 2);
    bool commas = body.find(',') != body.npos;
    Channels parser(body);
    std::array<Channel, 3> channels;
    for (size_t i = 0; i < 3; ++i) {
        if (i && !(commas ? parser.take(',') : parser.spaces()))
            syntax();
        channels[i] = parser.number();
    }
    double a = 1;
    if (!parser.end()) {
        if (!parser.take(commas ? ',' : '/'))
            syntax();
        a = alpha(parser.number());
    }
    if (!parser.end())
        syntax();
    if (hsl) {
        auto hue = channels[0];
        if (!hue.unit.empty() && hue.unit != "deg")
            syntax();
        if (channels[1].unit != "%" || channels[2].unit != "%")
            fail("color", "Add % to HSL saturation and lightness, such as hsl(32, 100%, 50%)");
        return fromHsl(hue.value, range(channels[1].value, 100), range(channels[2].value, 100), a);
    }
    std::array<double, 3> rgb;
    for (size_t i = 0; i < 3; ++i) {
        if (!channels[i].unit.empty() && channels[i].unit != "%")
            syntax();
        rgb[i] = range(channels[i].value, channels[i].unit == "%" ? 100 : 255);
    }
    return {rgb[0], rgb[1], rgb[2], a};
}
std::string number(double value) {
    // Three decimals retain 8-bit round trips through RGB, HSL and alpha.
    auto out = std::format("{:.3f}", std::round(value * 1000) / 1000 + 0.0);
    while (out.ends_with('0'))
        out.pop_back();
    if (out.ends_with('.'))
        out.pop_back();
    return out;
}
std::string hex(Color c) {
    auto byte = [](double v) { return int(std::round(std::clamp(v, 0., 1.) * 255)); };
    auto out = std::format("#{:02x}{:02x}{:02x}", byte(c.red), byte(c.green), byte(c.blue));
    if (c.alpha < 1)
        out += std::format("{:02x}", byte(c.alpha));
    return out;
}
std::string rgb(Color c) {
    auto body = number(c.red * 255) + ", " + number(c.green * 255) + ", " + number(c.blue * 255);
    return c.alpha < 1 ? "rgba(" + body + ", " + number(c.alpha) + ")" : "rgb(" + body + ")";
}
std::string hsl(Color c) {
    auto values = toHsl(c);
    auto body = number(values[0]) + ", " + number(values[1]) + "%, " + number(values[2]) + "%";
    return c.alpha < 1 ? "hsla(" + body + ", " + number(c.alpha) + ")" : "hsl(" + body + ")";
}
} // namespace
bool colorForm(const std::string &text) {
    static const std::regex function(R"(^(rgb|rgba|hsl|hsla) *\()");
    return text.starts_with('#') || std::regex_search(text, function);
}
Result colors(const std::string &text) {
    auto split = text.find(" to ");
    if (split == text.npos)
        split = text.find(" in ");
    auto valueText = text.substr(0, split);
    auto target = split == text.npos ? "hex" : text.substr(split + 4);
    if (target != "hex" && target != "rgb" && target != "hsl")
        fail("color", "Choose hex, rgb or hsl as the destination");
    Color value = valueText.starts_with('#') ? parseHex(valueText) : parseFunction(valueText);
    auto hexValue = hex(value), rgbValue = rgb(value), hslValue = hsl(value);
    auto title = target == "hex" ? hexValue : target == "rgb" ? rgbValue : hslValue;
    Result result;
    result.status = Status::Success;
    result.recognize = true;
    result.value = value;
    result.outputs = {{"Copy", title, text, title},
                      {"Copy Hex", "Hex", hexValue, hexValue},
                      {"Copy RGB", "RGB", rgbValue, rgbValue},
                      {"Copy HSL", "HSL", hslValue, hslValue}};
    return result;
}
} // namespace calculator::detail
