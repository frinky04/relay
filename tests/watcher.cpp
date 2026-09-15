#include "suites.h"
#include "watcher.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

using namespace std::chrono_literals;

int runWatcherTests() {
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() /
        ("relay-watcher-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    fs::create_directory(directory);
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
    };
    auto write = [&](const char* name, const char* text) {
        std::ofstream file(directory / name);
        file << text;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> values;
    bool primed = false;
    DirWatcher watcher;
    auto callback = [&](const fs::path& path) {
        std::lock_guard lock(mutex);
        if (path.filename() == "probe") primed = true;
        if (path.filename() == "init.lua") {
            std::ifstream file(path);
            values.emplace_back(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        changed.notify_one();
    };
    watcher.start({directory}, callback);
    // start() is asynchronous. Establish that the first directory read is armed.
    for (int attempt = 0; attempt < 10; ++attempt) {
        write("probe", "ready");
        std::unique_lock lock(mutex);
        if (changed.wait_for(lock, 500ms, [&] { return primed; })) break;
    }
    {
        std::lock_guard lock(mutex);
        check(primed, "watcher starts receiving file changes");
    }
    write("init.lua", "first");
    std::this_thread::sleep_for(100ms);
    write("init.lua", "final");
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock, 5s, [&] { return !values.empty(); }), "save burst produces a callback");
        check(values.size() == 1 && values.front() == "final", "save burst reloads the final contents once");
    }
    // Stopping discards pending callbacks and drains the outstanding directory read.
    watcher.stop();
    for (int attempt = 0; attempt < 25; ++attempt) {
        watcher.start({directory}, callback);
        write("init.lua", "cancelled");
        std::this_thread::sleep_for(10ms);
        watcher.stop();
    }
    {
        std::lock_guard lock(mutex);
        check(values.size() == 1, "stop and restart cancel pending callbacks");
    }
    // Every watcher handle must be closed before removing the temporary directory.
    std::error_code error;
    fs::remove(directory / "probe", error);
    fs::remove(directory / "init.lua", error);
    fs::remove(directory, error);
    check(!error, "watcher releases directory handles on stop");
    std::printf("watcher checks: %d failure(s)\n", failures);
    return failures;
}
