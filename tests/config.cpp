#include "suites.h"
#include "config.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
struct Fixture {
    fs::path root = fs::temp_directory_path() /
        ("relay-config-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    Fixture() {
        if (!fs::create_directory(root)) throw std::runtime_error("Cannot create config fixture");
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
};

void write(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    if (!file) throw std::runtime_error("Cannot write config fixture");
}

std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot read config fixture");
    return {(std::istreambuf_iterator<char>(file)), {}};
}

bool same(const Config& left, const Config& right) {
    return left.hotkeyText == right.hotkeyText && left.hotkeyMods == right.hotkeyMods &&
        left.hotkeyVk == right.hotkeyVk && left.width == right.width &&
        left.maxRows == right.maxRows && left.startWithWindows == right.startWithWindows;
}
}

int runConfigTests() {
    int failures = 0;
    auto check = [&](bool ok, const char* description) {
        if (!ok) { ++failures; std::printf("FAIL: %s\n", description); }
    };
    Fixture fixture;
    const auto path = fixture.root / "init.lua";
    const auto backup = fixture.root / "init.lua.relay-backup";
    const auto reference = Config::reference();
    const Config defaults;
    Config config;
    std::string error;

    check(config.load(path, error) && same(config, defaults) && !fs::exists(path),
        "loading a missing config inherits defaults without writing");
    check(Config::refreshReference(path).empty(), "refresh creates a missing config");
    const auto initial = read(path);
    check(initial.starts_with(reference) && initial.find("return {}") != initial.npos &&
        config.load(path, error) && same(config, defaults), "new config documents settings without pinning defaults");
    const auto timestamp = fs::last_write_time(path);
    check(Config::refreshReference(path).empty() && read(path) == initial &&
        fs::last_write_time(path) == timestamp && !fs::exists(backup), "current reference does not rewrite or create a backup");

    // Every advertised assignment must be copyable together into a Lua table.
    std::istringstream lines(reference);
    std::string line, explicitDefaults = "return {\n";
    while (std::getline(lines, line))
        if (line.starts_with("-- ") && line.find(" = ") != line.npos)
            explicitDefaults += line.substr(3) + "\n";
    explicitDefaults += "}\n";
    write(path, explicitDefaults);
    check(config.load(path, error) && same(config, defaults), "documented defaults are valid Lua and match runtime defaults");

    const std::string userLua = "-- My settings\nlocal size = 400\nreturn {\n"
        "  width = size * 2, -- Keep my comment\n  max_rows = 12,\n"
        "  hotkey = 'ctrl+shift+f12',\n  start_with_windows = true,\n}\n";
    write(path, userLua);
    check(Config::refreshReference(path).empty() && read(path) == reference + userLua && read(backup) == userLua,
        "refresh preserves custom Lua byte for byte and backs up the original");
    check(config.load(path, error) && config.width == 800 && config.maxRows == 12 && config.startWithWindows &&
        config.hotkeyMods == (MOD_CONTROL | MOD_SHIFT) && config.hotkeyVk == VK_F12,
        "computed overrides and configured hotkey still apply");

    const std::string stale = "-- BEGIN RELAY SETTINGS\n-- obsolete = true\n-- END RELAY SETTINGS\n";
    const std::string prefix = "-- Personal heading\n\n";
    write(path, prefix + stale + userLua);
    check(Config::refreshReference(path).empty() && read(path) == prefix + reference + userLua &&
        read(backup) == prefix + stale + userLua,
        "new settings replace stale reference only and refresh an existing backup");
    write(path, reference + "return { max_rows = 17 }\n");
    check(config.load(path, error) && config.maxRows == 17 && config.width == defaults.width &&
        config.hotkeyText == defaults.hotkeyText && config.startWithWindows == defaults.startWithWindows,
        "omitted properties inherit defaults after previously loading custom values");

    write(path, "return { width = 1, max_rows = 999 }");
    check(config.load(path, error) && config.width == 320 && config.maxRows == 30, "numeric limits still clamp");
    write(path, "return { width = 9999, max_rows = -1 }");
    check(config.load(path, error) && config.width == 1600 && config.maxRows == 1, "opposite numeric limits clamp");
    for (const auto* invalid : {"width = '900'", "width = 0/0", "width = 1/0", "max_rows = 2.5",
            "max_rows = false", "hotkey = 12", "hotkey = 'unknown'", "start_with_windows = 'yes'"}) {
        write(path, "return { " + std::string(invalid) + " }");
        check(!config.load(path, error) && !error.empty() && same(config, defaults),
            "invalid setting reports an error and retains its default");
    }
    write(path, "return { width = false, max_rows = 18 }");
    check(!config.load(path, error) && config.width == defaults.width && config.maxRows == 18,
        "an invalid setting does not discard other valid overrides");

    std::string crlfReference;
    for (char c : reference) { if (c == '\n') crlfReference += '\r'; crlfReference += c; }
    const std::string bom = "\xEF\xBB\xBF";
    const std::string shell = "#!/usr/bin/lua\r\n";
    const std::string crlfBody = "-- Personal comment\r\nreturn { width = 700 }\r\n";
    write(path, bom + shell + crlfBody);
    check(Config::refreshReference(path).empty() && read(path) == bom + shell + crlfReference + crlfBody &&
        config.load(path, error) && config.width == 700, "BOM, interpreter line and CRLF remain valid");
    check(Config::refreshReference(path).empty() && read(path) == bom + shell + crlfReference + crlfBody,
        "CRLF reference remains stable across refreshes");

    const std::string embedded = "local text = [[\n" + stale + "]]\nreturn {}\n";
    write(path, embedded);
    check(Config::refreshReference(path).empty() && read(path) == reference + embedded &&
        config.load(path, error), "markers inside Lua strings are never replaced");
    const std::string longComment = "--[[\n" + stale + "]]\nreturn {}\n";
    write(path, longComment);
    check(Config::refreshReference(path).empty() && read(path) == reference + longComment &&
        config.load(path, error), "markers inside long comments are never replaced");

    for (const auto* damaged : {
            "-- BEGIN RELAY SETTINGS\n-- missing end\nreturn {}",
            "-- BEGIN RELAY SETTINGS\nwidth = 700\n-- END RELAY SETTINGS\nreturn {}",
            "-- BEGIN RELAY SETTINGS\n--[[\n-- END RELAY SETTINGS\n]]\nreturn {}"}) {
        write(path, damaged);
        check(!Config::refreshReference(path).empty() && read(path) == damaged,
            "damaged reference never deletes possible user code");
    }
    const std::string invalidLua = "return { width =";
    write(path, invalidLua);
    check(Config::refreshReference(path).empty() && read(path) == reference + invalidLua,
        "refresh preserves invalid Lua without evaluating it");
    check(!config.load(path, error) && !error.empty() && read(path) == reference + invalidLua,
        "invalid Lua remains editable after a load failure");

    write(path, userLua);
    check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY), "mark fixture read-only");
    const auto readOnlyError = Config::refreshReference(path);
    check(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL), "restore fixture permissions");
    check(!readOnlyError.empty() && read(path) == userLua, "read-only file is preserved on failure");
    HANDLE lock = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(lock != INVALID_HANDLE_VALUE, "lock fixture");
    const auto lockedError = Config::refreshReference(path);
    if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
    check(!lockedError.empty() && read(path) == userLua, "locked file is preserved on failure");
    check(!Config::refreshReference(path / "init.lua").empty() && read(path) == userLua,
        "invalid parent path fails without changing existing data");
    const std::string utf16("\xff\xfe" "r\0", 4);
    write(path, utf16);
    check(!Config::refreshReference(path).empty() && read(path) == utf16, "unsupported encoding is preserved");
    const auto nested = fixture.root / "nested" / "init.lua";
    check(Config::refreshReference(nested).empty() && config.load(nested, error) && same(config, defaults),
        "refresh creates missing parent directories");
    for (const auto& entry : fs::directory_iterator(fixture.root))
        check(entry.path().extension() != ".tmp", "refresh failures leave no temporary file");

    std::printf("config checks: %d failure(s)\n", failures);
    return failures;
}
