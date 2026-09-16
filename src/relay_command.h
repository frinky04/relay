#pragma once
#include "command.h"
#include <filesystem>

command::Command relayCommand(std::filesystem::path config, std::filesystem::path plugins,
    std::string version,
    std::function<std::string(const std::filesystem::path&)> edit,
    std::function<std::string(const std::filesystem::path&)> openFolder,
    std::function<std::string(const std::string&)> copy,
    std::function<std::string()> quit,
    std::function<std::string()> reloadPlugins,
    std::function<std::string()> checkUpdates,
    std::function<std::string()> restartToUpdate,
    std::function<std::string()> rescanApps);
