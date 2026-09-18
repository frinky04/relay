#include "desktop.h"
#include "fuzzy.h"
#include "util.h"
#include <dwmapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <propkey.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <tlhelp32.h>
#include <unordered_map>
#include <powrprof.h>
#include <reason.h>
#include <optional>

using Microsoft::WRL::ComPtr;

namespace desktop {

namespace {
struct Handle {
    HANDLE value;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

struct ShutdownPrivilege {
    Handle token{};
    TOKEN_PRIVILEGES previous{};
    bool enable() {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token.value))
            return false;
        TOKEN_PRIVILEGES requested{};
        requested.PrivilegeCount = 1;
        requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &requested.Privileges[0].Luid)) return false;
        DWORD size = sizeof(previous);
        return AdjustTokenPrivileges(token.value, FALSE, &requested, sizeof(previous), &previous, &size) &&
            GetLastError() == ERROR_SUCCESS;
    }
    ~ShutdownPrivilege() {
        if (previous.PrivilegeCount)
            AdjustTokenPrivileges(token.value, FALSE, &previous, 0, nullptr, nullptr);
    }
};

uint64_t processCreated(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
    return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

struct Apartment {
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};
DWORD shellOpen(const std::string& target, const wchar_t* parameters = nullptr, const wchar_t* verb = L"open") {
    Apartment apartment;
    if (FAILED(apartment.result)) return ERROR_NOT_READY;
    std::wstring file = widen(target);
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    if (target.starts_with("shell:")) sei.fMask |= SEE_MASK_INVOKEIDLIST;
    sei.lpVerb = verb;
    sei.lpFile = file.c_str();
    sei.lpParameters = parameters;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) ? ERROR_SUCCESS : GetLastError();
}
}

bool canHibernate() {
    SYSTEM_POWER_CAPABILITIES capabilities{};
    return GetPwrCapabilities(&capabilities) && capabilities.SystemS4 && capabilities.HiberFilePresent &&
        capabilities.HiberFileType == HIBERFILE_TYPE_FULL;
}

std::string runSystem(SystemAction action) {
    if (action == SystemAction::Lock)
        return LockWorkStation() ? "" : "Cannot lock Windows; try Win+L";
    if (action == SystemAction::SignOut)
        return ExitWindowsEx(EWX_LOGOFF, 0) ? "" : "Cannot sign out; save your work and try the Windows Start menu";
    if (action == SystemAction::Hibernate && !canHibernate())
        return "Hibernate is unavailable; enable hibernation in Windows or choose Sleep";

    ShutdownPrivilege privilege;
    if (!privilege.enable())
        return "Windows denied the power request; try the Windows Start menu or contact your administrator";
    switch (action) {
    case SystemAction::Sleep:
        return SetSuspendState(FALSE, FALSE, FALSE) ? "" : "Cannot sleep; check Windows power settings and try again";
    case SystemAction::Hibernate:
        return SetSuspendState(TRUE, FALSE, FALSE) ? "" : "Cannot hibernate; check Windows power settings and try again";
    case SystemAction::Restart:
    case SystemAction::Shutdown:
        // Let applications block the request for unsaved work; never force termination.
        return ExitWindowsEx(action == SystemAction::Restart ? EWX_REBOOT : EWX_POWEROFF,
            SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED)
            ? "" : "Cannot shut down or restart; save your work and try the Windows Start menu";
    default:
        return "Unknown system action; select a current /system action and try again";
    }
}

std::string writeStartupShortcut(const std::filesystem::path& shortcut,
    const std::filesystem::path& target, bool enabled) {
    const std::string error = "Cannot change Windows startup; check the Startup folder permissions and save init.lua again";
    if (!enabled) {
        std::error_code ec;
        std::filesystem::remove(shortcut, ec);
        return ec ? error : "";
    }
    Apartment apartment;
    if (FAILED(apartment.result)) return error;
    ComPtr<IShellLinkW> link;
    ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link->SetPath(target.c_str())) || FAILED(link->SetArguments(L"--startup")) ||
        FAILED(link->SetWorkingDirectory(target.parent_path().c_str())) ||
        FAILED(link->SetDescription(L"Start Relay with Windows")) ||
        FAILED(link.As(&file)) || FAILED(file->Save(shortcut.c_str(), TRUE))) return error;
    return {};
}

