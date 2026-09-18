#include "config.h"
#include "util.h"
#include <sol/sol.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <functional>
#include <iomanip>
#include <locale>
#include <limits>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>

namespace fs = std::filesystem;

namespace {
struct Setting {
    const char* name;
    std::string example, help;
    std::function<void(Config&)> reset;
    std::function<std::string(Config&, const sol::object&)> apply;
};

template<class T>
std::string literal(const T& value) {
    if constexpr (std::is_floating_point_v<T>) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if constexpr (std::is_same_v<T, std::string>) {
        out << '"';
        for (unsigned char c : value) {
            if (c == '"' || c == '\\') out << '\\' << char(c);
            else if (c < 32 || c == 127) out << '\\' << std::setw(3) << std::setfill('0') << unsigned(c);
            else out << char(c);
        }
        out << '"';
    }
    else out << std::boolalpha << value;
    return out.str();
}

template<class T>
Setting setting(const char* name, T Config::* member, T value, std::string help,
    std::optional<std::pair<T, T>> limits = {}, std::string (*validate)(const T&) = nullptr) {
    if (limits) help += " (" + literal(limits->first) + " to " + literal(limits->second) + ")";
    return {name, literal(value), std::move(help),
        [member, value](Config& config) { config.*member = value; },
        [name, member, limits, validate](Config& config, const sol::object& object) -> std::string {
            if (!object.valid() || object.get_type() == sol::type::nil) return {};
            T parsed;
            if constexpr (std::is_same_v<T, std::string>) {
                if (object.get_type() != sol::type::string)
                    return "Set " + std::string(name) + " to a quoted string, then save init.lua";
                parsed = object.as<std::string>();
            } else if constexpr (std::is_same_v<T, bool>) {
                if (object.get_type() != sol::type::boolean)
                    return "Set " + std::string(name) + " to true or false, then save init.lua";
                parsed = object.as<bool>();
            } else {
                const std::string error = "Set " + std::string(name) +
                    (std::is_integral_v<T> ? " to a whole number" : " to a finite number") + ", then save init.lua";
                if (object.get_type() != sol::type::number) return error;
                double number = object.as<double>();
                if (!std::isfinite(number)) return error;
                if constexpr (std::is_integral_v<T>) if (std::trunc(number) != number) return error;
                if (limits) number = std::clamp(number, double(limits->first), double(limits->second));
                if (number < double(std::numeric_limits<T>::lowest()) || number > double(std::numeric_limits<T>::max())) return error;
                parsed = static_cast<T>(number);
            }
            if (validate) if (auto error = validate(parsed); !error.empty()) return error;
            config.*member = std::move(parsed);
            return {};
        }};
}

std::string validateHotkey(const std::string& value) {
    return HotkeyBinding::parse(value) ? "" :
        "Hotkey not understood: " + value + "; use win or a shortcut such as alt+space, then save init.lua";
}

// Add settings here. Their type follows the member; the same default and limits
// drive construction, loading and the reference written into init.lua.
const auto& settings() {
    static const std::array catalogue{
        setting<std::string>("hotkey", &Config::hotkeyText, "alt+space", "Shortcut to open or hide Relay; win replaces the Start menu on a standalone Win tap", {}, validateHotkey),
        setting<float>("width", &Config::width, 640.0f, "Window width", std::pair{320.0f, 1600.0f}),
        setting<int>("max_rows", &Config::maxRows, 9, "Visible rows", std::pair{1, 30}),
        setting<bool>("start_with_windows", &Config::startWithWindows, false, "Start hidden when you sign in"),
    };
    return catalogue;
}
}

Config::Config() {
    for (const auto& setting : settings()) setting.reset(*this);
    hotkey = *HotkeyBinding::parse(hotkeyText);
}

std::string Config::reference() {
    std::string text = "-- BEGIN RELAY SETTINGS\n"
        "-- Defaults and help; Relay refreshes this block automatically\n"
        "-- Copy entries into your returned table and remove the leading --\n"
        "-- Omitted settings use defaults; keep your edits outside this block\n";
    for (const auto& setting : settings())
        text += "-- " + std::string(setting.name) + " = " + setting.example + ", -- " + setting.help + "\n";
    return text + "-- END RELAY SETTINGS\n";
}

fs::path Config::path() { return fs::path(dataDir()) / L"init.lua"; }

bool Config::load(std::string& err) {
    return load(path(), err);
}

bool Config::load(const fs::path& file, std::string& err) {
    *this = Config{};
    err.clear();
    std::error_code ec;
    const bool exists = fs::exists(file, ec);
    if (ec) { err = "Cannot read init.lua; check its permissions and save it again"; return false; }
    if (!exists) return true;

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::os);
    sol::protected_function_result r = lua.safe_script_file(file.string(), sol::script_pass_on_error);
    if (!r.valid()) { sol::error e = r; err = e.what(); return false; }
    sol::object o = r;
    if (o.get_type() != sol::type::table) { err = "Make init.lua return a settings table"; return false; }
    sol::table t = o;

    for (const auto& setting : settings()) {
        const auto error = setting.apply(*this, t[setting.name]);
        if (err.empty()) err = error;
    }
    hotkey = *HotkeyBinding::parse(hotkeyText);
    return err.empty();
}
