#include "relay_command.h"
#include <windows.h>

namespace {
std::string ensureDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return error ? "Cannot create the folder; check its permissions and try again" : "";
}

std::string ensureConfig(const std::filesystem::path& path) {
    auto error = ensureDirectory(path.parent_path());
    if (!error.empty()) return error;
    // Exclusive creation preserves existing configuration, including invalid Lua.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const auto code = GetLastError();
        if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) return {};
        return "Cannot create init.lua; check its permissions and try again";
    }
    constexpr char initial[] = "-- Save this file to apply settings\nreturn {}\n";
    DWORD written = 0;
    bool ok = WriteFile(file, initial, sizeof(initial) - 1, &written, nullptr) && written == sizeof(initial) - 1;
    CloseHandle(file);
    return ok ? "" : "Cannot write init.lua; check available disk space and edit the file to return a settings table";
}
}

command::Command relayCommand(std::filesystem::path config, std::filesystem::path plugins,
    std::string version,
    std::function<std::string(const std::filesystem::path&)> edit,
    std::function<std::string(const std::filesystem::path&)> openFolder,
    std::function<std::string(const std::string&)> copy,
    std::function<std::string()> quit,
    std::function<std::string()> reloadPlugins,
    std::function<std::string()> checkUpdates,
    std::function<std::string()> restartToUpdate) {
    command::Command cmd{"relay", "Configure and manage Relay"};
    cmd.search = true;
    cmd.verbs.push_back({"Edit Config", false, [config = std::move(config), edit = std::move(edit)](auto&) {
        auto error = ensureConfig(config);
        return error.empty() ? edit(config) : error;
    }, "Open init.lua in your default app; saves apply automatically"});
    cmd.verbs.push_back({"Open Plugins Folder", false, [plugins = std::move(plugins), openFolder = std::move(openFolder)](auto&) {
        auto error = ensureDirectory(plugins);
        return error.empty() ? openFolder(plugins) : error;
    }, "Open your Lua plugins; run Reload Plugins after changes"});
    cmd.verbs.push_back({"Reload Plugins", false, [reloadPlugins = std::move(reloadPlugins)](auto&) {
        return reloadPlugins();
    }, "Reload bundled and user Lua commands after editing plugins"});
    const std::string label = "Relay " + version;
    cmd.verbs.push_back({"Copy Version", false, [label, copy = std::move(copy)](auto&) {
        return copy(label);
    }, label});
    cmd.verbs.push_back({"Check for Updates", false, [checkUpdates = std::move(checkUpdates)](auto&) {
        return checkUpdates();
    }, "Check for a new release and download it in the background"});
    cmd.verbs.push_back({"Restart to Update", false, [restartToUpdate = std::move(restartToUpdate)](auto&) {
        return restartToUpdate();
    }, "Apply the downloaded update and restart Relay"});
    cmd.verbs.push_back({"Quit", false, [quit = std::move(quit)](auto&) {
        return quit();
    }, "Exit Relay; launch it again to use the hotkey"});
    return cmd;
}
