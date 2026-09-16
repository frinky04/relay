#pragma once
#include "command.h"
#include "desktop.h"

command::Command windowCommand(std::function<std::vector<desktop::WindowEntry>()> list,
    std::function<std::string(const desktop::WindowTarget&, desktop::WindowAction)> run);