std::string setStartup(bool enabled) {
    const std::filesystem::path current(exeDir());
    std::error_code ec;
    // Development builds never replace an installed copy's startup shortcut.
    if (!std::filesystem::exists(current / L"sq.version", ec))
        return enabled ? "Windows startup is unavailable in this build; install Relay from GitHub Releases" : "";
    const auto target = current.parent_path() / L"relay.exe";
    if (enabled && !std::filesystem::exists(target, ec))
        return "Cannot find Relay's launcher; reinstall Relay, then save init.lua";
    PWSTR folder = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_CREATE, nullptr, &folder);
    if (FAILED(result)) return "Cannot find the Startup folder; check Windows permissions and save init.lua again";
    const auto shortcut = std::filesystem::path(folder) / L"Relay.lnk";
    CoTaskMemFree(folder);
    return writeStartupShortcut(shortcut, target, enabled);
}

namespace {
struct LaunchSettings {
    std::wstring target, arguments, directory;
    int show = 0;
    DWORD flags = 0;
    bool operator==(const LaunchSettings&) const = default;
};

std::wstring pathKey(const std::filesystem::path& path) {
    auto value = path.lexically_normal().wstring();
    CharLowerBuffW(value.data(), static_cast<DWORD>(value.size()));
    return value;
}

std::optional<LaunchSettings> launchSettings(const std::string& parsing) {
    ComPtr<IShellLinkW> link;
    if (parsing.starts_with("shell:")) {
        ComPtr<IShellItem> item;
        if (FAILED(SHCreateItemFromParsingName(widen(parsing).c_str(), nullptr, IID_PPV_ARGS(&item))) ||
            FAILED(item->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&link)))) return {};
    } else {
        ComPtr<IPersistFile> file;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
            FAILED(link.As(&file)) || FAILED(file->Load(widen(parsing).c_str(), STGM_READ))) return {};
    }
    // Read saved settings only: Resolve can search for moved targets or show UI.
    wchar_t target[MAX_PATH]{}, directory[MAX_PATH]{};
    std::wstring arguments(32768, L'\0');
    LaunchSettings settings;
    ComPtr<IShellLinkDataList> data;
    if (link->GetPath(target, MAX_PATH, nullptr, 0) != S_OK || !target[0] ||
        FAILED(link->GetArguments(arguments.data(), static_cast<int>(arguments.size()))) ||
        FAILED(link->GetWorkingDirectory(directory, MAX_PATH)) || FAILED(link->GetShowCmd(&settings.show)) ||
        FAILED(link.As(&data)) || FAILED(data->GetFlags(&settings.flags))) return {};
    std::filesystem::path path(target);
    std::error_code ec;
    const auto extension = pathKey(path.extension());
    if (!path.is_absolute() || (extension != L".exe" && extension != L".com") ||
        !std::filesystem::is_regular_file(path, ec)) return {};
    settings.target = pathKey(path);
    arguments.resize(wcslen(arguments.c_str()));
    settings.arguments = std::move(arguments);
    settings.directory = pathKey(directory);
    // Storage details (ID lists, icon locations, etc.) do not change launch behavior.
    settings.flags &= SLDF_RUNAS_USER | SLDF_RUN_IN_SEPARATE;
    return settings;
}
}

void appendDesktopApps(std::vector<AppEntry>& apps, const std::vector<std::filesystem::path>& folders) {
    Apartment apartment;
    if (FAILED(apartment.result)) throw std::runtime_error("Cannot read desktop apps; restart Relay");
    std::vector<LaunchSettings> known;
    for (const auto& app : apps)
        if (auto settings = launchSettings(app.parsing)) known.push_back(std::move(*settings));
    for (const auto& folder : folders) {
        std::error_code ec;
        std::vector<std::filesystem::path> shortcuts;
        for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
            if (pathKey(it->path().extension()) == L".lnk" && it->is_regular_file(ec))
                shortcuts.push_back(it->path());
        }
        std::sort(shortcuts.begin(), shortcuts.end());
        for (const auto& shortcut : shortcuts) {
            const auto parsing = narrow(shortcut.wstring());
            auto settings = launchSettings(parsing);
            if (!settings || std::find(known.begin(), known.end(), *settings) != known.end()) continue;
            known.push_back(std::move(*settings));
            apps.push_back({narrow(shortcut.stem().wstring()), parsing});
        }
    }
}

