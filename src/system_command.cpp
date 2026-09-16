#include "system_command.h"

command::Command systemCommand(std::function<std::string(desktop::SystemAction)> run,
    std::function<bool()> canHibernate) {
    command::Command cmd{"system", "Manage Windows power and your session"};
    cmd.search = true;
    const struct {
        const char* name;
        desktop::SystemAction action;
        bool danger;
        const char* help;
    } verbs[] = {
        {"Lock", desktop::SystemAction::Lock, false, "Lock your Windows session"},
        {"Sleep", desktop::SystemAction::Sleep, false, "Suspend the system and keep your apps open"},
        {"Hibernate", desktop::SystemAction::Hibernate, false, "Save your session to disk and power off"},
        {"Sign Out", desktop::SystemAction::SignOut, true, "End your Windows session; save your work first"},
        {"Restart", desktop::SystemAction::Restart, true, "Restart Windows; save your work first"},
        {"Shutdown", desktop::SystemAction::Shutdown, true, "Shut down Windows and power off; save your work first"},
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
