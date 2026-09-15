#include "config.h"
#include "util.h"
#include <sol/sol.hpp>
#include "fuzzy.h"
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

fs::path Config::path() { return fs::path(dataDir()) / L"init.lua"; }

bool Config::parseHotkey(const std::string& s, UINT& mods, UINT& vk) {
    mods = 0; vk = 0;
    std::string cur;
    auto apply = [&](std::string tok) -> bool {
        tok = fuzzy::lower(tok);
        if (tok == "ctrl" || tok == "control") { mods |= MOD_CONTROL; return true; }
        if (tok == "alt")   { mods |= MOD_ALT; return true; }
        if (tok == "shift") { mods |= MOD_SHIFT; return true; }
        if (tok == "win" || tok == "super") { mods |= MOD_WIN; return true; }
        if (tok == "space") { vk = VK_SPACE; return true; }
        if (tok == "tab")   { vk = VK_TAB; return true; }
        if (tok == "enter" || tok == "return") { vk = VK_RETURN; return true; }
        if (tok == "esc" || tok == "escape") { vk = VK_ESCAPE; return true; }
        if (tok == "`" || tok == "grave" || tok == "backtick") { vk = VK_OEM_3; return true; }
        if (tok.size() == 1 && std::isalnum((unsigned char)tok[0])) { vk = (UINT)std::toupper((unsigned char)tok[0]); return true; }
        if (tok.size() >= 2 && tok[0] == 'f' && std::isdigit((unsigned char)tok[1])) {
            int n = std::atoi(tok.c_str() + 1);
            if (n >= 1 && n <= 24) { vk = VK_F1 + n - 1; return true; }
        }
        return false;
    };
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '+') {
            if (!cur.empty() && !apply(cur)) return false;
            cur.clear();
        } else if (!std::isspace((unsigned char)s[i])) cur += s[i];
    }
    return vk != 0;
}

bool Config::load(std::string& err) {
    *this = Config{};
    err.clear();
    std::error_code ec;
    if (!fs::exists(path(), ec)) return true;

    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::os);
    sol::protected_function_result r = lua.safe_script_file(path().string(), sol::script_pass_on_error);
    if (!r.valid()) { sol::error e = r; err = e.what(); return false; }
    sol::object o = r;
    if (o.get_type() != sol::type::table) { err = "Make init.lua return a settings table"; return false; }
    sol::table t = o;

    if (sol::optional<std::string> hk = t["hotkey"]) {
        UINT m, v;
        if (parseHotkey(*hk, m, v)) { hotkeyMods = m; hotkeyVk = v; hotkeyText = *hk; }
        else err = "Hotkey not understood: " + *hk + "; use a key combination such as Alt+Space";
    }
    if (sol::optional<double> w = t["width"]) width = (float)std::clamp(*w, 320.0, 1600.0);
    if (sol::optional<int> mr = t["max_rows"]) maxRows = std::clamp(*mr, 1, 30);
    return err.empty();
}
