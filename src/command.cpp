#include "command.h"
#include "fuzzy.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>

namespace command {
namespace {
bool space(char c) { return std::isspace((unsigned char)c) != 0; }
bool equal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
        return std::tolower(x) == std::tolower(y);
    });
}
struct Token { std::string text; size_t begin, end; bool closed = true; };

Token token(const std::string& text, size_t& at, bool rest = false) {
    while (at < text.size() && space(text[at])) ++at;
    Token out{ {}, at, at };
    bool quoted = false;
    while (at < text.size()) {
        char c = text[at];
        if (!rest && !quoted && space(c)) break;
        ++at;
        if (c == '"') { quoted = !quoted; continue; }
        if (quoted && c == '\\' && at < text.size() && (text[at] == '\\' || text[at] == '"')) c = text[at++];
        out.text += c;
    }
    out.end = at;
    out.closed = !quoted;
    return out;
}

std::string complete(const std::string& text, TextSpan span, const std::string& value) {
    std::string prefix = text.substr(0, span.begin);
    if (span.begin == span.end && !prefix.empty() && !space(prefix.back())) prefix += ' ';
    return prefix + value + (span.end == text.size() ? " " : text.substr(span.end));
}

const std::string& value(const Choice& choice) { return choice.value.empty() ? choice.text : choice.value; }

const Choice* findChoice(const Argument& arg, std::string_view text) {
    if (!arg.choices) return nullptr;
    for (const auto& choice : *arg.choices)
        if (equal(value(choice), text)) return &choice;
    const Choice* found = nullptr;
    for (const auto& choice : *arg.choices) {
        if (!equal(choice.text, text)) continue;
        if (found) return nullptr; // A shared display name must be selected from the list.
        found = &choice;
    }
    return found;
}

bool fillDefaults(const Command& cmd, std::vector<std::string>& args) {
    while (args.size() < cmd.args.size()) {
        const auto& arg = cmd.args[args.size()];
        if (!arg.defaultValue) return false;
        const auto* choice = findChoice(arg, *arg.defaultValue);
        args.push_back(choice ? value(*choice) : *arg.defaultValue);
    }
    return true;
}

// Ghost the arguments still to fill, defaults quoted as they would be typed,
// then the default verb when there is a choice of verbs. Remaining-text
// commands select verbs from the list, so text after them is never a verb.
void ghosts(Evaluation& out, const Command& cmd, size_t from) {
    for (size_t i = from; i < cmd.args.size(); ++i) {
        const auto& arg = cmd.args[i];
        out.view.slots.push_back({Slot::Argument, arg.name, arg.defaultValue ? quote(*arg.defaultValue) : ""});
    }
    const bool rest = !cmd.args.empty() && cmd.args.back().rest;
    if (!rest && cmd.verbs.size() > 1) out.view.slots.push_back({Slot::Verb, cmd.verbs.front().name, {}});
}

void add(Evaluation& out, MenuRow row, const Verb* verb = nullptr, std::vector<std::string> args = {}) {
    if (verb) {
        row.actionLabel = verb->name;
        row.danger = verb->danger;
        row.updateCheck = verb->updateCheck;
        out.actions.push_back([verb, args = std::move(args)] { return verb->run(args); });
    } else out.actions.push_back({});
    out.view.rows.push_back(std::move(row));
}

Preview preview(const Command& cmd, const std::vector<std::string>& args) {
    try { return cmd.preview ? cmd.preview(args) : Preview{}; }
    catch (const std::exception& e) { return {{}, std::string(e.what()) + "; fix the plugin and run /relay Reload Plugins"}; }
}

MenuRow nounRow(const Command& cmd, const std::string& text, TextSpan span) {
    MenuRow row;
    // The gutter draws the slash; the title is the bare noun.
    row.title = cmd.name; row.subtitle = cmd.help; row.kind = "Command";
    row.completion = complete(text, span, "/" + cmd.name);
    return row;
}

void showVerbs(Evaluation& out, const Command& cmd, const std::vector<std::string>& args,
    const std::vector<const Verb*>& matches) {
    if (matches.empty()) return;
    auto result = preview(cmd, args);
    if (!result.error.empty()) {
        MenuRow row; row.title = "/" + cmd.name; row.subtitle = result.error; row.kind = "Error";
        add(out, std::move(row));
        return;
    }
    for (const auto* verb : matches) {
        MenuRow row;
        row.title = result.title.empty() ? verb->name : result.title;
        row.subtitle = verb->help.empty() ? cmd.help : verb->help;
        if (!result.title.empty()) row.subtitle = verb->name + " — " + row.subtitle;
        row.kind = result.title.empty() ? "Verb" : "Result";
        add(out, std::move(row), verb, args);
    }
}

