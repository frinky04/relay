#pragma once
#include "command.h"
#include <filesystem>

// Load on the engine worker. Each file returns one command and owns one Lua state.
command::Command loadLuaCommand(const std::filesystem::path& path,
    std::function<std::string(const std::string&)> copy,
    std::function<std::string(const std::string&)> openUrl);
void loadLuaCommands(std::vector<command::Command>& commands, const std::filesystem::path& directory,
    std::function<std::string(const std::string&)> copy,
    std::function<std::string(const std::string&)> openUrl, const std::function<void(std::string)>& report);
