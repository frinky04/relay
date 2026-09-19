#include "solver_internal.h"
#include <charconv>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace solver::detail {
const Unit *findUnit(std::string_view name) {
    static const auto units = [] {
        std::map<std::string, Unit, std::less<>> result;
        auto add = [&](const std::string &aliases, const std::string &symbol, const std::string &dimension,
                       double scale, double offset = 0, PeriodField period = PeriodField::None) {
            std::istringstream words(aliases);
            for (std::string word; words >> word;)
                result.emplace(word, Unit{symbol, dimension, scale, offset, period});
        };
        add("m meter meters metre metres", "m", "length", 1, 0);
        add("cm centimeter centimeters centimetre centimetres", "cm", "length", 0.01, 0);
        add("mm millimeter millimeters millimetre millimetres", "mm", "length", 0.001, 0);
        add("km kilometer kilometers kilometre kilometres", "km", "length", 1000, 0);
        add("in inch inches", "in", "length", 0.0254, 0);
        add("ft foot feet", "ft", "length", 0.3048, 0);
        add("yd yard yards", "yd", "length", 0.9144, 0);
        add("mi mile miles", "mi", "length", 1609.344, 0);
        add("kg kilogram kilograms", "kg", "mass", 1, 0);
        add("g gram grams", "g", "mass", 0.001, 0);
        add("mg milligram milligrams", "mg", "mass", 1e-06, 0);
        add("lb lbs pound pounds", "lb", "mass", 0.45359237, 0);
        add("oz ounce ounces", "oz", "mass", 0.028349523125, 0);
        add("c celsius", "C", "temperature", 1, 273.15);
        add("f fahrenheit", "F", "temperature", 0.5555555555555556, 255.3722222222222);
        add("k kelvin", "K", "temperature", 1, 0);
        add("l liter liters litre litres", "L", "volume", 1, 0);
        add("ml milliliter milliliters millilitre millilitres", "mL", "volume", 0.001, 0);
        add("usgal", "USgal", "volume", 3.785411784, 0);
        add("impgal", "impgal", "volume", 4.54609, 0);
        add("usfloz", "USfloz", "volume", 0.0295735295625, 0);
        add("m2 sqm", "m2", "area", 1, 0);
        add("cm2", "cm2", "area", 0.0001, 0);
        add("km2", "km2", "area", 1000000, 0);
        add("ft2 sqft", "ft2", "area", 0.09290304, 0);
        add("in2", "in2", "area", 0.00064516, 0);
        add("ha hectare hectares", "ha", "area", 10000, 0);
        add("acre acres", "acre", "area", 4046.8564224, 0);
        add("m/s mps", "m/s", "speed", 1, 0);
        add("km/h kph kmph", "km/h", "speed", 0.2777777777777778, 0);
        add("mph", "mph", "speed", 0.44704, 0);
        add("knot knots kn", "kn", "speed", 0.5144444444444445, 0);
        add("s sec secs second seconds", "s", "duration", 1, 0, PeriodField::Seconds);
        add("ms millisecond milliseconds", "ms", "duration", 0.001, 0, PeriodField::Seconds);
        add("min mins minute minutes", "min", "duration", 60, 0, PeriodField::Seconds);
        add("h hr hrs hour hours", "h", "duration", 3600, 0, PeriodField::Seconds);
        add("d day days", "d", "duration", 86400, 0, PeriodField::Days);
        add("w wk wks week weeks", "wk", "duration", 604800, 0, PeriodField::Days);
        add("mo month months", "mo", "calendar", 1, 0, PeriodField::Months);
        add("y yr yrs year years", "yr", "calendar", 12, 0, PeriodField::Months);
        add("bit bits", "bit", "data", 0.125, 0);
        add("b byte bytes", "B", "data", 1, 0);
        int power = 0;
        for (char prefix : std::string("kmgt")) {
            ++power;
            std::string key(1, prefix), symbol(1, static_cast<char>(prefix - 'a' + 'A'));
            add(key + "b", symbol + "B", "data", std::pow(1000., power));
            add(key + "ib", symbol + "iB", "data", std::pow(1024., power));
            add(key + "bit", symbol + "bit", "data", std::pow(1000., power) / 8);
        }
        return result;
    }();
    auto found = units.find(name);
    return found == units.end() ? nullptr : &found->second;
}
double convert(double value, const Unit *from, const Unit *to, size_t position) {
    if (from && to && from->dimension != to->dimension &&
        (from->period == PeriodField::Months || to->period == PeriodField::Months) &&
        from->period != PeriodField::None && to->period != PeriodField::None)
        fail("calendar_conversion",
             "Months and years need calendar dates for days or seconds; subtract two dates, such as 2027-01-01 - "
             "2026-01-01",
             position);
    if (!from || !to || from->dimension != to->dimension)
        fail("unit", "Use compatible units, such as ft to cm or min to hours", position);
    return checked((checked(value * from->scale + from->offset, position) - to->offset) / to->scale, position);
}
const Unit *periodUnit(std::string_view name, bool minuteAlias) {
    auto unit = findUnit(minuteAlias && name == "m" ? "min" : name);
    return unit && unit->period != PeriodField::None ? unit : nullptr;
}
PeriodTerms readPeriod(const std::vector<PeriodToken> &tokens, size_t &index) {
    PeriodTerms result;
    std::set<std::string> seen;
    auto term = [&](size_t i) {
        if (i + 1 >= tokens.size()) return false;
        auto unit = periodUnit(tokens[i + 1].text);
        if (!unit || tokens[i].text.empty()) return false;
        double value = 0;
        const auto &text = tokens[i].text;
        auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ptr != text.data() + text.size()) return false;
        if (parsed.ec != std::errc{} || !std::isfinite(value))
            failAt("duration", "Use a smaller finite duration", tokens[i].span);
        if (value < 0) return false;
        if (!seen.insert(unit->symbol).second) failAt("duration", "Use each duration unit once", tokens[i + 1].span);
        result.terms.push_back({value, unit, tokens[i + 1].span});
        return true;
    };
    while (term(index)) {
        index += 2;
        if (index < tokens.size() && (tokens[index].text == "and" || tokens[index].text == ",")) {
            auto separator = tokens[index++].span;
            if (index + 1 >= tokens.size() || !periodUnit(tokens[index + 1].text))
                failAt("duration", "Add a duration after the separator, such as 1h and 30min", separator);
            // The next iteration validates the numeric term without consuming it twice.
            if (tokens[index].text.empty() ||
                !(tokens[index].text[0] == '.' || (tokens[index].text[0] >= '0' && tokens[index].text[0] <= '9')))
                failAt("duration", "Add a positive duration term after the separator", tokens[index].span);
        }
    }
    if (result.terms.empty())
        failAt("duration", "Add a duration such as 8h or 7h30min",
               index < tokens.size() ? tokens[index].span
               : tokens.empty()      ? Span{}
                                     : Span{tokens.back().span.end, tokens.back().span.end});
    return result;
}
CalendarPeriod calendarPeriod(const PeriodTerms &terms) {
    CalendarPeriod result;
    for (const auto &[n, unit, span] : terms.terms) {
        if (n > 4e10) failAt("duration", "Use a smaller duration", span);
        if (unit->period != PeriodField::Seconds && std::floor(n) != n)
            failAt("duration", "Use whole days, weeks, months and years", span);
        double scale = unit->period == PeriodField::Days ? unit->scale / 86400 : unit->scale;
        double value = n * scale;
        if (std::abs(value - std::round(value)) > 1e-6)
            failAt("duration", "Use a duration that resolves to whole seconds", span);
        auto amount = static_cast<int64_t>(std::round(value));
        if (unit->period == PeriodField::Months)
            result.months += amount;
        else if (unit->period == PeriodField::Days)
            result.days += amount;
        else {
            result.seconds += amount;
            result.hasTime = true;
        }
    }
    return result;
}
Quantity periodQuantity(const PeriodTerms &terms) {
    Quantity result{0, terms.terms.front().unit};
    for (const auto &[value, unit, span] : terms.terms)
        result.value = checked(result.value + convert(value, unit, result.unit, span.begin + 1), span.begin + 1);
    return result;
}
Solution timespan(const Source &source) {
    auto expression = source.slice(0, source.text.size() - 12);
    std::vector<PeriodToken> tokens;
    static const std::regex word(R"(^([0-9]+(\.[0-9]+)?|[a-z]+|[+,\-]))");
    for (size_t i = 0; i < expression.text.size();) {
        if (expression.text[i] == ' ') {
            ++i;
            continue;
        }
        std::match_results<std::string::const_iterator> match;
        if (!std::regex_search(expression.text.cbegin() + i, expression.text.cend(), match, word))
            failAt("duration", "Use number–unit duration terms, such as 1h30min", expression.span(i));
        tokens.push_back({match.str(), expression.span(i, match.length())});
        i += match.length();
    }
    size_t index = 0;
    int sign = 1;
    if (!tokens.empty() && (tokens[0].text == "+" || tokens[0].text == "-"))
        sign = tokens[index++].text == "-" ? -1 : 1;
    auto terms = readPeriod(tokens, index);
    for (const auto &term : terms.terms)
        if (term.unit->period == PeriodField::Months)
            failAt("calendar_conversion",
                   "Months and years need calendar dates for an elapsed timespan; subtract two dates, such as "
                   "2027-01-01 - 2026-01-01",
                   term.span);
    auto period = calendarPeriod(terms);
    if (index != tokens.size()) failAt("duration", "Remove extra input after the duration", tokens[index].span);
    return {Duration{sign * (period.days * 86400 + period.seconds)}, true};
}
} // namespace solver::detail
