#include "app_command.h"
#include "fuzzy.h"
#include <algorithm>
#include <unordered_map>

command::Command appCommand(std::vector<desktop::AppEntry> apps,
    std::function<std::string(const std::string&, desktop::AppAction)> run,
    std::shared_ptr<Frecency> history, std::function<void(std::string)> report) {
    std::sort(apps.begin(), apps.end(), [](auto& a, auto& b) { return a.parsing < b.parsing; });
    apps.erase(std::unique(apps.begin(), apps.end(), [](auto& a, auto& b) { return a.parsing == b.parsing; }), apps.end());
    std::unordered_map<std::string, int> counts;
    for (const auto& app : apps) ++counts[fuzzy::lower(app.name)];
    std::vector<command::Choice> choices;
    std::unordered_map<std::string, std::string> targets;
    for (const auto& app : apps) {
        std::string name = app.name;
        if (counts[fuzzy::lower(name)] > 1) name += " (" + app.parsing + ")";
        // Also handle a real app named like another app's disambiguated name.
        while (targets.contains(fuzzy::lower(name))) name += " (" + app.parsing + ")";
        targets.emplace(fuzzy::lower(name), app.parsing);
        choices.push_back({name, {}, app.parsing});
    }
    std::sort(choices.begin(), choices.end(), [](auto& a, auto& b) { return fuzzy::lower(a.text) < fuzzy::lower(b.text); });
    command::Command cmd{"app", "Open and manage an installed app"};
    cmd.search = true;
    if (history) cmd.choiceFrecency = [history](const command::Choice& choice) { return history->score(choice.iconKey); };
    cmd.args.push_back({"App", std::move(choices)});
    auto bind = [targets = std::make_shared<const decltype(targets)>(std::move(targets)), run = std::move(run),
            history = std::move(history), report = std::move(report)](desktop::AppAction action) {
        return [targets, run, history, report, action](const auto& args) {
            auto found = targets->find(fuzzy::lower(args.at(0)));
            if (found == targets->end()) return std::string("App unavailable; run /relay Rescan Apps");
            auto error = run(found->second, action);
            if (error.empty() && history && (action == desktop::AppAction::Open || action == desktop::AppAction::Admin)) {
                history->bump(found->second);
                auto saved = history->save();
                if (!saved.empty() && report) report(std::move(saved));
            }
            return error;
        };
    };
    cmd.verbs.push_back({"Open", false, bind(desktop::AppAction::Open), "Open the app"});
    cmd.verbs.push_back({"Run as Administrator", false, bind(desktop::AppAction::Admin), "Open the app with administrator permissions"});
    cmd.verbs.push_back({"Open File Location", false, bind(desktop::AppAction::FileLocation), "Select the app's file in Explorer"});
    cmd.verbs.push_back({"Copy Path", false, bind(desktop::AppAction::CopyPath), "Copy the app's file path"});
    return cmd;
}
