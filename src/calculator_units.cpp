#include "calculator.h"
#include <map>
#include <sstream>
#include <cmath>

namespace calculator::detail {
const Unit *findUnit(std::string_view name) {
    static const auto units = [] {
        std::map<std::string, Unit, std::less<>> result;
        auto add = [&](const std::string &aliases, const std::string &symbol, const std::string &dimension,
                       double scale, double offset = 0) {
            std::istringstream words(aliases);
            for (std::string word; words >> word;) result.emplace(word, Unit{symbol, dimension, scale, offset});
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
        add("s sec secs second seconds", "s", "duration", 1, 0);
        add("ms millisecond milliseconds", "ms", "duration", 0.001, 0);
        add("min mins minute minutes", "min", "duration", 60, 0);
        add("h hr hrs hour hours", "h", "duration", 3600, 0);
        add("d day days", "d", "duration", 86400, 0);
        add("w wk week weeks", "wk", "duration", 604800, 0);
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
    if (!from || !to || from->dimension != to->dimension)
        fail("unit", "Use compatible units, such as ft to cm or min to hours", position);
    return checked((checked(value * from->scale + from->offset, position) - to->offset) / to->scale, position);
}
} // namespace calculator::detail
