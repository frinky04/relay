#pragma once
#include <windows.h>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

// Watches directories with ReadDirectoryChangesW. The callback runs on the
// watcher thread with the full path of the changed file; the caller marshals
// it to wherever it needs to go. Changes to each path are coalesced until
// that path has been quiet for 300 ms.
class DirWatcher {
public:
    using Callback = std::function<void(const std::filesystem::path&)>;
    void start(std::vector<std::filesystem::path> dirs, Callback cb);
    void stop();
    ~DirWatcher() { stop(); }

private:
    struct Watch { std::filesystem::path dir; HANDLE h = INVALID_HANDLE_VALUE; };
    std::vector<Watch> m_watches;
    std::thread m_thread;
    HANDLE m_stopEvent = nullptr;
    Callback m_cb;
    void run();
};
