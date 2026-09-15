#include "desktop.h"
#include "fuzzy.h"
#include "util.h"
#include <dwmapi.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <tlhelp32.h>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

namespace desktop {

namespace {
struct Handle {
    HANDLE value;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
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
DWORD shellOpen(const std::string& target, const wchar_t* parameters = nullptr) {
    Apartment apartment;
    if (FAILED(apartment.result)) return ERROR_NOT_READY;
    std::wstring file = widen(target);
    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = file.c_str();
    sei.lpParameters = parameters;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) ? ERROR_SUCCESS : GetLastError();
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
            while (en->Next(1, &item, nullptr) == S_OK) {
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
        } else throw std::runtime_error("Cannot list installed apps; restart Relay");
    } else throw std::runtime_error("Cannot access installed apps; restart Relay");
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return fuzzy::lower(a.name) < fuzzy::lower(b.name); });
    return out;
}

std::string launchApp(const std::string& parsing) {
    const auto error = shellOpen(parsing);
    if (!error) return {};
    return "Windows could not open this app (" + std::to_string(error) + "); check its installation and try again";
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

std::string activateWindow(const WindowTarget& target) {
    DWORD pid = 0;
    const HWND h = target.hwnd;
    if (!IsWindow(h) || GetWindowThreadProcessId(h, &pid) != target.threadId || pid != target.processId)
        return "Window unavailable; edit the query to refresh windows";
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
