#pragma once
#include "command.h"
#include "desktop.h"

command::Command systemCommand(std::function<std::string(desktop::SystemAction)> run,
    std::function<bool()> canHibernate);
