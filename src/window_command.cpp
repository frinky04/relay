#include "window_command.h"
#include <charconv>
#include <cstdint>
#include <algorithm>
#include <map>
#include <string_view>

namespace {
std::string reference(const desktop::WindowTarget& target) {
    return "[" + std::to_string(reinterpret_cast<uintptr_t>(target.hwnd)) + ":" +
        std::to_string(target.processId) + ":" + std::to_string(target.threadId) + "]";
}

std::optional<desktop::WindowTarget> target(const std::string& value) {
    auto begin = value.rfind('[');
    if (begin == value.npos || value.back() != ']') return std::nullopt;
    const char* at = value.data() + begin + 1;
    const char* end = value.data() + value.size();
    auto number = [&](auto& out, char separator) {
        const auto result = std::from_chars(at, end, out);
        if (result.ec != std::errc{} || result.ptr == end || *result.ptr != separator) return false;
        at = result.ptr + 1;
        return true;
    };
    uintptr_t hwnd = 0;
    DWORD process = 0, thread = 0;
    if (!number(hwnd, ':') || !number(process, ':') || !number(thread, ']') || at != end || !hwnd || !process || !thread)
        return std::nullopt;
    return desktop::WindowTarget{reinterpret_cast<HWND>(hwnd), process, thread};
}
}

command::Command windowCommand(std::function<std::vector<desktop::WindowEntry>()> list,
    std::function<std::string(const desktop::WindowTarget&, desktop::WindowAction)> run) {
    command::Command cmd{"window", "Switch to and manage an open window"};
    cmd.search = true;
    command::Argument arg; arg.name = "Window";
    arg.loadChoices = [list = std::move(list)] {
        const auto windows = list();
        std::map<std::pair<std::string_view, std::string_view>, size_t> counts;
        for (const auto& window : windows) ++counts[{window.title, window.exe}];
        std::vector<command::Choice> choices;
        choices.reserve(windows.size());
        for (const auto& window : windows) {
            if (window.title.empty()) continue;
            // Keep completed input below the launcher's byte limit, without splitting UTF-8.
            size_t length = std::min<size_t>(window.title.size(), 160);
            while (length < window.title.size() && (static_cast<unsigned char>(window.title[length]) & 0xc0) == 0x80) --length;
            auto name = window.title.substr(0, length);
            if (length < window.title.size()) name += "…";
            const auto id = reference(window.target);
            const bool duplicate = counts.at({window.title, window.exe}) > 1;
            auto subtitle = window.exe;
            if (duplicate) subtitle += (subtitle.empty() ? "" : " ") + id;
            choices.push_back({window.title, subtitle,
                window.exePath, name + " " + id});
        }
        return choices;
    };
    cmd.args.push_back(std::move(arg));
    auto bind = [run = std::move(run)](desktop::WindowAction action) {
        return [run, action](const auto& args) {
            const auto window = target(args.at(0));
            if (!window) return std::string("Window unavailable; edit the query to refresh windows");
            return run(*window, action);
        };
    };
    cmd.verbs.push_back({"Switch", false, bind(desktop::WindowAction::Switch), "Restore and focus the window"});
    cmd.verbs.push_back({"Close", true, bind(desktop::WindowAction::Close), "Ask the window to close; the app may prompt to save"});
    cmd.verbs.push_back({"Minimize", false, bind(desktop::WindowAction::Minimize), "Minimize the window"});
    cmd.verbs.push_back({"Maximize", false, bind(desktop::WindowAction::Maximize), "Maximize the window"});
    cmd.verbs.push_back({"Move to Other Monitor", false, bind(desktop::WindowAction::MoveToOtherMonitor), "Move the window to the next connected display"});
    return cmd;
}
