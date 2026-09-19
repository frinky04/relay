#include "suites.h"
#include "icons.h"
#include <cstdio>
#include <fstream>

// Fake icon keys and a software D3D device; no Shell queries or launcher window.
int runIconTests() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; std::printf("FAIL: %s\n", message); }
    };
    namespace fs = std::filesystem;
    using Clock = IconCache::Clock;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(com), "COM initializes for the Windows image scaler");
    {
        int width = 2, height = 2;
        // One opaque red pixel, three invisible blue pixels.
        std::vector<uint8_t> image{0, 0, 255, 255, 255, 0, 0, 0,
            255, 0, 0, 0, 255, 0, 0, 0};
        check(IconCache::downsample(width, height, image, 1) && width == 1 && height == 1 &&
            image[0] == 0 && image[1] == 0 && image[2] >= 254 && image[3] >= 63 && image[3] <= 64,
            "downsampling preserves red coverage without invisible blue bleeding into the edge");
    }
    {
        int width = 9, height = 1;
        std::vector<uint8_t> image(9 * 4, 0);
        for (int x = 0; x < 9; ++x) image[x * 4 + 3] = 255;
        image[0] = image[1] = image[2] = 255;
        check(IconCache::downsample(width, height, image, 1) && width == 1 && height == 1 &&
            image[0] >= 28 && image[0] <= 29 && image[1] == image[0] && image[2] == image[0] && image[3] == 255,
            "strong reduction includes edge detail that a center bilinear sample would miss");
    }
    {
        int width = 5, height = 3;
        std::vector<uint8_t> image(5 * 3 * 4, 255);
        check(IconCache::downsample(width, height, image, 2) && width == 2 && height == 1 &&
            image == std::vector<uint8_t>(8, 255), "odd rectangular sources retain aspect and solid color");
        const auto original = image;
        check(IconCache::downsample(width, height, image, 18) && width == 2 && height == 1 && image == original,
            "small sources are not upscaled or converted unnecessarily");
        check(!IconCache::downsample(width, height, image, 0) && image == original,
            "invalid target size leaves input intact");
        image.pop_back();
        check(!IconCache::downsample(width, height, image, 1), "incomplete source pixels cannot reach the scaler");
    }
    const auto directory = fs::temp_directory_path() / ("relay-icons-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(directory);
    IconCache cache;
    cache.m_directory = directory;
    const std::string key = "fake-icon";
    const std::vector<uint8_t> pixels{255, 255, 255, 64};
    int w = 0, h = 0;
    std::vector<uint8_t> loaded;
    Clock::time_point expires;
    cache.saveToDisk(key, 36, 1, 1, pixels);
    check(cache.loadFromDisk(key, 36, w, h, loaded, expires) && w == 1 && h == 1 && loaded == pixels,
        "disk cache preserves straight-alpha pixels exactly");
    check(!cache.loadFromDisk(key, 72, w, h, loaded, expires), "different DPI cannot reuse the smaller cached request");
    const auto path = cache.diskName(key, 36);
    fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(23));
    check(cache.loadFromDisk(key, 36, w, h, loaded, expires) && expires - Clock::now() <= std::chrono::hours(1),
        "loading an old disk entry does not restart its full lifetime");
    fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(25));
    check(!cache.loadFromDisk(key, 36, w, h, loaded, expires), "expired disk icons are refreshed");
    cache.saveToDisk(key, 36, 1, 1, pixels);
    fs::resize_file(path, 13);
    check(!cache.loadFromDisk(key, 36, w, h, loaded, expires), "truncated pixels are rejected");
    cache.saveToDisk(key, 36, 513, 1, pixels);
    check(!cache.loadFromDisk(key, 36, w, h, loaded, expires), "invalid dimensions are rejected before allocation");
    cache.m_directory = directory / "missing";
    cache.saveToDisk(key, 36, 1, 1, pixels);
    check(!cache.loadFromDisk(key, 36, w, h, loaded, expires), "unavailable disk cache is a nonfatal miss");

    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &cache.m_dev, nullptr, nullptr)), "software D3D device initializes");
    auto texture = cache.makeTexture(1, 1, pixels);
    check(texture != nullptr, "software device creates the test icon");
    cache.get(key);
    check(cache.m_hot.size() == 1, "first lookup queues one load");
    cache.get(key);
    check(cache.m_hot.size() == 1, "repeated lookup does not duplicate a queued request");
    cache.m_hot.clear();
    cache.m_map.at(key).state = IconCache::Entry::Loading;
    cache.m_completed.push_back({key, cache.m_generation, texture, Clock::now() + std::chrono::hours(1)});
    check(cache.get(key) == nullptr, "worker completion stays unpublished during drawing");
    cache.beginFrame(36);
    check(cache.get(key) == texture.Get(), "next frame publishes the completed texture");
    cache.m_map.at(key).refreshAt = Clock::now() - std::chrono::seconds(1);
    check(cache.get(key) == texture.Get() && cache.m_hot.size() == 1,
        "expired memory entry stays visible while a single refresh queues");
    cache.m_hot.clear();
    cache.m_map.at(key).state = IconCache::Entry::Loading;
    cache.m_completed.push_back({key, cache.m_generation, {}, Clock::now() + cache.RETRY_DELAY});
    cache.beginFrame(36);
    check(cache.get(key) == texture.Get() && cache.m_hot.empty(), "failed refresh retains the good texture and delays retry");

    const auto oldGeneration = cache.m_generation;
    cache.beginFrame(72);
    check(cache.m_px == 72 && cache.get(key) == nullptr, "DPI change invalidates in-memory icons");
    cache.m_completed.push_back({key, oldGeneration, texture, Clock::now() + std::chrono::hours(1)});
    cache.beginFrame(72);
    check(cache.get(key) == nullptr && cache.m_map.at(key).state == IconCache::Entry::Queued,
        "late completion at the old DPI cannot replace a new request for the same icon");
    cache.m_hot.clear();
    cache.m_map.at(key).state = IconCache::Entry::Loading;
    cache.m_completed.push_back({key, cache.m_generation, {}, Clock::now() + cache.RETRY_DELAY});
    cache.beginFrame(72);
    check(cache.get(key) == nullptr && cache.m_hot.empty(), "failed first load does not retry each frame");
    cache.m_map.at(key).refreshAt = Clock::now() - std::chrono::seconds(1);
    cache.get(key);
    cache.get(key);
    check(cache.m_hot.size() == 1, "failed first load retries once after the delay");
    cache.clear();
    fs::remove(path);
    fs::remove(directory);
    if (SUCCEEDED(com)) CoUninitialize();
    return failures;
}
