#include "calculator_command.h"
#include "calculator.h"
#include <algorithm>

namespace {
command::Preview preview(const std::string &text, const command::Context &context, const std::string &action,
                         const std::function<std::string(const std::string &)> &copy) {
    auto result = calculator::evaluate(text, context.now);
    command::Preview out;
    if (result.status != calculator::Status::Success) {
        out.error = result.error.message;
        if (result.error.position) out.error = "At byte " + std::to_string(result.error.position) + ": " + out.error;
        return out;
    }
    auto found = std::find_if(result.outputs.begin(), result.outputs.end(),
                              [&](const auto &row) { return row.action == action; });
    if (found == result.outputs.end()) {
        out.hidden = true;
        return out;
    }
    out.title = found->title;
    out.subtitle = found->subtitle;
    out.stacked = true;
    out.action = [copy, value = found->copy] { return copy(value); };
    return out;
}
} // namespace

command::Command calculatorCommand(std::function<std::string(const std::string &)> copy) {
    command::Command cmd{"calc", "Calculate numbers, units, dates and times"};
    command::Argument expression{"Expression"};
    expression.rest = true;
    cmd.args.push_back(std::move(expression));
    cmd.recognize = [](const std::string &text,
                       const command::Context &context) -> std::optional<std::vector<std::string>> {
        auto result = calculator::evaluate(text, context.now);
        if (result.status == calculator::Status::Success && result.recognize) return std::vector<std::string>{text};
        return {};
    };
    cmd.preview = [copy](const auto &args, const auto &context) { return preview(args[0], context, "Copy", copy); };
    for (const auto *name : {"Copy", "Copy Discord", "Copy Discord Relative", "Copy ISO", "Copy Unix", "Copy Full Date",
                             "Copy ISO Week"}) {
        command::Verb verb;
        verb.name = name;
        verb.preview = [copy, action = verb.name](const auto &args, const auto &context) {
            return preview(args[0], context, action, copy);
        };
        // Every successful preview supplies its immutable, prepared action.
        verb.run = [](const auto &) { return "Calculate the expression again before copying"; };
        cmd.verbs.push_back(std::move(verb));
    }
    return cmd;
}
