#include "relay_command.h"
#include "config.h"

namespace {
std::string ensureDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return error ? "Cannot create the folder; check its permissions and try again" : "";
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
        const auto referenceError = Config::refreshReference(config);
        std::error_code ec;
        if (!referenceError.empty() && !std::filesystem::is_regular_file(config, ec)) return referenceError;
        // A damaged reference or read-only file must still open for repair.
        const auto editError = edit(config);
        return editError.empty() ? referenceError : editError;
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
