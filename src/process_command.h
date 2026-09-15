#pragma once
#include "command.h"
#include "desktop.h"

command::Command processCommand(std::function<std::vector<desktop::ProcessEntry>()> list,
    std::function<std::string(const desktop::ProcessTarget&)> kill);
