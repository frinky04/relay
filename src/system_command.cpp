#include "system_command.h"

command::Command systemCommand(std::function<std::string(desktop::SystemAction)> run,
    std::function<bool()> canHibernate) {
    command::Command cmd{"system", "Windows power and session actions"};
    cmd.search = true;
    const struct {
        const char* name;
        desktop::SystemAction action;
        bool danger;
        const char* help;
    } verbs[] = {
        {"Lock", desktop::SystemAction::Lock, false, "Keep apps running"},
        {"Sleep", desktop::SystemAction::Sleep, false, "Pause without closing apps"},
        {"Hibernate", desktop::SystemAction::Hibernate, false, "Save session to disk; power off"},
        {"Sign Out", desktop::SystemAction::SignOut, true, "Save work first"},
        {"Restart", desktop::SystemAction::Restart, true, "Save work first"},
        {"Shutdown", desktop::SystemAction::Shutdown, true, "Save work first"},
    };
    for (const auto& entry : verbs) {
        command::Verb verb{entry.name, entry.danger, [run, action = entry.action](const auto&) {
            return run(action);
        }, entry.help};
        if (entry.action == desktop::SystemAction::Hibernate) {
            verb.preview = [canHibernate](const auto&, const auto&) {
                command::Preview preview;
                preview.hidden = !canHibernate();
                return preview;
            };
        }
        cmd.verbs.push_back(std::move(verb));
    }
    return cmd;
}
