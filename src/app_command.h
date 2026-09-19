#pragma once
#include "command.h"
#include "desktop.h"
#include "frecency.h"

struct AppSearchOptions {
    std::function<bool(const std::string&)> hidden;
    std::function<std::string(const std::string&, const std::string&, bool)> setHidden;
};
std::string appSearchId(const desktop::AppEntry& app);

command::Command appCommand(std::vector<desktop::AppEntry> apps,
    std::function<std::string(const std::string&, desktop::AppAction)> run,
    std::shared_ptr<Frecency> history = {}, std::function<void(std::string)> report = {}, AppSearchOptions search = {});
