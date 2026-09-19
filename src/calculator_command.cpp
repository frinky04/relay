#include "calculator_command.h"
#include "solver.h"
#include <algorithm>
#include <cmath>

namespace {
struct FormatLabel {
    solver::Format format;
    const char *action, *label;
};
constexpr FormatLabel labels[] = {
    {solver::Format::Primary, "Copy", ""},
    {solver::Format::Discord, "Copy Discord", "Discord timestamp"},
    {solver::Format::DiscordRelative, "Copy Discord Relative", "Discord relative"},
    {solver::Format::ISO, "Copy ISO", "ISO 8601 · UTC"},
    {solver::Format::Unix, "Copy Unix", "Unix seconds"},
    {solver::Format::FullDate, "Copy Full Date", "Full date"},
    {solver::Format::ISOWeek, "Copy ISO Week", "ISO week"},
    {solver::Format::Binary, "Copy Binary", "Binary"},
    {solver::Format::Octal, "Copy Octal", "Octal"},
    {solver::Format::Decimal, "Copy Decimal", "Decimal"},
    {solver::Format::Hex, "Copy Hex", "Hex"},
    {solver::Format::RGB, "Copy RGB", "RGB"},
    {solver::Format::HSL, "Copy HSL", "HSL"},
};
command::Preview preview(const std::string &text, const command::Context &context, const FormatLabel &label,
                         const std::function<std::string(const std::string &)> &copy) {
    auto result = solver::evaluate(text, context.now);
    command::Preview out;
    if (auto error = std::get_if<solver::Diagnostic>(&result)) {
        out.error = error->message;
        if (error->span && error->span->begin < error->span->end) {
            auto token = text.substr(error->span->begin, error->span->end - error->span->begin);
            if (token.size() <= 32 && token.find_first_of("\r\n\t") == token.npos)
                out.error = "At '" + token + "': " + out.error;
        }
        return out;
    }
    const auto &solution = std::get<solver::Solution>(result);
    auto available = solver::formats(solution);
    auto selected = std::find(available.begin(), available.end(), label.format);
    if (selected == available.end()) {
        out.hidden = true;
        return out;
    }
    auto value = solver::format(solution, *selected);
    out.title = *selected == solver::Format::Primary ? value : label.label;
    out.subtitle = *selected == solver::Format::Primary ? solution.expression : value;
    if (*selected == solver::Format::Primary) {
        if (std::holds_alternative<solver::Date>(solution.value))
            out.subtitle = solver::format(solution, solver::Format::ShortDate);
        if (const auto *instant = std::get_if<solver::Instant>(&solution.value)) {
            auto delta = instant->relativeDays;
            auto relative = delta == 0    ? "today"
                            : delta == 1  ? "tomorrow"
                            : delta == -1 ? "yesterday"
                                          : value.substr(0, 10);
            out.title = solver::format(solution, solver::Format::Clock) + " " + relative;
            out.subtitle = solver::format(solution, solver::Format::ShortDate) + " · " + instant->destination.zoneLabel;
            if (!instant->sourceLabel.empty()) out.subtitle += " · From " + instant->sourceLabel;
        }
        if (const auto *duration = std::get_if<solver::Duration>(&solution.value);
            duration && !duration->comparedZone.empty())
            out.subtitle = duration->comparedZone + " versus local at this instant";
    }
    out.stacked = true;
    if (label.format == solver::Format::Primary) {
        if (const auto *color = std::get_if<solver::Color>(&solution.value)) {
            auto byte = [](double channel) { return uint32_t(std::round(std::clamp(channel, 0., 1.) * 255)); };
            out.colorSwatch =
                (byte(color->red) << 24) | (byte(color->green) << 16) | (byte(color->blue) << 8) | byte(color->alpha);
        }
    }
    out.action = [copy, value = std::move(value)] { return copy(value); };
    return out;
}
} // namespace

command::Command calculatorCommand(std::function<std::string(const std::string &)> copy) {
    command::Command cmd{"calc", "Calculate numbers, bases, colors, units, dates and times"};
    command::Argument expression{"Expression"};
    expression.rest = true;
    cmd.args.push_back(std::move(expression));
    cmd.recognize = [](const std::string &text,
                       const command::Context &context) -> std::optional<std::vector<std::string>> {
        auto result = solver::evaluate(text, context.now);
        if (auto solution = std::get_if<solver::Solution>(&result); solution && solution->recognize)
            return std::vector<std::string>{text};
        return {};
    };
    cmd.preview = [copy](const auto &args, const auto &context) { return preview(args[0], context, labels[0], copy); };
    for (const auto &label : labels) {
        command::Verb verb;
        verb.name = label.action;
        verb.preview = [copy, label](const auto &args, const auto &context) {
            return preview(args[0], context, label, copy);
        };
        // Every successful preview supplies its immutable, prepared action.
        verb.run = [](const auto &) { return "Calculate the expression again before copying"; };
        cmd.verbs.push_back(std::move(verb));
    }
    return cmd;
}
