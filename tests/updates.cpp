#include "suites.h"
#include "updates.h"
#include "config.h"
#include "desktop.h"
#include "util.h"
#include <Velopack.hpp>
#include <shobjidl.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>

using namespace std::chrono_literals;
namespace fs = std::filesystem;

// Opt-in live HTTPS smoke check; ordinary CTest stays fully headless/offline.
int runUpdateHttpTest(const char* directory) {
    const auto root = fs::absolute(widen(directory));
    fs::create_directories(root);
    const auto file = root / "release.sha256";
    Velopack::VelopackAsset asset{};
    asset.Version = "0.1.0";
    asset.FileName = "relay-0.1.0-windows-x64.zip.sha256";
    asset.Size = 95;
    auto source = releaseUpdateSource({});
    if (!source->DownloadReleaseEntry(asset, narrow(file.wstring()), [](auto) {}))
        throw std::runtime_error("HTTPS download through GitHub redirects failed");
    std::ifstream input(file);
    std::string hash;
    input >> hash;
    if (hash != "ea05676c7cec074f6cc1812cc8f7272eb22efd1d68910ab6b88fbcf8f485346f")
        throw std::runtime_error("HTTPS returned unexpected bytes");
    std::stop_source cancelled;
    cancelled.request_stop();
    auto stopped = releaseUpdateSource(cancelled.get_token());
    if (!stopped->GetReleaseFeed("releases.win.json").empty() ||
        stopped->DownloadReleaseEntry(asset, narrow(file.wstring()), [](auto) {}))
        throw std::runtime_error("Cancelled HTTP work was accepted");
    asset.FileName = "missing-package.nupkg";
    if (source->DownloadReleaseEntry(asset, narrow(file.wstring()), [](auto) {}))
        throw std::runtime_error("Missing HTTP asset was accepted");
    std::printf("HTTPS checks: redirects, exact response, cancellation and missing asset passed\n");
    return 0;
}

int runUpdateTests() {
    int failures = 0;
    auto check = [&](bool ok, const char* description) {
        if (!ok) { ++failures; std::printf("FAIL: %s\n", description); }
    };
    {
        Updates updates;
        updates.start({}, [](auto, auto) {});
        check(!updates.check().empty() && !updates.restart().empty(), "unpackaged builds reject update commands");
    }
    {
        Updates updates;
        std::promise<void> started, release, restarted;
        auto gate = release.get_future().share();
        auto began = started.get_future();
        auto applied = restarted.get_future();
        std::mutex mutex;
        std::condition_variable changed;
        std::vector<std::string> notices;
        std::atomic<int> downloads = 0;
        const auto caller = std::this_thread::get_id();
        std::thread::id downloader, restarter;
        updates.start({[&](std::stop_token) {
            downloader = std::this_thread::get_id();
            if (++downloads == 1) { started.set_value(); gate.wait(); }
            else throw std::runtime_error("offline");
            return std::string("0.2.1");
        }, [&] { restarter = std::this_thread::get_id(); restarted.set_value(); }},
        [&](std::string title, std::string) {
            std::lock_guard lock(mutex);
            notices.push_back(std::move(title));
            changed.notify_one();
        });
        check(began.wait_for(5s) == std::future_status::ready, "startup checks automatically on a worker");
        check(!updates.check().empty() && !updates.restart().empty(), "concurrent checks and premature restarts are rejected");
        release.set_value();
        auto waitNotices = [&](size_t count) {
            std::unique_lock lock(mutex);
            return changed.wait_for(lock, 5s, [&] { return notices.size() >= count; });
        };
        check(waitNotices(1), "completed download reports readiness");
        check(updates.check().empty(), "manual checks can run after the automatic check");
        check(waitNotices(2), "failed downloads report a recovery notice");
        check(updates.restart().empty(), "failed recheck preserves a previously ready update");
        check(applied.wait_for(5s) == std::future_status::ready, "restart dispatches after a verified download");
        updates.stop();
        check(downloader != caller && downloader == restarter, "download and restart stay on the update worker");
        check(notices.size() == 2 && notices[0] == "Update ready" && notices[1] == "Update failed", "worker reports ready and failed states accurately");
    }
    {
        Updates updates;
        std::promise<void> started;
        auto began = started.get_future();
        std::atomic<int> reports = 0, restarts = 0;
        updates.start({[&](std::stop_token stop) {
            std::mutex mutex;
            std::condition_variable_any cancelled;
            std::unique_lock lock(mutex);
            started.set_value();
            cancelled.wait(lock, stop, [] { return false; });
            return std::string("0.2.1");
        }, [&] { ++restarts; }}, [&](auto, auto) { ++reports; });
        check(began.wait_for(5s) == std::future_status::ready, "cancellable download starts");
        updates.stop();
        check(reports == 0 && restarts == 0, "shutdown cancels downloads without notices or restarts");
    }
    const auto temp = fs::temp_directory_path() / ("relay-update-tests-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(temp);
    const auto configFile = temp / "init.lua";
    Config config;
    std::string error;
    { std::ofstream f(configFile); f << "return {start_with_windows = true}"; }
    check(config.load(configFile, error) && config.startWithWindows, "startup option accepts true");
    { std::ofstream f(configFile); f << "return {start_with_windows = 'yes'}"; }
    check(!config.load(configFile, error) && !error.empty(), "invalid startup option produces a repair message");
    { std::ofstream f(configFile); f << "return {}"; }
    check(config.load(configFile, error) && !config.startWithWindows, "startup defaults off when omitted");

    const auto shortcut = temp / "Relay.lnk", target = temp / "fake relay.exe";
    { std::ofstream f(target); f << "fake target; never executed"; }
    check(desktop::writeStartupShortcut(shortcut, target, true).empty(), "startup creates a shortcut with an explicit fake target");
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        Microsoft::WRL::ComPtr<IShellLinkW> link;
        Microsoft::WRL::ComPtr<IPersistFile> file;
        bool loaded = SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
            SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Load(shortcut.c_str(), STGM_READ));
        wchar_t actual[MAX_PATH]{}, arguments[MAX_PATH]{};
        if (loaded) loaded = SUCCEEDED(link->GetPath(actual, MAX_PATH, nullptr, SLGP_RAWPATH)) &&
            SUCCEEDED(link->GetArguments(arguments, MAX_PATH));
        check(loaded && fs::path(actual) == target && std::wstring(arguments) == L"--startup", "shortcut preserves paths with spaces and requests hidden startup");
    }
    if (SUCCEEDED(apartment)) CoUninitialize();
    check(desktop::writeStartupShortcut(shortcut, target, false).empty() && !fs::exists(shortcut), "disabling startup removes its shortcut");
    check(desktop::writeStartupShortcut(shortcut, target, false).empty(), "disabling absent startup is harmless");
    fs::remove_all(temp);
    std::printf("update checks: %d failure(s)\n", failures);
    return failures;
}