std::vector<AppEntry> listApps() {
    std::vector<AppEntry> out;
    Apartment apartment;
    if (FAILED(apartment.result)) throw std::runtime_error("Cannot read installed apps; restart Relay");
    ComPtr<IShellItem> folder;
    if (SUCCEEDED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder)))) {
        ComPtr<IEnumShellItems> en;
        if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&en)))) {
            ComPtr<IShellItem> item;
            HRESULT next;
            while ((next = en->Next(1, &item, nullptr)) == S_OK) {
                PWSTR disp = nullptr, parse = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &disp)) &&
                    SUCCEEDED(item->GetDisplayName(SIGDN_PARENTRELATIVEPARSING, &parse))) {
                    AppEntry e;
                    e.name = narrow(disp);
                    e.parsing = "shell:AppsFolder\\" + narrow(parse);
                    out.push_back(std::move(e));
                }
                if (disp) CoTaskMemFree(disp);
                if (parse) CoTaskMemFree(parse);
                item.Reset();
            }
            if (FAILED(next)) throw std::runtime_error("Cannot finish listing installed apps; run /relay Rescan Apps again");
        } else throw std::runtime_error("Cannot list installed apps; restart Relay");
    } else throw std::runtime_error("Cannot access installed apps; restart Relay");
    std::vector<std::filesystem::path> desktops;
    for (const auto& id : {FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path))) desktops.emplace_back(path);
        CoTaskMemFree(path);
    }
    appendDesktopApps(out, desktops);
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return fuzzy::lower(a.name) < fuzzy::lower(b.name); });
    return out;
}

std::string runApp(const std::string& parsing, AppAction action,
    const std::function<std::string(const std::string&)>& copy) {
    if (action == AppAction::Open || action == AppAction::Admin) {
        // Ask the shell to invoke the original app entry, retaining shortcut arguments.
        const auto error = shellOpen(parsing, nullptr, action == AppAction::Admin ? L"runas" : L"open");
        if (!error) return {};
        if (error == ERROR_CANCELLED) return "Launch cancelled; try again and approve any Windows prompt";
        return action == AppAction::Admin
            ? "Cannot run this app as administrator; try Open or use its Start menu entry"
            : "Windows could not open this app; check its installation and run /relay Rescan Apps";
    }
    Apartment apartment;
    ComPtr<IShellItem2> item;
    if (FAILED(apartment.result) || FAILED(SHCreateItemFromParsingName(widen(parsing).c_str(), nullptr, IID_PPV_ARGS(&item))))
        return "App unavailable; run /relay Rescan Apps and select it again";
    PWSTR target = nullptr;
    const auto result = item->GetString(PKEY_Link_TargetParsingPath, &target);
    const std::filesystem::path path = SUCCEEDED(result) && target ? target : L"";
    CoTaskMemFree(target);
    // Packaged apps may expose only a shell identity, not a file path.
    if (!path.is_absolute()) return "This app has no file path available; use Open instead";
    if (action == AppAction::CopyPath) return copy(narrow(path.wstring()));
    PIDLIST_ABSOLUTE id = nullptr;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr)))
        return "Cannot find the app's file; run /relay Rescan Apps and try again";
    const auto opened = SHOpenFolderAndSelectItems(id, 0, nullptr, 0);
    CoTaskMemFree(id);
    return SUCCEEDED(opened) ? "" : "Cannot open the file location; check the app's installation and try again";
}

std::string openUrl(const std::string& url) {
    const auto error = shellOpen(url);
    if (!error) return {};
    return "Windows could not open the URL (" + std::to_string(error) + "); set a default browser and try again";
}

std::string editTextFile(const std::filesystem::path& path) {
    if (!shellOpen(narrow(path.wstring()))) return {};
    wchar_t system[MAX_PATH];
    const auto length = GetSystemDirectoryW(system, MAX_PATH);
    if (!length || length >= MAX_PATH) return "Cannot find Notepad; open init.lua in a text editor";
    const auto editor = std::filesystem::path(system) / L"notepad.exe";
    const auto parameters = L"\"" + path.wstring() + L"\"";
    if (!shellOpen(narrow(editor.wstring()), parameters.c_str())) return {};
    return "Cannot open Notepad; open init.lua in a text editor";
}

std::string openFolder(const std::filesystem::path& path) {
    if (!shellOpen(narrow(path.wstring()))) return {};
    return "Cannot open the folder; check its permissions and try again";
}

