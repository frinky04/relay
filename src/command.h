#pragma once
#include "menu_view.h"
#include <optional>
#include <memory>
#include <vector>
#include <ctime>

namespace command {

using Run = std::function<std::string(const std::vector<std::string>&)>;
struct Context { std::time_t now = 0; };
struct Preview {
    std::string title, error, subtitle;
    bool hidden = false;
    std::function<std::string()> action; // optional prepared execution, owned by the evaluation
    bool stacked = false; // explicit detail below the title, with the action in the footer
};
struct Choice {
    std::string text, subtitle, iconKey;
    std::string value; // optional canonical argument/completion; defaults to text
};
struct Argument {
    std::string name;
    // No choices means a text argument. An empty choice list accepts nothing.
    std::optional<std::vector<Choice>> choices;
    std::optional<std::string> defaultValue; // only trailing arguments may have defaults
    bool rest = false; // final text argument consumes the remaining input
    std::function<std::vector<Choice>()> loadChoices; // native choices, sampled once per evaluation
};
struct Verb {
    std::string name;
    bool danger = false;
    Run run;
    std::string help;
    bool updateCheck = false; // native update status stays on this action row
    bool preserveInput = false; // native action keeps the current menu after success
    std::function<Preview(const std::vector<std::string>&, const Context&)> preview;
};
struct Command {
    std::string name, help;
    std::vector<Argument> args;
    std::vector<Verb> verbs; // first is the default
    std::function<std::optional<std::vector<std::string>>(const std::string&, const Context&)> recognize;
    std::function<Preview(const std::vector<std::string>&, const Context&)> preview;
    bool search = false; // expose complete verbs or first-argument choices in bare search
    // Native app history: order empty input directly, boost typed matches.
    std::function<int(const Choice&)> choiceFrecency;
    int recognitionPriority = 0; // higher priorities precede other recognized results; ties retain load order
};
struct View {
    std::string text;
    std::vector<TextSpan> spans;
    std::vector<Slot> slots; // ghosts after the caret: remaining arguments, then the default verb
    std::vector<MenuRow> rows;
};
struct Evaluation {
    Context context;
    std::vector<std::shared_ptr<Command>> snapshots; // own materialized dynamic choices and their verbs
    View view;
    // Own their bound callbacks on the worker, including across app rescans.
    // The UI receives only View, never plugin callbacks.
    std::vector<std::function<std::string()>> actions;
};

std::string quote(std::string_view text);
std::string validate(const Command& command);
Evaluation evaluate(const std::vector<Command>& commands, const std::string& text, Context context = {std::time(nullptr)});

} // namespace command
