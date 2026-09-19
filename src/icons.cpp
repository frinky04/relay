#include "icons.h"
#include "util.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

// Version 1 had no version field and incorrectly unpremultiplied Shell icons.
static constexpr uint32_t ICON_CACHE_VERSION = 2;

IconCache& IconCache::instance() { static IconCache c; return c; }

void IconCache::init(ID3D11Device* dev, int px) {
    m_dev = dev;
    m_px = px;
    fs::create_directories(fs::path(dataDir()) / L"icons");
    const int n = 2; // one slow shell call must not block the next icon
    for (int i = 0; i < n; ++i) m_workers.emplace_back([this] { worker(); });
}

void IconCache::shutdown() {
    m_stop = true;
    m_cv.notify_all();
    for (auto& t : m_workers) if (t.joinable()) t.join();
    m_workers.clear();
    clear();
}

ID3D11ShaderResourceView* IconCache::get(const std::string& key) {
    if (key.empty()) return nullptr;
    std::lock_guard lk(m_mtx);
    auto it = m_map.find(key);
    if (it == m_map.end()) {
        m_hot.push_front(key);
        m_map.emplace(key, Entry{}).first->second.queued = m_hot.begin();
        m_cv.notify_one();
        return nullptr;
    }
    Entry& e = it->second;
    if (e.state == Entry::Ready) return e.srv.Get();
    if (e.state == Entry::Cold || e.state == Entry::Queued) {
        // Move the one queued request, preserving constant-time UI lookups.
        m_hot.splice(m_hot.begin(), e.state == Entry::Cold ? m_cold : m_hot, e.queued);
        e.state = Entry::Queued;
        m_cv.notify_one();
    }
    return nullptr;
}

void IconCache::prewarm(std::vector<std::string> keys) {
    std::lock_guard lk(m_mtx);
    for (auto& k : keys) {
        if (k.empty() || m_map.count(k)) continue;
        m_cold.push_back(k);
        auto& entry = m_map.emplace(k, Entry{}).first->second;
        entry.state = Entry::Cold;
        entry.queued = std::prev(m_cold.end());
    }
    m_cv.notify_all();
}

void IconCache::clear() {
    std::lock_guard lk(m_mtx);
    m_map.clear(); m_hot.clear(); m_cold.clear();
}

void IconCache::worker() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        std::string key;
        {
            std::unique_lock lk(m_mtx);
            m_cv.wait(lk, [&] { return m_stop || !m_hot.empty() || !m_cold.empty(); });
            if (m_stop) break;
            auto& queue = m_hot.empty() ? m_cold : m_hot;
            key = std::move(queue.front());
            queue.pop_front();
            m_map.at(key).state = Entry::Loading;
        }

        int w = 0, h = 0;
        std::vector<uint8_t> px;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (loadPixels(key, w, h, px)) srv = makeTexture(w, h, px);

        std::lock_guard lk(m_mtx);
        auto it = m_map.find(key);
        if (it == m_map.end()) continue; // clear() raced us
        it->second.srv = srv;
        it->second.state = srv ? Entry::Ready : Entry::Failed;
    }
    CoUninitialize();
}

// ---------------------------------------------------------------- sources ---

static std::wstring diskName(const std::string& key) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx.bgra", (unsigned long long)std::hash<std::string>{}(key));
    return dataDir() + L"\\icons\\" + widen(buf);
}

bool IconCache::loadFromDisk(const std::string& key, int& w, int& h, std::vector<uint8_t>& bgra) {
    std::ifstream in(diskName(key), std::ios::binary);
    if (!in) return false;
    uint32_t version = 0;
    in.read((char*)&version, 4);
    if (!in || version != ICON_CACHE_VERSION) return false;
    uint32_t ww = 0, hh = 0;
    in.read((char*)&ww, 4); in.read((char*)&hh, 4);
    if (!in || ww == 0 || hh == 0 || ww > 512 || hh > 512) return false;
    bgra.resize((size_t)ww * hh * 4);
    in.read((char*)bgra.data(), bgra.size());
    if (!in) return false;
    w = (int)ww; h = (int)hh;
    return true;
}

void IconCache::saveToDisk(const std::string& key, int w, int h, const std::vector<uint8_t>& bgra) {
    std::ofstream out(diskName(key), std::ios::binary | std::ios::trunc);
    if (!out) return;
    uint32_t ww = w, hh = h;
    out.write((const char*)&ICON_CACHE_VERSION, 4);
    out.write((const char*)&ww, 4); out.write((const char*)&hh, 4);
    out.write((const char*)bgra.data(), bgra.size());
}

bool IconCache::loadPixels(const std::string& key, int& w, int& h, std::vector<uint8_t>& bgra) {
    if (loadFromDisk(key, w, h, bgra)) return true;

    ComPtr<IShellItemImageFactory> fac;
    if (FAILED(SHCreateItemFromParsingName(widen(key).c_str(), nullptr, IID_PPV_ARGS(&fac)))) return false;
    HBITMAP hbm = nullptr;
    SIZE sz{ m_px, m_px };
    if (FAILED(fac->GetImage(sz, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbm)) || !hbm) return false;

    BITMAP bm{};
    GetObject(hbm, sizeof(bm), &bm);
    w = bm.bmWidth; h = bm.bmHeight;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bgra.resize((size_t)w * h * 4);
    HDC dc = GetDC(nullptr);
    GetDIBits(dc, hbm, 0, h, bgra.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    DeleteObject(hbm);

    // ICONONLY pixels already use straight alpha, as ImGui expects. Dividing
    // RGB by alpha again corrupts antialiased edges (and can overflow a byte).
    saveToDisk(key, w, h, bgra);
    return true;
}

ComPtr<ID3D11ShaderResourceView> IconCache::makeTexture(int w, int h, const std::vector<uint8_t>& bgra) {
    ComPtr<ID3D11ShaderResourceView> srv;
    if (!m_dev) return srv;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd{ bgra.data(), (UINT)(w * 4), 0 };
    ComPtr<ID3D11Texture2D> tex;
    if (FAILED(m_dev->CreateTexture2D(&td, &sd, &tex))) return srv;
    m_dev->CreateShaderResourceView(tex.Get(), nullptr, &srv);
    return srv;
}
