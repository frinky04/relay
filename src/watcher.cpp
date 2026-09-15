#include "watcher.h"
#include <algorithm>
#include <cstdint>
#include <map>

void DirWatcher::start(std::vector<std::filesystem::path> dirs, Callback cb) {
    stop();
    m_cb = std::move(cb);
    for (auto& d : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(d, ec)) continue;
        HANDLE h = CreateFileW(d.c_str(), FILE_LIST_DIRECTORY,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (h != INVALID_HANDLE_VALUE) m_watches.push_back({ d, h });
    }
    if (m_watches.empty()) return;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_thread = std::thread([this] { run(); });
}

void DirWatcher::stop() {
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_thread.joinable()) m_thread.join();
    for (auto& w : m_watches) CloseHandle(w.h);
    m_watches.clear();
    if (m_stopEvent) { CloseHandle(m_stopEvent); m_stopEvent = nullptr; }
}

void DirWatcher::run() {
    SetThreadDescription(GetCurrentThread(), L"relay-watch");
    const size_t n = m_watches.size();
    std::vector<std::vector<uint8_t>> bufs(n, std::vector<uint8_t>(16 * 1024));
    std::vector<OVERLAPPED> ov(n);
    std::vector<bool> reading(n, false);
    std::map<std::filesystem::path, ULONGLONG> pending;
    std::vector<HANDLE> events;
    events.push_back(m_stopEvent);
    auto arm = [&](size_t i) {
        ov[i] = {};
        ov[i].hEvent = events[i + 1];
        ResetEvent(ov[i].hEvent);
        reading[i] = ReadDirectoryChangesW(m_watches[i].h, bufs[i].data(), (DWORD)bufs[i].size(), FALSE,
                              FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE,
                              nullptr, &ov[i], nullptr) != FALSE;
    };
    for (size_t i = 0; i < n; ++i) { events.push_back(CreateEventW(nullptr, TRUE, FALSE, nullptr)); arm(i); }

    for (;;) {
        DWORD timeout = INFINITE;
        const auto now = GetTickCount64();
        for (const auto& [path, due] : pending)
            timeout = std::min(timeout, due > now ? static_cast<DWORD>(due - now) : 0UL);
        DWORD r = WaitForMultipleObjects((DWORD)events.size(), events.data(), FALSE, timeout);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) break;
        if (r == WAIT_TIMEOUT) {
            const auto ready = GetTickCount64();
            for (auto it = pending.begin(); it != pending.end();) {
                if (it->second > ready) { ++it; continue; }
                if (WaitForSingleObject(m_stopEvent, 0) == WAIT_OBJECT_0) break;
                m_cb(it->first);
                it = pending.erase(it);
            }
            continue;
        }
        const size_t i = r - WAIT_OBJECT_0 - 1;
        if (i >= n) continue;
        DWORD got = 0;
        if (GetOverlappedResult(m_watches[i].h, &ov[i], &got, FALSE) && got) {
            auto* p = (FILE_NOTIFY_INFORMATION*)bufs[i].data();
            for (;;) {
                std::wstring name(p->FileName, p->FileNameLength / sizeof(wchar_t));
                pending[m_watches[i].dir / name] = GetTickCount64() + 300;
                if (!p->NextEntryOffset) break;
                p = (FILE_NOTIFY_INFORMATION*)((uint8_t*)p + p->NextEntryOffset);
            }
        }
        arm(i);
    }
    for (size_t i = 0; i < n; ++i)
        if (reading[i]) CancelIoEx(m_watches[i].h, &ov[i]);
    for (size_t i = 0; i < n; ++i) {
        // Cancellation only requests completion; keep buffers and events alive until it finishes.
        DWORD got = 0;
        if (reading[i]) GetOverlappedResult(m_watches[i].h, &ov[i], &got, TRUE);
        CloseHandle(events[i + 1]);
    }
}
