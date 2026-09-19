#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <d3d11.h>
#include <wrl/client.h>

// Shell icon cache. Keys are shell parsing names (file paths or
// "shell:AppsFolder\<AppUserModelId>").
//
// The UI thread never touches the shell: get() is a map lookup that enqueues a
// request on miss. Worker threads resolve icons (disk cache first, then
// IShellItemImageFactory), create the D3D texture themselves (device resource
// creation is thread-safe). beginFrame publishes results before drawing.
// Requests are served most-recent
// first so rows on screen beat rows scrolled past. prewarm() feeds a low
// priority list so the whole app index is resident before it is ever asked for.
//
// Disk cache: %APPDATA%\relay\icons\<hash>-<size>.bgra
// (u32 version, u32 w, u32 h, straight-alpha BGRA rows).
class IconCache {
public:
    static IconCache& instance();

    void init(ID3D11Device* dev, int pixelSize);
    void shutdown();

    // UI thread, before building draw commands: apply completed loads and DPI.
    void beginFrame(int pixelSize);

    // UI thread. Returns texture or nullptr; a miss queues a high-priority load.
    ID3D11ShaderResourceView* get(const std::string& key);
    // Any thread. Queue keys for background loading, lowest priority.
    void prewarm(std::vector<std::string> keys);
    // UI thread, outside drawing. Drop memory entries and invalidate in-flight loads.
    void clear();

private:
    friend int runIconTests();
    using Clock = std::chrono::steady_clock;
    static constexpr auto CACHE_LIFETIME = std::chrono::hours(24);
    static constexpr auto RETRY_DELAY = std::chrono::seconds(30);
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        enum { Cold, Queued, Loading, Ready, Failed } state = Queued;
        std::list<std::string>::iterator queued;
        Clock::time_point refreshAt{};
    };
    struct Result {
        std::string key;
        uint64_t generation;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Clock::time_point refreshAt;
    };
    Microsoft::WRL::ComPtr<ID3D11Device> m_dev;
    int m_px = 36;
    uint64_t m_generation = 0;
    std::filesystem::path m_directory;
    std::vector<Result> m_completed;

    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::unordered_map<std::string, Entry> m_map;
    std::list<std::string> m_hot;   // LIFO: front = most recent request
    std::list<std::string> m_cold;  // FIFO prewarm
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{ false };

    void worker();
    std::filesystem::path diskName(const std::string& key, int size) const;
    bool loadPixels(const std::string& key, int size, int& w, int& h, std::vector<uint8_t>& bgra, Clock::time_point& expires);
    bool loadFromDisk(const std::string& key, int size, int& w, int& h, std::vector<uint8_t>& bgra, Clock::time_point& expires);
    void saveToDisk(const std::string& key, int size, int w, int h, const std::vector<uint8_t>& bgra);
    static bool downsample(int& w, int& h, std::vector<uint8_t>& bgra, int targetSize);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> makeTexture(int w, int h, const std::vector<uint8_t>& bgra);
};
