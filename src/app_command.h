#pragma once
#include "command.h"
#include "desktop.h"
#include "frecency.h"

command::Command appCommand(std::vector<desktop::AppEntry> apps,
    std::function<std::string(const std::string&, desktop::AppAction)> run,
    std::shared_ptr<Frecency> history = {}, std::function<void(std::string)> report = {});
