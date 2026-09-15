#pragma once
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

// Standalone TSV launch history. Callers perform load/save off the UI thread.
class Frecency {
public:
    explicit Frecency(std::filesystem::path file) : m_file(std::move(file)) {}
    std::string load(); // empty on success, including a missing history file
    void bump(const std::string& key);
    // Count (capped at 50), weighted by recency: 0..2000, 0 when never launched.
    int score(const std::string& key) const;
    std::string save() const;

private:
    struct Hit { int count = 0; long long last = 0; };
    std::filesystem::path m_file;
    mutable std::mutex m_mtx;
    std::unordered_map<std::string, Hit> m_map;
};
