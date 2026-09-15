#include "frecency.h"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <filesystem>

static long long nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string Frecency::load() {
    std::lock_guard lk(m_mtx);
    constexpr auto error = "Cannot read launch history; check frecency.tsv permissions and restart Relay";
    std::error_code ec;
    const bool exists = std::filesystem::exists(m_file, ec);
    if (ec) return error;
    if (!exists) { m_map.clear(); return {}; }
    std::ifstream in(m_file);
    if (!in) return error;
    std::unordered_map<std::string, Hit> loaded;
    std::string line;
    const auto now = nowSec();
    while (std::getline(in, line)) {
        auto t1 = line.find('\t');
        auto t2 = line.find('\t', t1 + 1);
        if (t1 == 0 || t1 == std::string::npos || t2 == std::string::npos ||
            line.find_first_of("\r\n\0", 0, 3) != line.npos) continue;
        Hit h;
        const auto* begin = line.data();
        const auto* end = begin + line.size();
        auto count = std::from_chars(begin + t1 + 1, begin + t2, h.count);
        auto last = std::from_chars(begin + t2 + 1, end, h.last);
        if (count.ec != std::errc{} || count.ptr != begin + t2 || h.count <= 0 ||
            last.ec != std::errc{} || last.ptr != end || h.last < 0) continue;
        h.count = std::min(h.count, 50);
        h.last = std::min(h.last, now);
        loaded[line.substr(0, t1)] = h;
    }
    if (!in.eof()) return error;
    m_map = std::move(loaded);
    return {};
}

void Frecency::bump(const std::string& key) {
    std::lock_guard lk(m_mtx);
    if (key.empty() || key.find_first_of("\t\r\n\0", 0, 4) != key.npos) return;
    auto& h = m_map[key];
    h.count = std::min(h.count + 1, 50);
    h.last = nowSec();
}

int Frecency::score(const std::string& key) const {
    std::lock_guard lk(m_mtx);
    auto it = m_map.find(key);
    if (it == m_map.end()) return 0;
    const long long age = nowSec() - it->second.last;
    // Recency weight: 4 within an hour, 2 within a day, 1 within a week, 0.5 older.
    double w = age < 3600 ? 4.0 : age < 86400 ? 2.0 : age < 7 * 86400 ? 1.0 : 0.5;
    int c = it->second.count > 50 ? 50 : it->second.count;
    return (int)(c * w * 10);
}

std::string Frecency::save() const {
    std::lock_guard lk(m_mtx);
    constexpr auto error = "Cannot save launch history; check frecency.tsv permissions and available disk space";
    std::error_code ec;
    if (!m_file.parent_path().empty()) std::filesystem::create_directories(m_file.parent_path(), ec);
    if (ec) return error;
    auto temp = m_file;
    temp += L".tmp";
    std::ofstream out(temp, std::ios::trunc);
    if (!out) return error;
    for (auto& [k, h] : m_map) out << k << '\t' << h.count << '\t' << h.last << '\n';
    out.close();
    if (out) {
        std::filesystem::rename(temp, m_file, ec);
        if (!ec) return {};
    }
    std::filesystem::remove(temp, ec);
    return error;
}
