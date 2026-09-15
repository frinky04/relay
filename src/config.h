#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

// %APPDATA%\relay\init.lua, vim-style:
//   return {
//     hotkey      = "alt+space",         -- ctrl/alt/shift/win + key
//     width       = 640,
//     max_rows    = 9,
//     start_with_windows = false,
//   }
struct Config {
    UINT hotkeyMods = MOD_ALT;
    UINT hotkeyVk = VK_SPACE;
    std::string hotkeyText = "alt+space";
    float width = 640.0f;
    int maxRows = 9;
    bool startWithWindows = false;

    static std::filesystem::path path();
    // Resets to defaults, then applies the file if present. Returns false and
    // fills err on a parse error (defaults stay in effect).
    bool load(std::string& err);
    bool load(const std::filesystem::path& file, std::string& err);
    static bool parseHotkey(const std::string& s, UINT& mods, UINT& vk);
};
