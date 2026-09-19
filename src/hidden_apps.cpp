#include "hidden_apps.h"
#include <windows.h>
#include <fstream>

namespace {
bool valid(const std::string& value) {
    return !value.empty() && value.find_first_of("\t\r\n\0", 0, 4) == value.npos;
}
}

std::string HiddenApps::load() {
    constexpr auto error = "Cannot read hidden apps; check hidden-apps.tsv permissions and restart Relay";
    std::error_code ec;
    const bool exists = std::filesystem::exists(m_file, ec);
    if (ec) return error;
    if (!exists) { m_apps.clear(); return {}; }
    std::ifstream in(m_file);
    if (!in) return error;
    std::map<std::string, std::string> loaded;
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == line.npos) continue;
        auto id = line.substr(0, tab), name = line.substr(tab + 1);
        if (valid(id) && valid(name)) loaded.emplace(std::move(id), std::move(name));
    }
    if (!in.eof()) return error;
    m_apps = std::move(loaded);
    return {};
}

std::string HiddenApps::set(const std::string& id, const std::string& name, bool hidden) {
    if (!valid(id) || !valid(name)) return "App identity or name is invalid; rescan apps and try again";
    // Re-read before writing so a failed startup load cannot cause us to
    // overwrite the file with an incomplete in-memory list.
    if (auto error = load(); !error.empty()) return error;
    auto next = m_apps;
    if (hidden) next[id] = name;
    else next.erase(id);
    if (next == m_apps) return {};
    constexpr auto error = "Cannot save hidden apps; check hidden-apps.tsv permissions and available disk space";
    std::error_code ec;
    if (!m_file.parent_path().empty()) std::filesystem::create_directories(m_file.parent_path(), ec);
    if (ec) return error;
    auto temp = m_file;
    temp += L".tmp";
    std::ofstream out(temp, std::ios::trunc);
    if (!out) return error;
    for (const auto& [key, title] : next) out << key << '\t' << title << '\n';
    out.close();
    if (out && MoveFileExW(temp.c_str(), m_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        m_apps = std::move(next);
        return {};
    }
    std::filesystem::remove(temp, ec);
    return error;
}
