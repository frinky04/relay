#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <d3d11.h>
#include <wrl/client.h>

// Shell icon cache. Keys are shell parsing names (file paths or
// "shell:AppsFolder\<AppUserModelId>").
//
// The UI thread never touches the shell: get() is a map lookup that enqueues a
// request on miss. Worker threads resolve icons (disk cache first, then
// IShellItemImageFactory), create the D3D texture themselves (device resource
// creation is thread-safe), and publish it. Requests are served most-recent
// first so rows on screen beat rows scrolled past. prewarm() feeds a low
// priority list so the whole app index is resident before it is ever asked for.
//
// Disk cache: %APPDATA%\relay\icons\<hash>.bgra
// (u32 version, u32 w, u32 h, straight-alpha BGRA rows).
class IconCache {
public:
    static IconCache& instance();

    void init(ID3D11Device* dev, int pixelSize);
    void shutdown();

    // UI thread. Returns texture or nullptr; a miss queues a high-priority load.
    ID3D11ShaderResourceView* get(const std::string& key);
    // Any thread. Queue keys for background loading, lowest priority.
    void prewarm(std::vector<std::string> keys);
    // Drop everything (memory only; disk cache stays).
    void clear();

private:
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        enum { Cold, Queued, Loading, Ready, Failed } state = Queued;
        std::list<std::string>::iterator queued;
    };
    Microsoft::WRL::ComPtr<ID3D11Device> m_dev;
    int m_px = 36;

    std::mutex m_mtx;
    std::condition_variable m_cv;
    std::unordered_map<std::string, Entry> m_map;
    std::list<std::string> m_hot;   // LIFO: front = most recent request
    std::list<std::string> m_cold;  // FIFO prewarm
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{ false };

    void worker();
    bool loadPixels(const std::string& key, int& w, int& h, std::vector<uint8_t>& bgra);
    bool loadFromDisk(const std::string& key, int& w, int& h, std::vector<uint8_t>& bgra);
    void saveToDisk(const std::string& key, int w, int h, const std::vector<uint8_t>& bgra);
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> makeTexture(int w, int h, const std::vector<uint8_t>& bgra);
};