void verbs(Evaluation& out, const Command& cmd, const std::vector<std::string>& args,
    TextSpan tail = {}, bool implicit = false) {
    const auto filter = std::string_view(out.view.text).substr(tail.begin, tail.end - tail.begin);
    std::vector<const Verb*> matches;
    if (implicit) matches.push_back(&cmd.verbs.front());
    else {
        const auto exact = std::find_if(cmd.verbs.begin(), cmd.verbs.end(), [&](auto& v) { return equal(v.name, filter); });
        if (exact != cmd.verbs.end()) {
            out.view.spans.push_back({tail.begin, tail.end, TextSpan::Verb});
            matches.push_back(&*exact);
        } else {
            for (const auto& verb : cmd.verbs)
                if (filter.empty() || fuzzy::score(filter, verb.name)) matches.push_back(&verb);
            if (!filter.empty()) out.view.spans.push_back({tail.begin, tail.end, matches.empty() ? TextSpan::Error : TextSpan::Partial});
        }
    }
    showVerbs(out, cmd, args, matches);
}

void recognized(Evaluation& out, const Command& cmd) {
    try {
        auto args = cmd.recognize(out.view.text);
        if (!args) return;
        if (args->size() > cmd.args.size() || !fillDefaults(cmd, *args))
            throw std::runtime_error("Recognition must supply the command's arguments");
        for (size_t i = 0; i < args->size(); ++i) {
            if (!cmd.args[i].choices) continue;
            const auto* choice = findChoice(cmd.args[i], (*args)[i]);
            if (!choice) throw std::runtime_error("Recognition must use declared argument choices");
            (*args)[i] = value(*choice);
        }
        verbs(out, cmd, *args, {}, true);
    } catch (const std::exception& e) {
        MenuRow row; row.title = "/" + cmd.name; row.kind = "Error";
        row.subtitle = std::string(e.what()) + "; fix the plugin and run /relay Reload Plugins";
        add(out, std::move(row));
    }
}

using Rank = std::pair<bool, int>;

void choices(Evaluation& out, const Command& cmd, const std::vector<std::string>& args, std::string_view needle, TextSpan span,
    std::vector<Rank>* ranks = nullptr) {
    const auto& arg = cmd.args[args.size()];
    if (!arg.choices) return;
    const bool rankChoices = out.view.text.empty() || !needle.empty();
    std::vector<std::pair<Rank, const Choice*>> matches;
    for (const auto& choice : *arg.choices) {
        int score = needle.empty() ? 1 : std::max(fuzzy::score(needle, choice.text), fuzzy::score(needle, choice.subtitle));
        if (!score) continue;
        const bool exact = equal(needle, choice.text) || equal(needle, choice.subtitle);
        if (rankChoices && cmd.choiceFrecency) {
            const int history = std::clamp(cmd.choiceFrecency(choice), 0, 2000);
            // Empty input uses the full score, including infrequently used
            // apps. Typed matches keep the existing boost capped at 200.
            score += out.view.text.empty() ? history : history / 10;
        }
        matches.push_back({{exact, score}, &choice});
    }
    // Global search ranks all commands together; retain declaration order for ties.
    if (!ranks && rankChoices)
        std::stable_sort(matches.begin(), matches.end(), [&](auto& a, auto& b) {
            // Preserve other commands' existing scoped ordering. App history
            // must never lift a partial match above an exact name.
            return cmd.choiceFrecency ? a.first > b.first : a.first.second > b.first.second;
        });
    for (auto [rank, choice] : matches) {
        if (ranks) ranks->push_back(rank);
        auto filled = args;
        filled.push_back(value(*choice));
        MenuRow row;
        row.title = choice->text;
        row.subtitle = choice->subtitle;
        row.kind = cmd.name; row.kind[0] = (char)std::toupper((unsigned char)row.kind[0]);
        row.iconKey = choice->iconKey;
        std::string replacement = quote(value(*choice));
        // Bare search fills the noun as well as the selected choice. Scoped
        // completion edits only the argument, preserving all other input.
        if (out.view.text.empty() || out.view.text.front() != '/') replacement = "/" + cmd.name + " " + replacement;
        row.completion = complete(out.view.text, span, replacement);
        const Verb* verb = fillDefaults(cmd, filled) ? &cmd.verbs.front() : nullptr;
        if (verb) {
            auto result = preview(cmd, filled);
            if (!result.error.empty()) { row.subtitle = result.error; verb = nullptr; }
        }
        add(out, std::move(row), verb, std::move(filled));
    }
}

