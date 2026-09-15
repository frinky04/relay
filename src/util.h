#pragma once
#include <windows.h>
#include <string>
#include <string_view>
#include <vector>

inline std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

inline std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// %APPDATA%\relay
std::wstring dataDir();
// Directory containing the running exe.
std::wstring exeDir();

// Appends a line to %APPDATA%\relay\log.txt. Cheap; used for show/hide and failures only.
void logf(const char* fmt, ...);

// Strip leading and trailing ASCII whitespace.
std::string trim(std::string_view s);
// Put UTF-8 text on the clipboard as CF_UNICODETEXT.
std::string clipboardCopy(std::string_view text, HWND owner); // empty on success
