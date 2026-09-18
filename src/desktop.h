#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <functional>

// Windows operations retained from the old feature adapters. Enumeration,
// launching and typing may block; callers must schedule them off the UI thread.
namespace desktop {

struct AppEntry {
    std::string name;
    std::string parsing; // shell:AppsFolder target or shortcut path; also an icon cache key
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
// Explicit folders allow headless checks without reading the user's desktop.
void appendDesktopApps(std::vector<AppEntry>& apps, const std::vector<std::filesystem::path>& folders);
enum class AppAction { Open, Admin, FileLocation, CopyPath };
std::string runApp(const std::string& parsing, AppAction action,
    const std::function<std::string(const std::string&)>& copy);
std::string openUrl(const std::string& url); // host validates HTTP(S) before dispatch
std::string editTextFile(const std::filesystem::path& path); // default app, then Notepad if opening fails
std::string openFolder(const std::filesystem::path& path);
std::string setStartup(bool enabled);
// Explicit paths allow headless checks to write only to a temporary directory.
std::string writeStartupShortcut(const std::filesystem::path& shortcut,
    const std::filesystem::path& target, bool enabled);
std::vector<WindowEntry> listWindows();
enum class WindowAction { Switch, Close, Minimize, Maximize, MoveToOtherMonitor };
std::string runWindow(const WindowTarget& target, WindowAction action);
std::vector<ProcessEntry> listProcesses();
std::string killProcess(const ProcessTarget& target);
enum class SystemAction { Lock, Sleep, Hibernate, SignOut, Restart, Shutdown };
bool canHibernate();
std::string runSystem(SystemAction action);
void typeInto(const std::string& text);

} // namespace desktop
