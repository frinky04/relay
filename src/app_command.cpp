#include "app_command.h"
#include "fuzzy.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

std::string appSearchId(const desktop::AppEntry& app) {
    return app.appId.empty() ? "path:" + fuzzy::lower(app.parsing) : "id:" + app.appId;
}

command::Command appCommand(std::vector<desktop::AppEntry> apps,
    std::function<std::string(const std::string&, desktop::AppAction)> run,
    std::shared_ptr<Frecency> history, std::function<void(std::string)> report, AppSearchOptions search) {
    std::sort(apps.begin(), apps.end(), [](auto& a, auto& b) {
        const bool installedA = a.parsing.starts_with("shell:AppsFolder\\");
        const bool installedB = b.parsing.starts_with("shell:AppsFolder\\");
        return installedA != installedB ? installedA : a.parsing < b.parsing;
    });
    std::unordered_set<std::string> identities, paths;
    std::unordered_map<std::string, std::string> launches;
    apps.erase(std::remove_if(apps.begin(), apps.end(), [&](const auto& app) {
        if (!paths.insert(app.parsing).second) return true;
        if (!app.appId.empty() && !identities.insert(app.appId).second) return true;
        if (!app.launchKey.empty()) {
            const auto key = fuzzy::lower(app.name) + "\n" + app.launchKey;
            auto [found, inserted] = launches.emplace(key, app.appId);
            if (!inserted && (app.appId.empty() || found->second.empty())) return true;
        }
        return false;
    }), apps.end());
    std::unordered_map<std::string, int> counts;
    for (const auto& app : apps) ++counts[fuzzy::lower(app.name)];
    std::vector<command::Choice> choices;
    std::unordered_map<std::string, std::string> targets;
    std::unordered_map<std::string, desktop::AppEntry> entries;
    std::unordered_map<std::string, std::string> searchIds;
    for (const auto& app : apps) {
        std::string name = app.name;
        std::string subtitle;
        if (counts[fuzzy::lower(name)] > 1) {
            const std::string source = app.parsing.starts_with("shell:AppsFolder\\") ? "Installed app" : "Desktop shortcut";
            subtitle = source;
            name = app.name + " (" + subtitle + ")";
            int suffix = 2;
            // Reserve real app names as well as previously assigned completions.
            while (counts.contains(fuzzy::lower(name)) || targets.contains(fuzzy::lower(name))) {
                subtitle = source + " " + std::to_string(suffix++);
                name = app.name + " (" + subtitle + ")";
            }
        }
        targets.emplace(fuzzy::lower(name), app.parsing);
        entries.emplace(fuzzy::lower(name), app);
        searchIds.emplace(app.parsing, appSearchId(app));
        const auto lower = fuzzy::lower(app.name);
        int penalty = 0;
        for (const auto* suffix : {" help", " tutorial", " uninstall", " (32-bit", " (64-bit"})
            if (lower.find(suffix) != std::string::npos) penalty = 2000;
        choices.push_back({app.name, subtitle, app.parsing, name, penalty});
    }
    std::stable_sort(choices.begin(), choices.end(), [](auto& a, auto& b) { return fuzzy::lower(a.text) < fuzzy::lower(b.text); });
    command::Command cmd{"app", "Launch and manage apps"};
    cmd.search = true;
    if (search.hidden) cmd.choiceVisible = [hidden = search.hidden, ids = std::move(searchIds)](const command::Choice& choice) {
        return !hidden(ids.at(choice.iconKey));
    };
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
    cmd.verbs.push_back({"Open", false, bind(desktop::AppAction::Open), "Use saved launch options"});
    cmd.verbs.push_back({"Open File Location", false, bind(desktop::AppAction::FileLocation), "Select the file in Explorer"});
    cmd.verbs.push_back({"Copy Path", false, bind(desktop::AppAction::CopyPath), "Full path without quotes"});
    cmd.verbs.push_back({"Run as Administrator", false, bind(desktop::AppAction::Admin), "Windows may ask for approval"});
    if (search.setHidden) for (bool hiding : {true, false}) {
        command::Verb hide{hiding ? "Hide from Search" : "Show in Search", false,
            [entries, save = search.setHidden, hiding](const auto& args) {
                const auto& app = entries.at(fuzzy::lower(args.at(0)));
                return save(appSearchId(app), app.name, hiding);
            }, hiding ? "Keep available through /app" : "Restore to app search"};
        if (search.hidden) hide.preview = [entries, hidden = search.hidden, hiding](const auto& args, const auto&) {
            command::Preview result;
            result.hidden = hidden(appSearchId(entries.at(fuzzy::lower(args.at(0))))) == hiding;
            return result;
        };
        cmd.verbs.push_back(std::move(hide));
    }
    return cmd;
}
