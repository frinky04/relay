#include "updates.h"
#include "util.h"
#include <Velopack.hpp>
#include <winhttp.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
constexpr wchar_t repository[] = L"https://github.com/frinky04/relay/releases/";

struct InternetHandle {
    HINTERNET value;
    ~InternetHandle() { if (value) WinHttpCloseHandle(value); }
};

// Velopack 1.2's built-in HTTP sources have no timeout/cancellation controls.
// Keep its feed/package validation, but bound network work during shutdown.
void download(const std::wstring& url, std::stop_token stop,
    const std::function<void(const char*, size_t)>& write) {
    const auto started = GetTickCount64();
    auto require = [&](bool ok) {
        if (!ok || stop.stop_requested() || GetTickCount64() - started > 120000)
            throw std::runtime_error("Update download interrupted");
    };
    require(true);
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    require(WinHttpCrackUrl(url.c_str(), 0, 0, &parts));
    require(parts.nScheme == INTERNET_SCHEME_HTTPS);
    InternetHandle session{WinHttpOpen(L"Relay", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    require(session.value != nullptr);
    require(WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 5000));
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    InternetHandle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    require(connection.value != nullptr);
    const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength);
    InternetHandle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    require(request.value != nullptr);
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    require(WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
    require(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0));
    require(WinHttpReceiveResponse(request.value, nullptr));
    DWORD status = 0, size = sizeof(status);
    require(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX));
    require(status == 200);
    std::array<char, 65536> buffer;
    for (;;) {
        DWORD read = 0;
        require(WinHttpReadData(request.value, buffer.data(), DWORD(buffer.size()), &read));
        if (!read) break;
        write(buffer.data(), read);
    }
}

class ReleaseSource : public Velopack::IUpdateSource {
public:
    explicit ReleaseSource(std::stop_token stop) : m_stop(stop) {}
    const std::string GetReleaseFeed(const std::string releasesName) override {
        // Exceptions must never unwind through the SDK's C/Rust callback boundary.
        try {
            std::string feed;
            download(std::wstring(repository) + L"latest/download/" + widen(releasesName), m_stop,
                [&](const char* data, size_t count) {
                    if (feed.size() + count > 1024 * 1024) throw std::runtime_error("Update feed is too large");
                    feed.append(data, count);
                });
            return feed;
        } catch (...) { return {}; }
    }
    bool DownloadReleaseEntry(const Velopack::VelopackAsset& asset, const std::string localFilePath,
        Velopack::vpkc_progress_send_t progress) override {
        try {
            // Tags pin package downloads even if latest changes after reading the feed.
            if (asset.FileName.find_first_of("/\\?#") != std::string::npos ||
                asset.Version.find_first_not_of("0123456789.") != std::string::npos) return false;
            std::ofstream file(std::filesystem::path(widen(localFilePath)), std::ios::binary | std::ios::trunc);
            if (!file) return false;
            uint64_t written = 0;
            download(std::wstring(repository) + L"download/v" + widen(asset.Version) + L"/" + widen(asset.FileName), m_stop,
                [&](const char* data, size_t count) {
                    written += count;
                    if (written > asset.Size) throw std::runtime_error("Update package is too large");
                    file.write(data, count);
                    if (!file) throw std::runtime_error("Cannot write update package");
                });
            file.close();
            if (!file || written != asset.Size) return false;
            progress(100);
            return true;
        } catch (...) { return false; }
    }
private:
    std::stop_token m_stop;
};
}

std::unique_ptr<Velopack::IUpdateSource> releaseUpdateSource(std::stop_token stop) {
    return std::make_unique<ReleaseSource>(stop);
}

void runVelopack() {
    Velopack::VelopackApp::Build().Run();
}

UpdateBackend velopackUpdates(std::function<void()> quit) {
    if (!std::filesystem::exists(std::filesystem::path(exeDir()) / L"sq.version")) return {};
    struct State {
        std::unique_ptr<Velopack::UpdateManager> manager;
    };
    auto state = std::make_shared<State>();
    return {
        [state](std::stop_token stop) {
            if (!state->manager) {
                state->manager = std::make_unique<Velopack::UpdateManager>(releaseUpdateSource(stop));
            }
            if (auto pending = state->manager->UpdatePendingRestart()) return pending->Version;
            const auto update = state->manager->CheckForUpdates();
            if (stop.stop_requested() || !update) return std::string{};
            state->manager->DownloadUpdates(*update);
            return update->TargetFullRelease.Version;
        },
        [state, quit = std::move(quit)] {
            const auto pending = state->manager->UpdatePendingRestart();
            if (!pending) throw std::runtime_error("No update is ready");
            state->manager->WaitExitThenApplyUpdates(*pending, true, true);
            quit();
        }
    };
}