const Command* resolve(Evaluation& out, const Command& cmd) {
    if (std::none_of(cmd.args.begin(), cmd.args.end(), [](auto& arg) { return bool(arg.loadChoices); })) return &cmd;
    try {
        const auto declarationError = validate(cmd);
        if (!declarationError.empty()) throw std::runtime_error(declarationError);
        auto snapshot = std::make_shared<Command>(cmd);
        for (auto& arg : snapshot->args) if (arg.loadChoices) {
            arg.choices = arg.loadChoices();
            arg.loadChoices = {};
        }
        const auto error = validate(*snapshot);
        if (!error.empty()) throw std::runtime_error(error);
        out.snapshots.push_back(std::move(snapshot));
        return out.snapshots.back().get();
    } catch (const std::exception& e) {
        MenuRow row; row.title = "/" + cmd.name; row.kind = "Error";
        row.subtitle = std::string(e.what()) + "; edit the query to retry";
        add(out, std::move(row));
        return nullptr;
    }
}

Rank rank(std::string_view needle, std::string_view name, std::string_view context) {
    return {equal(needle, name) || equal(needle, context),
        std::max(fuzzy::score(needle, name), fuzzy::score(needle, context))};
}

void search(Evaluation& out, const std::vector<Command>& commands) {
    struct Match {
        MenuRow row;
        std::function<std::string()> action;
        Rank rank;
        bool app;
    };
    std::vector<Match> matches;
    for (const auto& declaration : commands) {
        const auto nounRank = rank(out.view.text, declaration.name, {});
        if (nounRank.second)
            matches.push_back({nounRow(declaration, out.view.text, {0, out.view.text.size()}), {}, nounRank, false});
        if (!declaration.search) continue;
        Evaluation found; found.view.text = out.view.text;
        const auto* cmd = resolve(found, declaration);
        std::vector<Rank> ranks;
        const bool choiceSearch = cmd && !cmd->args.empty() && cmd->args.front().choices.has_value();
        if (cmd) {
            if (choiceSearch) {
                choices(found, *cmd, {}, out.view.text, {0, out.view.text.size()}, &ranks);
                for (auto& row : found.view.rows) {
                    row.subtitle = row.actionLabel + (row.subtitle.empty() ? "" : " — " + row.subtitle);
                    row.context = "/" + cmd->name;
                }
            } else {
                std::vector<std::string> args;
                if (!fillDefaults(*cmd, args)) continue;
                std::vector<const Verb*> verbs;
                for (const auto& verb : cmd->verbs) {
                    auto score = rank(out.view.text, verb.name, cmd->name + " " + verb.name);
                    if (!score.second) continue;
                    ranks.push_back(score);
                    verbs.push_back(&verb);
                }
                showVerbs(found, *cmd, args, verbs);
                // The noun identifies the command; help (including version text) stays visible.
                for (size_t i = 0; i < found.view.rows.size(); ++i)
                    if (found.actions[i]) found.view.rows[i].context = "/" + cmd->name;
            }
        }
        out.snapshots.insert(out.snapshots.end(), found.snapshots.begin(), found.snapshots.end());
        for (size_t i = 0; i < found.view.rows.size(); ++i) {
            const bool error = !found.actions[i];
            if (error && choiceSearch) continue;
            matches.push_back({std::move(found.view.rows[i]), std::move(found.actions[i]),
                error ? Rank{} : ranks[i], declaration.name == "app"});
        }
    }
    std::stable_sort(matches.begin(), matches.end(), [](auto& a, auto& b) {
        if (a.rank != b.rank) return a.rank > b.rank;
        return a.app > b.app;
    });
    for (auto& match : matches) {
        out.view.rows.push_back(std::move(match.row));
        out.actions.push_back(std::move(match.action));
    }
}
} // namespace

std::string quote(std::string_view text) {
    if (!text.empty() && text.find_first_of(" \t\r\n\"") == text.npos) return std::string(text);
    std::string out = "\"";
    for (char c : text) { if (c == '\\' || c == '"') out += '\\'; out += c; }
    return out + '"';
}

