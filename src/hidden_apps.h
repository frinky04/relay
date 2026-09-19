#pragma once
#include <filesystem>
#include <map>
#include <string>

// Owned and used by the engine worker, like the app catalog.
class HiddenApps {
public:
    explicit HiddenApps(std::filesystem::path file) : m_file(std::move(file)) {}
    std::string load();
    bool contains(const std::string& id) const { return m_apps.contains(id); }
    // Commit memory only after the complete replacement file is saved.
    std::string set(const std::string& id, const std::string& name, bool hidden);
private:
    std::filesystem::path m_file;
    std::map<std::string, std::string> m_apps;
};