// Exercise the real SDK against two packaged versions without launching Relay,
// Update.exe, the installer, or modifying the user's installation/data.
int runUpdateFeedTest(const char* portableRoot, const char* releaseFeed) {
    const auto root = fs::absolute(widen(portableRoot));
    const auto packages = root / "packages";
    fs::create_directories(packages);
    const auto sentinel = root / "settings-preserved.txt";
    { std::ofstream file(sentinel); file << "preserve me"; }
    auto utf8 = [](const fs::path& path) { return narrow(path.wstring()); };
    Velopack::VelopackLocatorConfig locator{utf8(root), utf8(root / "Update.exe"), utf8(packages),
        utf8(root / "current" / "sq.version"), utf8(root / "current"), true};
    Velopack::UpdateManager manager(utf8(fs::absolute(widen(releaseFeed))), nullptr, &locator);
    auto update = manager.CheckForUpdates();
    if (!update) throw std::runtime_error("Fixture must offer a newer version");
    manager.DownloadUpdates(*update);
    const auto pending = manager.UpdatePendingRestart();
    if (!pending || pending->Version != update->TargetFullRelease.Version || !fs::exists(sentinel))
        throw std::runtime_error("Verified update was not staged without touching user files");
    // A failed download must never become a pending update.
    fs::remove(packages / widen(pending->FileName));
    const auto sourcePackage = fs::path(widen(releaseFeed)) / widen(pending->FileName);
    const auto savedPackage = sourcePackage.wstring() + L".saved";
    fs::rename(sourcePackage, savedPackage);
    bool failed = false;
    try { manager.DownloadUpdates(*update); } catch (const std::exception&) { failed = true; }
    if (!failed || manager.UpdatePendingRestart()) throw std::runtime_error("Missing download became installable");
    fs::copy_file(savedPackage, sourcePackage);
    {
        std::fstream corrupt(sourcePackage, std::ios::in | std::ios::out | std::ios::binary);
        corrupt.put('X'); // same size, wrong checksum
    }
    failed = false;
    try { manager.DownloadUpdates(*update); } catch (const std::exception&) { failed = true; }
    fs::remove(sourcePackage);
    fs::rename(savedPackage, sourcePackage);
    if (!failed || manager.UpdatePendingRestart()) throw std::runtime_error("Failed download became installable");
    std::printf("SDK update fixture: verified %s, rejected missing/corrupt packages, preserved settings\n", pending->Version.c_str());
    return 0;
}
