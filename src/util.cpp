#include "util.h"
#include <shlobj.h>

std::wstring dataDir() {
    PWSTR p = nullptr;
    std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) r = p;
    if (p) CoTaskMemFree(p);
    return r + L"\\relay";
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf, n);
    auto slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

#include <cstdarg>
#include <cstdio>
#include <filesystem>

void logf(const char* fmt, ...) {
    static FILE* f = [] {
        std::filesystem::create_directories(dataDir());
        const std::wstring p = dataDir() + L"\\log.txt";
        std::error_code ec;
        const bool big = std::filesystem::file_size(p, ec) > (1u << 20); // cap at 1 MB: start over
        return _wfopen(p.c_str(), big ? L"w" : L"a");
    }();
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fflush(f);
}

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace((unsigned char)s[b])) ++b;
    while (e > b && isspace((unsigned char)s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

std::string clipboardCopy(std::string_view text, HWND owner) {
    std::wstring w = widen(text);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (!h) return "Cannot allocate clipboard text; free memory and try again";
    void* data = GlobalLock(h);
    if (!data) { GlobalFree(h); return "Cannot prepare clipboard text; try again"; }
    memcpy(data, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(h);
    if (!OpenClipboard(owner)) { GlobalFree(h); return "Clipboard is busy; try again"; }
    bool ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, h);
    if (!ok) GlobalFree(h); // Windows owns the allocation only after success
    CloseClipboard();
    return ok ? "" : "Cannot write to the clipboard; try again";
}
