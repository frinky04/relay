#include "process_command.h"
#include <algorithm>
#include <charconv>

namespace {
std::optional<desktop::ProcessTarget> target(const std::string& value) {
    const auto begin = value.rfind('[');
    if (begin == value.npos || value.back() != ']') return std::nullopt;
    const char* end = value.data() + value.size() - 1;
    desktop::ProcessTarget process{};
    const auto pid = std::from_chars(value.data() + begin + 1, end, process.pid);
    if (pid.ec != std::errc{} || pid.ptr == end || *pid.ptr != ':') return std::nullopt;
    const auto created = std::from_chars(pid.ptr + 1, end, process.created);
    if (created.ec != std::errc{} || created.ptr != end || !process.pid || !process.created) return std::nullopt;
    return process;
}
}

command::Command processCommand(std::function<std::vector<desktop::ProcessEntry>()> list,
    std::function<std::string(const desktop::ProcessTarget&)> kill) {
    command::Command cmd{"process", "Kill a running process"};
    command::Argument arg; arg.name = "Process";
    arg.loadChoices = [list = std::move(list)] {
        std::vector<command::Choice> choices;
        for (const auto& process : list()) {
            if (process.name.empty() || !process.target.pid || !process.target.created) continue;
            size_t length = std::min<size_t>(process.name.size(), 160);
            while (length < process.name.size() && (static_cast<unsigned char>(process.name[length]) & 0xc0) == 0x80) --length;
            auto name = process.name.substr(0, length);
            if (length < process.name.size()) name += "…";
            const auto pid = std::to_string(process.target.pid);
            const auto reference = " [" + pid + ":" + std::to_string(process.target.created) + "]";
            choices.push_back({process.name, "PID " + pid + " — " + process.exePath,
                process.exePath, name + reference});
        }
        return choices;
    };
    cmd.args.push_back(std::move(arg));
    cmd.verbs.push_back({"Kill", true, [kill = std::move(kill)](const auto& args) {
        const auto process = target(args.at(0));
        if (!process) return std::string("Process unavailable; edit the query to refresh processes");
        return kill(*process);
    }, "Force the process to exit; unsaved work will be lost"});
    return cmd;
}
