#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

// Windows operations retained from the old feature adapters. Enumeration,
// launching and typing may block; callers must schedule them off the UI thread.
namespace desktop {

struct AppEntry {
    std::string name;
    std::string parsing; // shell:AppsFolder target; also an icon cache key
};

struct WindowTarget {
    HWND hwnd;
    DWORD processId, threadId;
    bool operator==(const WindowTarget&) const = default;
};

struct WindowEntry {
    WindowTarget target;
    std::string title;
    std::string exe;
    std::string exePath;
};

struct ProcessTarget {
    DWORD pid;
    uint64_t created;
    bool operator==(const ProcessTarget&) const = default;
};

struct ProcessEntry {
    ProcessTarget target;
    std::string name, exePath;
};

std::vector<AppEntry> listApps();
std::string launchApp(const std::string& parsing); // empty on success, otherwise a recovery message
std::string openUrl(const std::string& url); // host validates HTTP(S) before dispatch
std::string editTextFile(const std::filesystem::path& path); // default app, then Notepad if opening fails
std::string openFolder(const std::filesystem::path& path);
std::string setStartup(bool enabled);
// Explicit paths allow headless checks to write only to a temporary directory.
std::string writeStartupShortcut(const std::filesystem::path& shortcut,
    const std::filesystem::path& target, bool enabled);
std::vector<WindowEntry> listWindows();
std::string activateWindow(const WindowTarget& target);
std::vector<ProcessEntry> listProcesses();
std::string killProcess(const ProcessTarget& target);
void typeInto(const std::string& text);

} // namespace desktop