namespace {

bool isAltTabWindow(HWND h) {
    if (!IsWindowVisible(h)) return false;
    if (GetWindow(h, GW_OWNER) != nullptr) return false;
    LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    if (!(ex & WS_EX_APPWINDOW)) {
        // Windows hides these from alt-tab when they have no app-window flag and a parent chain.
        if (GetAncestor(h, GA_ROOTOWNER) != h) return false;
    }
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return false;
    return GetWindowTextLengthW(h) > 0;
}

} // namespace

std::vector<WindowEntry> listWindows() {
    struct ProcessInfo { std::string exe, path; };
    struct Enumeration {
        std::vector<WindowEntry> found;
        std::unordered_map<DWORD, ProcessInfo> processes;
    } enumeration;
    if (!EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto& enumeration = *reinterpret_cast<Enumeration*>(lp);
        if (!isAltTabWindow(h)) return TRUE;
        DWORD pid = 0;
        const auto thread = GetWindowThreadProcessId(h, &pid);
        if (pid == GetCurrentProcessId()) return TRUE;
        wchar_t buf[512];
        int n = GetWindowTextW(h, buf, 512);
        if (n <= 0 || !pid || !thread) return TRUE;
        WindowEntry w{ {h, pid, thread}, narrow(std::wstring_view(buf, n)) };
        // Metadata is shared only within this enumeration, including failed lookups.
        auto [process, inserted] = enumeration.processes.try_emplace(pid);
        auto& info = process->second;
        if (inserted) {
            Handle p{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
            if (p.value) {
                wchar_t path[MAX_PATH];
                DWORD len = MAX_PATH;
                if (QueryFullProcessImageNameW(p.value, 0, path, &len)) {
                    info.path = narrow(std::wstring_view(path, len));
                    auto slash = info.path.find_last_of("\\/");
                    info.exe = info.path.substr(slash == std::string::npos ? 0 : slash + 1);
                    if (auto dot = info.exe.rfind('.'); dot != std::string::npos) info.exe.erase(dot);
                }
            }
        }
        w.exePath = info.path;
        w.exe = info.exe;
        enumeration.found.push_back(std::move(w));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&enumeration))) throw std::runtime_error("Cannot list open windows; try again");
    return std::move(enumeration.found);
}

std::string runWindow(const WindowTarget& target, WindowAction action) {
    DWORD pid = 0;
    const HWND h = target.hwnd;
    if (!IsWindow(h) || GetWindowThreadProcessId(h, &pid) != target.threadId || pid != target.processId)
        return "Window unavailable; edit the query to refresh windows";
    if (action == WindowAction::Close)
        return PostMessageW(h, WM_CLOSE, 0, 0) ? "" : "Cannot close the window; close it from the app instead";
    if (action == WindowAction::Minimize || action == WindowAction::Maximize)
        return ShowWindowAsync(h, action == WindowAction::Minimize ? SW_MINIMIZE : SW_MAXIMIZE)
            ? "" : "Cannot change the window; use its title bar controls instead";
    if (action == WindowAction::MoveToOtherMonitor) {
        std::vector<MONITORINFO> monitors;
        if (!EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
            MONITORINFO info{sizeof(info)};
            if (!GetMonitorInfoW(monitor, &info)) return FALSE;
            reinterpret_cast<std::vector<MONITORINFO>*>(data)->push_back(info);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors))) return "Cannot read displays; check Windows display settings and try again";
        if (monitors.size() < 2) return "No other monitor available; connect or enable another display";
        std::sort(monitors.begin(), monitors.end(), [](const auto& a, const auto& b) {
            return a.rcMonitor.left != b.rcMonitor.left ? a.rcMonitor.left < b.rcMonitor.left : a.rcMonitor.top < b.rcMonitor.top;
        });
        MONITORINFO source{sizeof(source)};
        WINDOWPLACEMENT placement{sizeof(placement)};
        if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &source) || !GetWindowPlacement(h, &placement))
            return "Cannot read the window position; select the window again";
        const auto current = std::find_if(monitors.begin(), monitors.end(), [&](const auto& info) {
            return EqualRect(&info.rcMonitor, &source.rcMonitor);
        });
        if (current == monitors.end()) return "Displays changed; try moving the window again";
        const auto& destination = monitors[(std::distance(monitors.begin(), current) + 1) % monitors.size()];
        // WINDOWPLACEMENT uses workspace coordinates for ordinary top-level windows.
        const bool tool = (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
        const auto& from = tool ? source.rcWork : source.rcMonitor;
        const auto& to = tool ? destination.rcWork : destination.rcMonitor;
        auto& rect = placement.rcNormalPosition;
        const LONG width = std::min(rect.right - rect.left, destination.rcWork.right - destination.rcWork.left);
        const LONG height = std::min(rect.bottom - rect.top, destination.rcWork.bottom - destination.rcWork.top);
        rect.left = to.left + std::clamp(rect.left - from.left, 0L, destination.rcWork.right - destination.rcWork.left - width);
        rect.top = to.top + std::clamp(rect.top - from.top, 0L, destination.rcWork.bottom - destination.rcWork.top - height);
        rect.right = rect.left + width;
        rect.bottom = rect.top + height;
        placement.ptMaxPosition = {-1, -1};
        placement.ptMinPosition = {-1, -1};
        placement.flags = (placement.flags & WPF_RESTORETOMAXIMIZED) | WPF_ASYNCWINDOWPLACEMENT;
        return SetWindowPlacement(h, &placement) ? "" : "Cannot move the window; try moving it from its title bar";
    }
    if (IsIconic(h) && !ShowWindowAsync(h, SW_RESTORE))
        return "Cannot restore the window; try switching again";
    DWORD fgThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD me = GetCurrentThreadId();
    const bool attached = fgThread && fgThread != me && AttachThreadInput(fgThread, me, TRUE);
    SetForegroundWindow(h);
    if (attached) AttachThreadInput(fgThread, me, FALSE);
    if (GetForegroundWindow() == h) return {};
    return "Windows could not focus the window; try switching again";
}