std::string validate(const Command& cmd) {
    if (cmd.name.empty() || !std::all_of(cmd.name.begin(), cmd.name.end(), [](char c) { return c >= 'a' && c <= 'z'; }))
        return "Use a lowercase noun containing letters only";
    if (cmd.help.empty() || cmd.verbs.empty()) return "Supply help and at least one verb";
    if (cmd.recognize && std::any_of(cmd.args.begin(), cmd.args.end(), [](auto& arg) { return bool(arg.loadChoices); }))
        return "Do not combine dynamic choices with recognition";
    if (cmd.search) {
        for (size_t i = 0; i < cmd.args.size(); ++i)
            if (!cmd.args[i].defaultValue && !(i == 0 && (cmd.args[i].choices || cmd.args[i].loadChoices)))
                return "Supply argument defaults for search, except for a first choice argument";
    }
    bool sawDefault = false;
    for (const auto& arg : cmd.args) {
        if (arg.name.empty()) return "Name each argument";
        if (arg.rest && (&arg != &cmd.args.back() || arg.choices || arg.loadChoices))
            return "Use rest only on the final text argument";
        if (arg.loadChoices && (arg.choices || arg.defaultValue))
            return "Dynamic choices cannot also declare a fixed list or a default";
        if (sawDefault && !arg.defaultValue) return "Place required arguments before arguments with defaults";
        sawDefault = arg.defaultValue.has_value();
        if (arg.choices) {
            std::unordered_set<std::string> names;
            for (const auto& choice : *arg.choices)
                if (choice.text.empty() || !names.insert(fuzzy::lower(value(choice))).second)
                    return "Give every choice a unique, nonempty name";
            if (arg.defaultValue && !names.contains(fuzzy::lower(*arg.defaultValue)))
                return "Choose a default from the argument's choices";
        }
    }
    std::unordered_set<std::string> names;
    for (const auto& verb : cmd.verbs)
        if (verb.name.empty() || space(verb.name.front()) || space(verb.name.back()) ||
            verb.name.find_first_of("\t\r\n\"") != verb.name.npos || !verb.run || !names.insert(fuzzy::lower(verb.name)).second)
            return "Supply unique verb names and run functions";
    return {};
}

Evaluation evaluate(const std::vector<Command>& commands, const std::string& text) {
    Evaluation out; out.view.text = text;
    if (text.empty() || text[0] != '/') {
        if (!text.empty()) for (const auto& cmd : commands)
            if (cmd.recognize) recognized(out, cmd);
        if (text.empty()) {
            for (const auto& cmd : commands)
                if (cmd.name == "app") choices(out, cmd, {}, text, {0, text.size()});
        } else search(out, commands);
        return out;
    }
    size_t at = 0;
    Token noun = token(text, at);
    const std::string name = noun.text.empty() ? "" : noun.text.substr(1);
    if (at == text.size()) {
        for (const auto& cmd : commands) {
            if (!name.empty() && !fuzzy::score(name, cmd.name)) continue;
            add(out, nounRow(cmd, text, {0, at}));
        }
        return out;
    }
    const auto found = std::find_if(commands.begin(), commands.end(), [&](auto& cmd) { return equal(name, cmd.name); });
    if (found == commands.end() || !noun.closed || noun.text != text.substr(0, at)) return out;
    out.view.spans.push_back({0, at, TextSpan::Noun});
    const auto* resolved = resolve(out, *found);
    if (!resolved) return out;
    const auto& cmd = *resolved;
    std::vector<std::string> args;
    size_t defaultStart = cmd.args.size();
    for (const auto& arg : cmd.args) {
        auto word = token(text, at, arg.rest);
        if (word.begin == word.end) {
            ghosts(out, cmd, args.size());
            if (arg.defaultValue) {
                defaultStart = args.size();
                fillDefaults(cmd, args);
                break;
            }
            choices(out, cmd, args, "", {word.begin, word.end});
            return out;
        }
        if (arg.choices) {
            const auto* choice = findChoice(arg, word.text);
            if (!choice || !word.closed) {
                size_t rest = at; while (rest < text.size() && space(text[rest])) ++rest;
                if (rest != text.size()) { out.view.spans.push_back({word.begin, word.end, TextSpan::Error}); return out; }
                out.view.spans.push_back({word.begin, word.end, TextSpan::Partial});
                ghosts(out, cmd, args.size() + 1);
                choices(out, cmd, args, word.text, {word.begin, word.end});
                return out;
            }
            args.push_back(value(*choice));
        } else {
            if (!word.closed) { out.view.slots.push_back({Slot::Message, "Close the quote", {}}); return out; }
            args.push_back(word.text);
        }
        out.view.spans.push_back({word.begin, word.end, TextSpan::Argument});
    }
    while (at < text.size() && space(text[at])) ++at;
    size_t end = text.size(); while (end > at && space(text[end - 1])) --end;
    if (defaultStart == cmd.args.size() && at == end) ghosts(out, cmd, cmd.args.size());
    verbs(out, cmd, args, {at, end});
    if (defaultStart < cmd.args.size()) {
        args.resize(defaultStart);
        choices(out, cmd, args, "", {text.size(), text.size()});
    }
    return out;
}
} // namespace command
