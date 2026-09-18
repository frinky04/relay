#pragma once
#include "hotkey.h"
#include <windows.h>
#include <filesystem>
#include <string>

// User overrides live in %APPDATA%\relay\init.lua. The catalogue in config.cpp
// supplies defaults, validation and the generated comment reference.
struct Config {
    Config();
    HotkeyBinding hotkey;
    std::string hotkeyText;
    float width;
    int maxRows;
    bool startWithWindows;

    static std::filesystem::path path();
    static std::string reference();
    // Refresh only generated comments; preserve user Lua without executing it.
    static std::string refreshReference(const std::filesystem::path& file);
    // Starts with defaults; valid supplied fields override them. Errors return
    // false with a recovery message. Loading itself never writes the file.
    bool load(std::string& err);
    bool load(const std::filesystem::path& file, std::string& err);
};