std::vector<ProcessEntry> listProcesses() {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot list processes; edit the query to try again");
    std::vector<ProcessEntry> found;
    PROCESSENTRY32W entry{sizeof(entry)};
    for (BOOL ok = Process32FirstW(snapshot.value, &entry); ok; ok = Process32NextW(snapshot.value, &entry)) {
        if (entry.th32ProcessID <= 4 || entry.th32ProcessID == GetCurrentProcessId()) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        if (!process.value) continue;
        const auto created = processCreated(process.value);
        if (!created) continue; // A process without a verifiable identity cannot be a kill target.
        wchar_t path[32768];
        DWORD length = static_cast<DWORD>(std::size(path));
        // Use the opened process's name too: the snapshot's PID may have been recycled.
        if (!QueryFullProcessImageNameW(process.value, 0, path, &length)) continue;
        auto exePath = narrow(std::wstring_view(path, length));
        auto name = exePath.substr(exePath.find_last_of("\\/") + 1);
        found.push_back({{entry.th32ProcessID, created}, std::move(name), std::move(exePath)});
    }
    if (GetLastError() != ERROR_NO_MORE_FILES)
        throw std::runtime_error("Cannot list processes; edit the query to try again");
    return found;
}

std::string killProcess(const ProcessTarget& target) {
    const std::string unavailable = "Process unavailable; edit the query to refresh processes";
    if (target.pid == GetCurrentProcessId()) return "Use /relay Quit to exit Relay";
    if (target.pid <= 4 || !target.created) return unavailable;
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, target.pid)};
    if (!process.value) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return unavailable;
        return "Cannot access this process; check permissions in Task Manager";
    }
    // Check and terminate through the same handle; never reopen a PID after validation.
    if (processCreated(process.value) != target.created || WaitForSingleObject(process.value, 0) != WAIT_TIMEOUT)
        return unavailable;
    if (TerminateProcess(process.value, 1)) return {};
    if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) return unavailable;
    return "Cannot kill this process; check permissions in Task Manager";
}

void typeInto(const std::string& text) {
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    std::wstring w = widen(text);
    std::vector<INPUT> in;
    in.reserve(w.size() * 2);
    for (wchar_t c : w) {
        INPUT k{}; k.type = INPUT_KEYBOARD; k.ki.wScan = c; k.ki.dwFlags = KEYEVENTF_UNICODE;
        in.push_back(k);
        k.ki.dwFlags |= KEYEVENTF_KEYUP;
        in.push_back(k);
    }
    if (!in.empty()) SendInput((UINT)in.size(), in.data(), sizeof(INPUT));
}

} // namespace desktop
