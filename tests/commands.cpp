#include "suites.h"
#include "command.h"
#include "app_command.h"
#include "relay_command.h"
#include "config.h"
#include "window_command.h"
#include "process_command.h"
#include "engine.h"
#include "lua_commands.h"
#include <cstdio>
#include <fstream>
#include <future>
#include <stdexcept>
#include <algorithm>
#include <sol/sol.hpp>

using namespace std::chrono_literals;
static int failures = 0;
static void check(bool ok, const char* description) {
    if (!ok) { ++failures; printf("FAIL: %s\n", description); }
}

// Ghost slots rendered as the input would show them, one trailing space each.
static std::string hint(const command::View& view) {
    std::string out;
    for (const auto& slot : view.slots) {
        if (slot.kind == Slot::Argument) out += "<" + slot.label + (slot.value.empty() ? "" : ": " + slot.value) + "> ";
        else out += slot.label + " ";
    }
    return out;
}

// Event-driven waits with a bounded failure deadline; no desktop effects.
struct Signal {
    std::mutex mutex;
    std::condition_variable cv;
    unsigned count = 0;
    void notify() { std::lock_guard lock(mutex); ++count; cv.notify_one(); }
    void wait() {
        std::unique_lock lock(mutex);
        if (!cv.wait_for(lock, 5s, [&] { return count != 0; })) throw std::runtime_error("Worker timed out");
        --count;
    }
};

int runCommandTests() {
    {
        std::string selected;
        command::Command cmd{"sample", "Test choice ranking"};
        cmd.search = true;
        cmd.args.push_back({"Value", std::vector<command::Choice>{
            {"Editor extra", {}, {}, "prefix"},
            {"Rejected", "Editor", {}, "reject"},
            {"First", "Editor", {}, "first"},
            {"Second", "Editor", {}, "second"}}});
        cmd.verbs.push_back({"Open", false, [&](const auto& args) { selected = args[0]; return std::string{}; }});
        cmd.preview = [](const auto& args, const auto&) -> command::Preview {
            return args[0] == "reject" ? command::Preview{{}, "Unavailable; retry"} : command::Preview{};
        };
        std::vector<command::Command> catalog{std::move(cmd)};
        auto result = command::evaluate(catalog, "Editor");
        check(result.view.rows.size() == 3 && result.view.rows[0].title == "First" &&
            result.view.rows[1].title == "Second" && result.view.rows[2].title == "Editor extra",
            "global choice ranks preserve exact subtitle matches and declaration ties while omitting rejected previews");
        check(result.actions.size() == 3 && result.actions[1]().empty() && selected == "second",
            "carried choice ranks keep actions aligned after preview rejection");
        auto scoped = command::evaluate(catalog, "/sample Editor");
        check(scoped.view.rows.size() == 4 && scoped.view.rows[0].title == "Rejected" &&
            scoped.view.rows[1].title == "First" && scoped.view.rows[3].title == "Editor extra",
            "scoped choices retain score ordering and rejected preview rows");
    }
    std::string launched, copied, opened, openError;
    auto openUrl = [&](const std::string& url) { opened = url; return openError; };
    auto apps = appCommand({{"Firefox", "firefox"}, {"Google Chrome", "chrome"}, {"Editor", "editor1"}, {"Editor", "editor2"}},
        [&](const std::string& target, desktop::AppAction) { launched = target; return target == "chrome" ? "Install Chrome and try again" : ""; });
    auto copy = [&](const std::string& value) { copied = value; return value == "fail" ? "Clipboard busy; try again" : ""; };
    {
        const desktop::ProcessTarget first{101, 1000}, second{102, 2000}, third{103, 3000};
        std::vector<desktop::ProcessEntry> processes{
            {first, "Editor.exe", "C:\\Apps\\Editor.exe"},
            {second, "Editor.exe", "D:\\Tools\\Editor.exe"},
            {third, "Paint.exe", "C:\\Apps\\Paint.exe"}};
        desktop::ProcessTarget killed{};
        int enumerations = 0, kills = 0;
        bool fail = false;
        std::string killError;
        std::vector<command::Command> catalog{processCommand([&] {
            ++enumerations;
            if (fail) throw std::runtime_error("Cannot list processes; try again");
            return processes;
        }, [&](const auto& target) -> std::string {
            if (std::none_of(processes.begin(), processes.end(), [&](auto& p) { return p.target == target; }))
                return "Process unavailable; edit the query to refresh processes";
            if (!killError.empty()) return killError;
            ++kills;
            killed = target;
            return {};
        })};
        check(command::validate(catalog[0]).empty(), "process uses the existing dynamic argument and danger contract");
        command::evaluate(catalog, ""); command::evaluate(catalog, "/"); command::evaluate(catalog, "/process");
        auto noun = command::evaluate(catalog, "process");
        check(noun.view.rows.size() == 1 && noun.view.rows[0].completion == "/process " && !noun.actions[0],
            "bare process discovers a completion-only command");
        check(command::evaluate(catalog, "Editor").view.rows.empty() && enumerations == 0,
            "process choices never enumerate or appear in bare search");
        auto rows = command::evaluate(catalog, "/process ");
        check(enumerations == 1 && kills == 0 && rows.view.rows.size() == 3,
            "entering process samples choices once without side effects");
        check(rows.view.rows[0].danger && rows.view.rows[0].actionLabel == "Kill" &&
            rows.view.rows[0].subtitle == "PID 101 — C:\\Apps\\Editor.exe" &&
            rows.view.rows[0].iconKey == "C:\\Apps\\Editor.exe" &&
            rows.view.rows[0].completion != rows.view.rows[1].completion,
            "process rows show PID, path, icon and destructive metadata with unique references");
        auto byPid = command::evaluate(catalog, "/process 102");
        check(byPid.view.rows.size() == 1 && byPid.actions[0]().empty() && killed == second,
            "PID search targets a single process");
        check(command::evaluate(catalog, "/process Tools").view.rows.size() == 1,
            "process search includes the executable path");
        check(command::evaluate(catalog, "/process Editor.exe Kill").view.rows.empty(),
            "duplicate process names cannot pick an arbitrary instance");
        auto explicitProcess = command::evaluate(catalog, "/process Paint.exe Kill");
        check(explicitProcess.view.rows.size() == 1 && explicitProcess.view.rows[0].danger &&
            explicitProcess.actions[0]().empty() && killed == third,
            "a unique process name accepts the explicit Kill verb");
        std::reverse(processes.begin(), processes.end());
        for (size_t i = 0; i < rows.view.rows.size(); ++i) {
            auto completed = command::evaluate(catalog, rows.view.rows[i].completion);
            check(completed.view.rows.size() == 1 && completed.view.rows[0].danger &&
                completed.view.rows[0].completion.empty() && completed.actions[0]().empty(),
                "process completion survives reorder and resolves to an action-only dangerous verb");
            auto selected = killed;
            check(rows.actions[i]().empty() && killed == selected,
                "displayed process actions remain bound to their original target");
        }
        killError = "Cannot kill this process; check permissions in Task Manager";
        check(rows.actions[0]() == killError, "process permission errors propagate unchanged");
        killError.clear();
        processes = {{{first.pid, first.created + 1}, "Editor.exe", "C:\\Apps\\Editor.exe"}};
        const int before = kills;
        check(!rows.actions[0]().empty() && kills == before,
            "a recycled PID with the same name cannot receive an old action");
        auto stale = command::evaluate(catalog, rows.view.rows[0].completion);
        check(std::none_of(stale.actions.begin(), stale.actions.end(), [](auto& action) { return bool(action); }),
            "a stale completed process reference cannot bind the replacement");
        processes.clear();
        check(!rows.actions[1]().empty() && command::evaluate(catalog, "/process ").view.rows.empty(),
            "exited processes fail old actions and disappear on refresh");
        processes = {{{104, 0}, "Unknown.exe", ""}};
        check(command::evaluate(catalog, "/process ").view.rows.empty(),
            "unverifiable process identities are not offered");
        processes = {{first, std::string(159, 'x') + "日本\".exe", "C:\\long.exe"}};
        auto longName = command::evaluate(catalog, "/process ");
        check(longName.view.rows[0].completion.size() < 512 &&
            command::evaluate(catalog, longName.view.rows[0].completion).actions[0]().empty(),
            "long UTF-8 process names produce bounded round-trip completions");
        fail = true;
        auto error = command::evaluate(catalog, "/process ");
        check(error.view.rows.size() == 1 && error.view.rows[0].kind == "Error" && !error.actions[0],
            "process enumeration errors offer recovery without an action");
    }
    {
        const desktop::WindowTarget first{reinterpret_cast<HWND>(101), 10, 11};
        const desktop::WindowTarget second{reinterpret_cast<HWND>(102), 10, 11};
        const desktop::WindowTarget third{reinterpret_cast<HWND>(103), 20, 21};
        std::vector<desktop::WindowEntry> windows{
            {first, "Notes", "Code", "code.exe"}, {second, "Notes", "Code", "code.exe"},
            {third, "Canvas", "Paint", "paint.exe"}};
        desktop::WindowTarget switched{};
        desktop::WindowAction windowAction{};
        int enumerations = 0, switches = 0;
        bool fail = false;
        std::vector<command::Command> catalog{windowCommand([&] {
            ++enumerations;
            if (fail) throw std::runtime_error("Enumeration failed");
            return windows;
        }, [&](const auto& target, desktop::WindowAction action) -> std::string {
            windowAction = action;
            ++switches;
            if (std::none_of(windows.begin(), windows.end(), [&](auto& window) { return window.target == target; }))
                return "Window unavailable; edit the query to refresh windows";
            switched = target;
            return {};
        })};
        check(command::validate(catalog[0]).empty(), "native dynamic window declaration is valid");
        command::evaluate(catalog, "/"); command::evaluate(catalog, "/wind"); command::evaluate(catalog, "");
        check(enumerations == 0, "command discovery and empty input do not enumerate windows");
        auto rows = command::evaluate(catalog, "/window ");
        check(enumerations == 1 && switches == 0 && rows.view.rows.size() == 3 && hint(rows.view) == "<Window> Switch ", "window choices refresh on entering the command without switching");
        check(rows.view.rows[0].title == "Notes" && rows.view.rows[0].subtitle != rows.view.rows[1].subtitle &&
            rows.view.rows[0].completion != rows.view.rows[1].completion, "duplicate windows have distinct descriptions and completion references");
        check(rows.view.rows[2].subtitle == "Paint" && rows.view.rows[2].iconKey == "paint.exe", "window rows expose app names and existing icon keys");
        auto appSearch = command::evaluate(catalog, "/window code");
        check(appSearch.view.rows.size() == 2 && appSearch.actions[1]().empty() && switched == second, "window search includes app names and binds the selected window");
        const int beforeSearch = enumerations;
        auto global = command::evaluate(catalog, "Notes");
        check(enumerations == beforeSearch + 1 && global.view.rows.size() == 2 &&
            global.view.rows[0].subtitle.find("Switch — Code") == 0, "bare search samples windows once and labels the action");
        std::reverse(windows.begin(), windows.end());
        auto refreshed = command::evaluate(catalog, "Notes");
        check(global.actions[0]().empty() && switched == first && refreshed.actions[0]().empty() && switched == second,
            "old and new global window results keep their own target identity");
        check(command::evaluate(catalog, global.view.rows[0].completion).actions[0]().empty() && switched == first,
            "global window completion resolves the same target after reordering");
        for (size_t i = 0; i < rows.view.rows.size(); ++i) {
            auto completed = command::evaluate(catalog, rows.view.rows[i].completion);
            check(completed.view.rows.size() == 5 && completed.view.rows[0].actionLabel == "Switch" &&
                completed.view.rows[0].completion.empty(), "Tab completion offers window verbs with Switch first");
            check(completed.actions[0]().empty(), "completed window reference executes");
            const auto selected = switched;
            check(rows.actions[i]().empty() && switched == selected, "fresh enumeration and old snapshots preserve exact target identity across reorder");
        }
        auto explicitWindow = command::evaluate(catalog, "/window Canvas Switch");
        check(explicitWindow.view.spans.size() == 3 && explicitWindow.actions[0]().empty() && switched == third, "unique window titles can be typed with an explicit verb");
        for (const auto& [verb, action] : std::vector<std::pair<std::string, desktop::WindowAction>>{
                {"Close", desktop::WindowAction::Close}, {"Minimize", desktop::WindowAction::Minimize},
                {"Maximize", desktop::WindowAction::Maximize}, {"Move to Other Monitor", desktop::WindowAction::MoveToOtherMonitor}}) {
            auto selected = command::evaluate(catalog, "/window Canvas " + verb);
            check(selected.view.rows.size() == 1 && selected.actions[0]().empty() && switched == third && windowAction == action,
                "each window verb binds the selected identity and operation");
            check(selected.view.rows[0].danger == (action == desktop::WindowAction::Close), "only Close requires confirmation");
            check(!desktop::runWindow({}, action).empty(), "native window actions reject missing targets before operating");
        }
        check(command::evaluate(catalog, "/window Notes").view.rows.size() == 2 &&
            command::evaluate(catalog, "/window Notes Switch").view.rows.empty(), "ambiguous titles require selecting a window before typing a verb");
        windows = {{desktop::WindowTarget{first.hwnd, 99, 98}, "Replacement", "Code", "code.exe"}};
        check(!rows.actions[0]().empty(), "a closed window or reused handle cannot silently activate a different process");
        windows.clear();
        check(command::evaluate(catalog, "/window ").view.rows.empty(), "closed windows disappear on the next query");
        fail = true;
        auto error = command::evaluate(catalog, "/window ");
        check(error.view.rows.size() == 1 && error.view.rows[0].kind == "Error" && !error.actions[0], "enumeration failures become non-executable recovery rows");
        catalog.push_back(apps);
        auto failedSearch = command::evaluate(catalog, "Firefox");
        check(failedSearch.view.rows.size() == 2 && failedSearch.view.rows[0].title == "Firefox" &&
            failedSearch.actions[0]().empty() && launched == "firefox" && failedSearch.view.rows[1].kind == "Error",
            "window enumeration failure leaves matching apps usable and reports recovery last");
        catalog.pop_back();
        fail = false;
        windows = {{first, std::string(500, 'x') + "日本", "Code", "code.exe"}};
        auto longTitle = command::evaluate(catalog, "/window ");
        check(longTitle.view.rows[0].completion.size() < 512 && command::evaluate(catalog, longTitle.view.rows[0].completion).actions[0]().empty(), "long titles still produce usable unambiguous completions");
        windows = {{first, "Say \"hello\" \\ notes", "Code", "code.exe"}};
        auto quoted = command::evaluate(catalog, "/window ");
        check(command::evaluate(catalog, quoted.view.rows[0].completion).actions[0]().empty() && switched == first, "window completion preserves quotes and backslashes");
        auto invalid = catalog[0];
        invalid.args[0].defaultValue = "Notes";
        check(!command::validate(invalid).empty(), "dynamic choices cannot declare stale defaults");
    }
    const auto source = std::filesystem::path(RELAY_TEST_SOURCE_DIR);
    {
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::io, sol::lib::debug);
        const auto path = source / "tests/math_parser.lua";
        lua.create_named_table("arg", 0, path.generic_string());
        auto result = lua.safe_script_file(path.string(), sol::script_pass_on_error);
        if (!result.valid()) { sol::error error = result; printf("%s\n", error.what()); }
        check(result.valid(), "full math parser suite passes, including generated expressions and malformed-input probes");
    }
    auto echo = loadLuaCommand(source / "tests/fixtures/echo.lua", copy, openUrl);
    auto web = loadLuaCommand(source / "plugins/web.lua", copy, openUrl);
    auto calc = loadLuaCommand(source / "plugins/calc.lua", copy, openUrl);
    {
        std::vector<command::Command> bundled;
        std::vector<std::string> errors;
        loadLuaCommands(bundled, source / "plugins", copy, openUrl, [&](auto error) { errors.push_back(error); });
        check(errors.empty() && bundled.size() == 3 && bundled[0].name == "calc" && bundled[1].name == "datetime" && bundled[2].name == "web",
            "bundled content contains calculator, datetime and web commands");
        check(command::evaluate(bundled, "/text hello").view.rows.empty() &&
            command::evaluate(bundled, "/echo hello").view.rows.empty(), "removed text command and test fixture are absent from bundled content");
    }
    std::vector<command::Command> commands{apps, echo, web, calc};
    check(command::validate(apps).empty(), "duplicate app names become unique choices");
    auto query = [&](const std::string& value) { return command::evaluate(commands, value); };
    auto home = query("");
    check(home.view.rows.size() == 4 && home.view.spans.empty() && home.view.slots.empty(), "empty input shows only apps");
    check(home.view.rows[0].title == "Editor (editor1)" && home.view.rows[1].title == "Editor (editor2)" &&
        home.view.rows[2].title == "Firefox" && home.view.rows[3].title == "Google Chrome", "home apps have stable alphabetical order");
    check(home.actions[0]().empty() && launched == "editor1", "Enter on the initial app row opens that app");
    for (const auto& row : home.view.rows) {
        check(row.kind == "App" && query(row.completion).actions[0]().empty() == (row.iconKey != "chrome") && launched == row.iconKey,
            "home app completion preserves the chosen launch target");
    }
    auto nouns = query("/");
    check(nouns.view.rows.size() == 4 && nouns.view.rows[0].title == "app" && nouns.view.rows[1].title == "echo" &&
        nouns.view.rows[2].title == "web" && nouns.view.rows[3].title == "calc" &&
        !nouns.actions[0] && !nouns.actions[1] && !nouns.actions[2] && !nouns.actions[3], "slash shows the command catalog without app rows");
    check(query("/we").view.rows.size() == 1 && query("/we").view.rows[0].completion == "/web ", "slash prefix filters commands");
    for (const auto* text : {"web", "WeB", "wb"}) {
        auto entrance = query(text);
        check(entrance.view.rows.size() == 1 && entrance.view.rows[0].title == "web" &&
            entrance.view.rows[0].kind == "Command" && entrance.view.rows[0].subtitle == web.help &&
            entrance.view.rows[0].completion == "/web " && entrance.view.rows[0].actionLabel.empty() &&
            !entrance.actions[0] && entrance.view.spans.empty(),
            "bare command names match case-insensitively and fuzzily, offering completion only");
        auto entered = query(entrance.view.rows[0].completion);
        check(entered.view.rows.empty() && hint(entered.view) == "<Query> ", "command entrance reaches the explicit argument prompt");
    }
    check(query("web cats").view.rows.empty(), "bare text does not implicitly parse command arguments");
    {
        auto plain = web;
        plain.preview = [](auto&, const auto&) -> command::Preview { throw std::runtime_error("Must not preview an entrance"); };
        std::vector<command::Command> catalog{plain,
            appCommand({{"Web", "web-app"}, {"Web Browser", "browser-app"}},
                [&](auto& target, desktop::AppAction) { launched = target; return std::string(); })};
        auto result = command::evaluate(catalog, "web");
        check(result.view.rows.size() == 3 && result.view.rows[0].kind == "App" &&
            result.view.rows[1].title == "web" && !result.actions[1] &&
            result.actions[0]().empty() && launched == "web-app" &&
            result.actions[2]().empty() && launched == "browser-app",
            "exact apps win noun ties, exact nouns beat fuzzy apps, and sorted actions stay aligned");
        auto scoped = command::evaluate(catalog, "/we");
        check(scoped.view.rows.size() == 1 && scoped.view.rows[0].completion == result.view.rows[1].completion &&
            !scoped.actions[0], "slash restricts discovery to commands using the same completion");
    }
    std::vector<command::Command> noApps{appCommand({}, [](auto&, desktop::AppAction) { return std::string(); }), echo};
    check(command::evaluate(noApps, "").view.rows.empty() && command::evaluate(noApps, "/").view.rows.size() == 2,
        "an empty app catalog does not fall back to commands");
    auto catalog = query("/app");
    check(catalog.view.rows.size() == 1 && catalog.view.rows[0].completion == "/app " && !catalog.actions[0], "noun completes before entering it");
    auto all = query("/app ");
    check(all.view.rows.size() == 4 && hint(all.view) == "<App> Open ", "app noun lists every installed app");
    auto bare = query("fire");
    check(bare.view.rows.size() == 1 && bare.view.rows[0].actionLabel == "Open", "bare search offers default app action");
    check(bare.actions[0]().empty() && launched == "firefox", "bare action launches the selected target");
    auto partial = query("/app chr");
    check(partial.view.rows.size() == 1 && partial.view.rows[0].completion == "/app \"Google Chrome\" ", "Tab quotes app names with spaces");
    auto filled = query(partial.view.rows[0].completion);
    check(filled.view.rows[0].title == "Open" && filled.view.spans.size() == 2, "completed app resolves and highlights");
    check(filled.view.rows[0].completion.empty(), "app action rows offer execution only");
    check(filled.actions[0]() == "Install Chrome and try again" && launched == "chrome", "launch failure propagates");
    for (const auto& row : all.view.rows) {
        auto resolved = query(row.completion);
        check(resolved.actions.size() == 4 && (bool)resolved.actions[0], "every app completion resolves unambiguously");
        resolved.actions[0]();
        check(launched == row.iconKey, "completion preserves exact app identity");
    }
    {
        auto typed = query("/APP firefox oPeN ");
        check(typed.view.spans.size() == 3 && typed.view.spans[0].kind == TextSpan::Noun &&
            typed.view.spans[1].kind == TextSpan::Argument && typed.view.spans[2].kind == TextSpan::Verb,
            "noun, args and verb match without rewriting case");
        check(partial.view.spans.size() == 2 && partial.view.spans[1].kind == TextSpan::Partial && hint(partial.view) == "Open ",
            "a choice being typed is marked as the slot being filled");
        auto unknown = query("/app Firefox Delete");
        check(unknown.actions.empty() && unknown.view.spans.size() == 3 && unknown.view.spans[2].kind == TextSpan::Error,
            "unknown verb cannot run the default and is marked as an error");
        auto stray = query("/app nope Open");
        check(stray.actions.empty() && stray.view.spans.size() == 2 && stray.view.spans[1].kind == TextSpan::Error,
            "stray text after an unresolved choice cannot execute and marks the bad choice");
        auto filter = query("/app Firefox op");
        check(filter.actions.size() == 3 && filter.view.spans[2].kind == TextSpan::Partial, "a partial verb filters and is marked as being filled");
    }
    check(query("/echo ").actions.empty(), "missing argument cannot run");
    check(query("/echo \"hello").actions.empty(), "unclosed text quote cannot run");
    check(query("/echo hello garbage").actions.empty(), "extra text cannot run");
    check(query("/missing ").actions.empty(), "unknown noun cannot run");
    for (const std::string value : {"", "hello world", "C:\\Program Files\\", "say \"hello\"", "Copy", "a\\b"}) {
        auto parsed = query("/echo " + command::quote(value) + " Copy ");
        check(parsed.actions.size() == 1 && parsed.actions[0]().empty() && copied == value, "quoted arguments round-trip through Lua");
    }
    auto failed = query("/echo fail");
    check(failed.actions[0]() == "Clipboard busy; try again", "Lua receives host errors");

    auto terms = query("/web ");
    check(terms.actions.empty() && hint(terms.view) == "<Query> ", "web prompts only for required input");
    auto search = query("/web lua documentation");
    check(search.actions.size() == 2 && search.view.rows[0].actionLabel == "Search Google" &&
        search.view.rows[1].actionLabel == "Search DuckDuckGo" && hint(search.view).empty(),
        "web offers engine verbs for an unquoted multiword query");
    check(search.actions[0]().empty() && opened == "https://www.google.com/search?q=lua%20documentation",
        "the default web verb searches Google with the complete query");
    check(search.actions[1]().empty() && opened == "https://duckduckgo.com/?q=lua%20documentation",
        "the alternate web verb searches DuckDuckGo with the same query");
    check(search.view.rows[0].completion.empty() && search.view.rows[1].completion.empty(),
        "web verbs leave the editable query unchanged");
    check(query("/web \"lua documentation\"").actions[0]().empty() &&
        opened == "https://www.google.com/search?q=lua%20documentation", "quoted queries still work");
    check(query("/WeB  Lua   docs  ").actions[0]().empty() &&
        opened == "https://www.google.com/search?q=Lua%20%20%20docs%20%20",
        "web preserves query casing and internal and trailing whitespace");
    auto quoted = query("/APP  \"Google Ch");
    check(quoted.view.rows.size() == 1 && quoted.view.rows[0].completion == "/APP  \"Google Chrome\" ",
        "completion closes the edited quote without rewriting the noun");
    check(query("/web google").actions[0]().empty() && opened == "https://www.google.com/search?q=google",
        "engine names remain query text");
    check(query("/web hello Search DuckDuckGo").actions[0]().empty() &&
        opened == "https://www.google.com/search?q=hello%20Search%20DuckDuckGo",
        "written verb names belong to the query; engines are selected from rows");
    const auto complex = std::string("C++ & #/%?=\" café 日本");
    auto duck = query("/web " + command::quote(complex));
    check(duck.actions[1]().empty() && opened == "https://duckduckgo.com/?q=C%2B%2B%20%26%20%23%2F%25%3F%3D%22%20caf%C3%A9%20%E6%97%A5%E6%9C%AC",
        "web encodes punctuation and UTF-8 as one query parameter");
    for (const auto value : {"", "   "}) {
        opened.clear();
        check(!query("/web " + command::quote(value)).actions[0]().empty() && opened.empty(), "blank searches report recovery without opening a browser");
    }
    check(query("/web \"unfinished").actions.empty(), "web cannot run an unfinished quote");
    openError = "Set a default browser and try again";
    check(search.actions[0]() == openError, "URL opening failures propagate through Lua");
    openError.clear();

    for (const auto& [expression, expected] : std::vector<std::pair<std::string, std::string>>{
        {"5 + 5", "10"}, {"2+3*4", "14"}, {"(2+3)*4", "20"}, {"8/4/2", "1"},
        {"10-3-2", "5"}, {"-2 * -(3+1)", "8"}, {".5 + 1.25", "1.75"},
        {"0.1+0.2", "0.3"}, {"1/3", "0.333333333333333"}, {"0*-1", "0"},
        {"9223372036854775807 * 2", "1.84467440737096e+19"},
        {"2^3^2", "512"}, {"-2^2", "-4"}, {"2^-3", "0.125"},
        {"sqrt(144)", "12"}, {"SIN(pi/2)", "1"}, {"round(12.345, 2)", "12.35"},
        {"2pi", "6.28318530717959"}, {"2(3)", "6"}, {"(2)(3)", "6"},
        {"5!", "120"}, {"15% of 240", "36"}, {"240 - 15%", "204"},
        {"1.5e3 / 4", "375"}, {"2^53", "9007199254740992"}, {"log(8, 2)", "3"}}) {
        copied = "untouched";
        auto result = query(expression);
        check(result.view.rows.size() == 1 && result.view.rows[0].title == expected && copied == "untouched",
            "bare arithmetic previews the result without copying");
        if (result.view.rows.empty()) continue;
        check(result.view.text == expression && result.view.spans.empty() && result.view.rows[0].completion.empty() &&
            result.view.rows[0].kind == "Result" && result.view.rows[0].actionLabel == "Copy", "math preserves input and offers Enter Copy only");
        check(result.actions[0] && result.actions[0]().empty() && copied == expected, "Enter copies the displayed calculator result");
        auto explicitResult = query("/calc " + expression);
        check(explicitResult.view.rows.size() == 1 && explicitResult.view.rows[0].title == expected &&
            explicitResult.actions[0]().empty() && copied == expected, "explicit calculator uses the same preview and execution");
    }
    for (const auto expression : {"5", "-5", "(5)", "pi", "e", "1e3", "(-pi)", "5 +", "1/0", "hello + world",
            "1..2+3", "math.sqrt(4)", "host.copy('oops')", "2+3 garbage"}) {
        const auto result = query(expression);
        check(std::none_of(result.view.rows.begin(), result.view.rows.end(), [](auto& row) { return row.kind == "Result"; }),
            "recognition leaves literals, incomplete math and unsupported syntax to ordinary search");
    }
    for (const auto expression : {"", "5 +", "1/0", "(2+3", "sqrt(-1)", "171!", "min(1,)", "2e", "1,234"}) {
        auto result = query("/calc " + command::quote(expression));
        check(result.view.rows.size() == 1 && result.view.rows[0].kind == "Error" && !result.actions[0] &&
            !result.view.rows[0].subtitle.empty(), "explicit invalid math shows a recovery error without execution");
    }
    check(query("/calc 5").view.rows[0].title == "5", "explicit calculator accepts a plain number");
    check(query("/calc pi").view.rows[0].title == "3.14159265358979" &&
        query("/calc 1e3").view.rows[0].title == "1000", "explicit calculator accepts constants and scientific literals");
    auto domain = query("/calc sqrt(-1)");
    check(domain.view.rows[0].subtitle == "At byte 1: Use a nonnegative number for sqrt" && !domain.actions[0],
        "explicit errors preserve the parser's recovery message and expression byte position");
    check(hint(query("/calc \"5 +").view) == "Close the quote ", "explicit math keeps normal quote rules");
    for (const auto input : {"/calc 32 + 32", "/calc   32  +  32  ", "/calc \"32 + 32\""}) {
        auto result = query(input);
        check(result.view.rows.size() == 1 && result.view.rows[0].title == "64" && result.view.rows[0].completion.empty() &&
            result.view.text == input && result.view.spans.size() == 2 && result.view.spans[1].end == std::string(input).size() &&
            result.actions[0]().empty() && copied == "64", "calculator consumes the full expression without changing input or Tab behavior");
    }
    check(hint(query("/calc ").view) == "<Expression> " && query("/calc   ").view.rows.empty(), "calculator still requires an expression");
    for (const auto input : {"/calc 32 + 32 Copy", "/calc \"32 + 32\" Copy"}) {
        auto result = query(input);
        check(result.view.rows.size() == 1 && result.view.rows[0].kind == "Error" && !result.actions[0], "a typed verb stays part of the calculator expression");
    }
    check(query("/web \"5 + 5\"").view.rows[0].actionLabel == "Search Google", "slash commands never invoke bare recognition");
    check(query(std::string(70, '(') + "1+2" + std::string(70, ')')).view.rows.empty(), "calculator bounds parser recursion");
    check(query(std::string(310, '9') + "+1").view.rows.empty(), "calculator rejects nonfinite values");
    {
        std::vector<command::Command> overlap{appCommand({{"5 + 5", "math-app"}}, [&](auto& target, desktop::AppAction) { launched = target; return std::string(); }), calc};
        auto result = command::evaluate(overlap, "5 + 5");
        check(result.view.rows.size() == 2 && result.view.rows[0].title == "10" && result.view.rows[1].kind == "App" &&
            result.actions[1]().empty() && launched == "math-app", "recognized results precede apps without removing app matches");
    }

    auto temp = std::filesystem::temp_directory_path() / ("relay-commands-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(temp);
    {
        const auto path = temp / "search.lua";
        { std::ofstream f(path); f << R"(return {name='sample', help='Test search', search=true,
            args={{name='Value', default='Ready'}},
            preview=function(args) return args[1] end,
            verbs={{name='Quit', danger=true, run=function(args) return host.copy(args[1]) end},
                   {name='Quick Copy', run=function(args) return host.copy('Quick') end}}})"; }
        auto plugin = loadLuaCommand(path, copy, openUrl);
        int previews = 0, loads = 0;
        command::Command windows{"windows", "Test dynamic choices"};
        windows.search = true;
        command::Argument arg; arg.name = "Window";
        arg.loadChoices = [&] { ++loads; return std::vector<command::Choice>{{"Quit", "Editor", {}, "target"}}; };
        windows.args.push_back(arg);
        windows.verbs.push_back({"Switch", false, [&](auto& args) { copied = args[0]; return std::string(); }});
        auto other = windows; other.name = "other";
        other.args[0].loadChoices = [&] { ++loads; return std::vector<command::Choice>{{"Quit", "Other", {}, "second"}}; };
        auto hidden = windows; hidden.name = "hidden"; hidden.search = false;
        hidden.args[0].loadChoices = []() -> std::vector<command::Choice> { throw std::runtime_error("Must not load"); };
        std::vector<command::Command> catalog{plugin, windows, other, hidden,
            appCommand({{"Quit", "quit-app"}, {"Quitter", "quitter-app"}, {"5 + 5", "math-app"}},
                [&](auto& target, desktop::AppAction) { launched = target; return std::string(); }), calc};
        auto originalPreview = catalog[0].preview;
        catalog[0].preview = [&](auto& args, const auto& context) { ++previews; return originalPreview(args, context); };
        copied.clear(); launched.clear();
        check(command::evaluate(catalog, "").view.rows.size() == 3 && loads == 0 && previews == 0,
            "empty input keeps alphabetical apps without search callbacks");
        command::evaluate(catalog, "/sam");
        check(loads == 0 && previews == 0, "slash discovery bypasses global callbacks");
        check(command::evaluate(catalog, "zzzzzz").view.rows.empty() && loads == 2 && previews == 0,
            "only opted-in dynamic lists load and unmatched verbs do not preview");
        auto result = command::evaluate(catalog, "Quit");
        check(result.view.rows.size() == 5 && previews == 1 && copied.empty() && launched.empty(),
            "matching previews run once without executing actions");
        check(result.view.rows[0].kind == "App" && result.view.rows[0].subtitle == "Open" &&
            result.view.rows[1].title == "Ready" && result.view.rows[1].kind == "Result" && result.view.rows[1].context == "/sample" &&
            result.view.rows[1].danger && result.view.rows[1].completion.empty() && result.view.spans.empty(),
            "exact app wins ties, preview keeps verb identity and danger without completing bare text");
        check(result.actions[0]().empty() && launched == "quit-app" && result.actions[1]().empty() && copied == "Ready" &&
            result.actions[2]().empty() && copied == "target" && result.actions[3]().empty() && copied == "second" &&
            result.actions[4]().empty() && launched == "quitter-app",
            "global sorting keeps row actions aligned across verbs, apps and multiple dynamic commands");
        auto next = command::evaluate(catalog, "Editor");
        check(next.view.rows.size() == 1 && next.actions[0]().empty() && copied == "target" &&
            result.actions[3]().empty() && copied == "second", "later evaluation leaves all prior command snapshots alive");
        auto math = command::evaluate(catalog, "5 + 5");
        check(math.view.rows.size() == 2 && math.view.rows[0].title == "10" && math.view.rows[1].kind == "App",
            "recognition stays above exact global matches");
        auto fuzzy = command::evaluate(catalog, "qcp");
        check(fuzzy.view.rows.size() == 1 && fuzzy.actions[0]().empty() && copied == "Quick", "fuzzy verb search executes the matched nondefault verb");
        catalog[0].preview = [](auto&, const auto&) -> command::Preview { return {{}, "Unavailable; try again"}; };
        auto error = command::evaluate(catalog, "Quit");
        check(error.view.rows.back().kind == "Error" && !error.actions.back() && error.view.rows[0].kind == "App",
            "preview errors cannot displace executable exact matches");
        for (const auto* declaration : {
            "search='yes'", "search=true,args={'Query'}",
            "search=true,args={{name='First',choices={'One'}},{name='Second',choices={'Two'}}}"}) {
            { std::ofstream f(path); f << "return {name='bad',help='Test'," << declaration << ",verbs={{name='Run',run=function() end}}}"; }
            bool rejected = false;
            try { loadLuaCommand(path, copy, openUrl); } catch (const std::exception&) { rejected = true; }
            check(rejected, "global search rejects invalid flags and requests needing guessed argument combinations");
        }
        { std::ofstream f(path); f << R"(return {name='colors',help='Copy a color',search=true,
            args={{name='Color',choices={'Red','Blue'}},{name='Suffix',default='!'}},
            verbs={{name='Copy',run=function(args) return host.copy(table.concat(args)) end}}})"; }
        std::vector<command::Command> colors{loadLuaCommand(path, copy, openUrl)};
        auto red = command::evaluate(colors, "red");
        check(red.view.rows.size() == 1 && red.actions[0]().empty() && copied == "Red!" &&
            command::evaluate(colors, red.view.rows[0].completion).actions[0]().empty() && copied == "Red!",
            "Lua choice search fills trailing defaults and completes the same executable request");
        std::filesystem::remove(path);
    }
    {
        const auto directory = temp / "native commands";
        const auto configPath = directory / "init.lua";
        const auto pluginsPath = directory / "plugins";
        std::filesystem::path edited, folder;
        std::string hostError;
        int quits = 0, reloads = 0, checks = 0, restarts = 0, rescans = 0;
        std::vector<command::Command> catalog{relayCommand(configPath, pluginsPath, "1.2.3-test",
            [&](const auto& path) { edited = path; return hostError; },
            [&](const auto& path) { folder = path; return hostError; },
            [&](const auto& value) { copied = value; return hostError; },
            [&] { ++quits; return hostError; }, [&] { ++reloads; return hostError; },
            [&] { ++checks; return hostError; }, [&] { ++restarts; return hostError; }, [&] { ++rescans; return hostError; })};
        auto noun = command::evaluate(catalog, "/relay");
        check(noun.view.rows.size() == 1 && noun.view.rows[0].completion == "/relay " && !noun.actions[0], "relay noun completes before any native action");
        copied.clear();
        auto actions = command::evaluate(catalog, noun.view.rows[0].completion);
        check(actions.view.rows.size() == 8 && actions.view.rows[0].title == "Edit Config" &&
            actions.view.rows[1].title == "Open Plugins Folder" && actions.view.rows[2].title == "Reload Plugins" &&
            actions.view.rows[3].title == "Copy Version" && actions.view.rows[4].title == "Check for Updates" &&
            actions.view.rows[5].title == "Restart to Update" && actions.view.rows[6].title == "Rescan Apps" && actions.view.rows[7].title == "Quit",
            "relay exposes eight ordered verbs with config as the default");
        check(hint(actions.view) == "Edit Config " && actions.view.slots[0].kind == Slot::Verb, "entering a multi-verb command ghosts its default verb");
        check(actions.view.rows[3].subtitle == "Relay 1.2.3-test" && copied.empty() && quits == 0 && reloads == 0 &&
            checks == 0 && restarts == 0 && edited.empty() && folder.empty() && !std::filesystem::exists(directory), "discovery shows the version without performing actions or creating files");
        for (const auto& row : actions.view.rows)
            check(row.completion.empty() && !row.subtitle.empty(), "native actions offer descriptions and execution only");
        check(actions.actions[0]().empty() && edited == configPath, "Edit Config creates missing config and dispatches its exact path");
        {
            std::ifstream file(configPath);
            const std::string contents((std::istreambuf_iterator<char>(file)), {});
            check(contents.starts_with(Config::reference()) && contents.find("return {}") != contents.npos,
                "new config documents settings and uses built-in defaults without copying overrides");
        }
        { std::ofstream file(configPath); file << "unfinished user config"; }
        check(actions.actions[0]().empty(), "existing invalid config can still be opened for repair");
        {
            std::ifstream file(configPath);
            const std::string contents((std::istreambuf_iterator<char>(file)), {});
            check(contents == Config::reference() + "unfinished user config", "Edit Config preserves existing user Lua after its reference");
        }
        { std::ofstream file(configPath); file << "-- BEGIN RELAY SETTINGS\nreturn {}"; }
        edited.clear();
        check(!actions.actions[0]().empty() && edited == configPath,
            "Edit Config opens a damaged reference for repair and reports the refresh error");
        { std::ofstream file(configPath); file << "return {}"; }
        check(actions.actions[1]().empty() && folder == pluginsPath && std::filesystem::is_directory(pluginsPath), "Open Plugins Folder creates and opens the user directory");
        check(actions.actions[3]().empty() && copied == actions.view.rows[3].subtitle, "Copy Version copies exactly the visible version");
        check(command::evaluate(catalog, "/relay Reload Plugins").actions[0]().empty() && reloads == 1 &&
            command::evaluate(catalog, "reload").actions[0]().empty() && reloads == 2,
            "explicit and bare Reload Plugins dispatch the same native request");
        check(command::evaluate(catalog, "/relay Rescan Apps").actions[0]().empty() && rescans == 1 &&
            command::evaluate(catalog, "rescan").actions[0]().empty() && rescans == 2,
            "explicit and bare Rescan Apps dispatch the same native request");
        check(command::evaluate(catalog, "/relay Quit").actions[0]().empty() && quits == 1, "explicit Quit dispatches the host shutdown request");
        check(command::evaluate(catalog, "/relay Check for Updates").actions[0]().empty() && checks == 1 &&
            command::evaluate(catalog, "/relay Restart to Update").actions[0]().empty() && restarts == 1,
            "update verbs dispatch separate background check and restart requests");
        for (const auto* input : {"/relay Check for Updates", "Check for Updates"}) {
            auto checkRows = command::evaluate(catalog, input);
            check(checkRows.view.rows.size() == 1 && checkRows.view.rows[0].updateCheck,
                "explicit and global update checks identify their in-place status row");
        }
        for (const auto& row : actions.view.rows)
            check(row.updateCheck == (row.actionLabel == "Check for Updates"),
                "only Check for Updates displays update status");
        for (const auto* input : {"/relay ", "rescan", "Check for Updates"}) {
            const auto rows = command::evaluate(catalog, input);
            for (const auto& row : rows.view.rows)
                check(row.preserveInput == (row.actionLabel == "Rescan Apps" || row.actionLabel == "Check for Updates"),
                    "menu preservation is independent of update status in scoped and global results");
        }
        auto version = command::evaluate(catalog, "/relay version");
        check(version.view.rows.size() == 1 && version.view.rows[0].title == "Copy Version", "typing version discovers Copy Version through normal verb matching");
        auto globalVersion = command::evaluate(catalog, "version");
        check(globalVersion.view.rows.size() == 1 && globalVersion.view.rows[0].subtitle == "Relay 1.2.3-test" &&
            globalVersion.view.rows[0].kind == "Verb" && globalVersion.view.rows[0].context == "/relay" &&
            globalVersion.actions[0]().empty() && copied == globalVersion.view.rows[0].subtitle,
            "global Copy Version shows exactly what it copies");
        check(command::evaluate(catalog, "Quit").actions[0]().empty() && quits == 2 &&
            command::evaluate(catalog, "config").view.rows[0].title == "Edit Config", "bare Quit and config discover native management verbs");
        hostError = "Host operation failed; try again";
        for (const auto& action : actions.actions) check(action() == hostError, "native host failures propagate without changing their recovery message");
        std::filesystem::remove(pluginsPath);
        { std::ofstream file(pluginsPath); file << "not a directory"; }
        folder.clear();
        check(!actions.actions[1]().empty() && folder.empty(), "a blocked plugins directory fails before opening anything");
        std::filesystem::remove(pluginsPath);
        std::filesystem::remove(configPath);
        std::filesystem::remove(directory / "init.lua.relay-backup");
        std::filesystem::remove(directory);
    }
    { std::ofstream f(temp / "rest.lua"); f << R"(return {name='rest', help='Test remaining text', args={{name='color', choices={'Red'}}, {name='text', rest=true}}, verbs={{name='Copy', run=function(args) return host.copy(table.concat(args, '|')) end}, {name='Remove', danger=true, run=function(args) return host.copy('remove|' .. args[2]) end}}})"; }
    {
        std::vector<command::Command> catalog{loadLuaCommand(temp / "rest.lua", copy, openUrl)};
        auto result = command::evaluate(catalog, "/rest RED   hello  world Copy  ");
        check(result.view.rows.size() == 2 && result.actions[0]().empty() && copied == "Red|hello  world Copy  ",
            "remaining text follows fixed arguments and preserves internal and trailing spaces including verb names");
        check(result.view.rows[1].danger && result.view.rows[1].completion.empty() && result.actions[1]().empty() &&
            copied == "remove|hello  world Copy  ", "remaining text commands expose alternate verbs through action rows");
        auto quoted = command::evaluate(catalog, "/rest Red \"hello world\" Copy");
        check(quoted.actions[0]().empty() && copied == "Red|hello world Copy", "closing a quote does not terminate the remaining text argument");
        auto empty = command::evaluate(catalog, "/rest Red \"\"");
        check(empty.actions[0]().empty() && copied == "Red|", "quoted empty remaining text counts as a supplied argument");
        check(hint(command::evaluate(catalog, "/rest Red ").view) == "<text> " &&
            command::evaluate(catalog, "/rest Red \"unfinished").actions.empty(), "missing and unclosed remaining text cannot execute");
    }
    { std::ofstream f(temp / "rest.lua"); f << R"(return {name='rest', help='Test remaining text default', args={{name='text', rest=true, default='hello world'}}, verbs={{name='Copy', run=function(args) return host.copy(args[1]) end}}})"; }
    {
        std::vector<command::Command> catalog{loadLuaCommand(temp / "rest.lua", copy, openUrl)};
        check(command::evaluate(catalog, "/rest ").actions[0]().empty() && copied == "hello world", "remaining text uses defaults only when omitted");
        check(command::evaluate(catalog, "/rest Copy").actions[0]().empty() && copied == "Copy", "typed verb names override remaining text defaults as text");
    }
    for (const auto schema : {"{{name='text', rest=true}, 'next'}", "{{name='text', rest=true, choices={'x'}}}",
            "{{name='text', rest=true, choices={}}}", "{{name='text', rest='true'}}"}) {
        { std::ofstream f(temp / "rest.lua"); f << "return {name='rest', help='Test remaining text schema', args=" << schema << ", verbs={{name='Copy', run=function() end}}}"; }
        bool rejected = false;
        try { loadLuaCommand(temp / "rest.lua", copy, openUrl); } catch (const std::exception&) { rejected = true; }
        check(rejected, "remaining text requires a boolean declaration on the final text argument");
    }
    std::filesystem::remove(temp / "rest.lua");
    // Exercise the API independently of calculator syntax and domain logic.
    for (const auto callback : {"return false", "return {'extra', 'args', 'here'}", "return {}", "return {'missing'}",
            "error('recognition failed')", "host.copy('bad'); return {'Red'}", "host.open_url('https://example.com/'); return {'Red'}"}) {
        { std::ofstream f(temp / "recognize.lua"); f << "return {name='probe', help='Test recognition', args={{name='color', choices={'Red'}}, {name='suffix', default='!'}}, recognize=function(text) " << callback << " end, verbs={{name='Copy', run=function() end}}}"; }
        std::vector<command::Command> catalog{apps, loadLuaCommand(temp / "recognize.lua", copy, openUrl)};
        copied.clear(); opened.clear();
        auto result = command::evaluate(catalog, "fire");
        check(result.view.rows.size() == 2 && result.view.rows[0].kind == "Error" && !result.actions[0] &&
            result.view.rows[1].kind == "App" && copied.empty() && opened.empty(), "bad recognition is contained and cannot perform host actions");
    }
    { std::ofstream f(temp / "recognize.lua"); f << R"(return {name='probe', help='Test recognition', args={{name='color', choices={'Red'}}, {name='suffix', default='!'}}, recognize=function(text) return {'RED'} end, preview=function(args) return table.concat(args) end, verbs={{name='Copy', help='Copy the value', run=function(args) return host.copy(table.concat(args)) end}, {name='Remove', danger=true, run=function() end}}})"; }
    {
        std::vector<command::Command> catalog{loadLuaCommand(temp / "recognize.lua", copy, openUrl)};
        auto result = command::evaluate(catalog, "anything");
        check(result.view.rows[0].subtitle == "Copy — Copy the value", "Lua verb help describes the action beside its preview");
        check(result.view.rows.size() == 1 && result.view.rows[0].title == "Red!" && result.actions[0]().empty() && copied == "Red!",
            "recognition resolves choices and defaults before preview and default execution");
        auto explicitResult = command::evaluate(catalog, "/probe Red ! Remove");
        check(explicitResult.view.rows.size() == 1 && explicitResult.view.rows[0].title == "Red!" && explicitResult.view.rows[0].danger,
            "preview preserves explicitly selected verb and danger metadata");
        check(explicitResult.view.rows[0].subtitle == "Remove — Test recognition", "preview actions remain identifiable without custom verb help");
    }
    for (const auto help : {"false", "'line\\nbreak'"}) {
        { std::ofstream f(temp / "recognize.lua"); f << "return {name='probe', help='Test verb help', verbs={{name='Copy', help=" << help << ", run=function() end}}}"; }
        bool rejected = false;
        try { loadLuaCommand(temp / "recognize.lua", copy, openUrl); } catch (const std::exception&) { rejected = true; }
        check(rejected, "verb help must be a single-line string");
    }
    for (const auto callback : {"return false", "return ''", "error('preview failed')", "host.copy('bad')", "return nil, 'Invalid value; choose another'"}) {
        { std::ofstream f(temp / "recognize.lua"); f << "return {name='probe', help='Test preview', args={{name='color', choices={'Red'}}}, preview=function(args) " << callback << " end, verbs={{name='Copy', run=function() end}}}"; }
        std::vector<command::Command> catalog{loadLuaCommand(temp / "recognize.lua", copy, openUrl)};
        copied.clear();
        auto result = command::evaluate(catalog, "/probe Red");
        auto choice = command::evaluate(catalog, "/probe R");
        check(result.view.rows.size() == 1 && result.view.rows[0].kind == "Error" && !result.actions[0] && copied.empty(), "preview failures disable execution and cannot copy");
        check(choice.view.rows.size() == 1 && !choice.actions[0] && !choice.view.rows[0].completion.empty(), "choice completion cannot bypass a failed preview");
    }
    std::filesystem::remove(temp / "recognize.lua");
    // A second command proves defaults are generic and supplied before Lua runs.
    { std::ofstream f(temp / "defaults.lua"); f << R"(return {name='defaults', help='Test defaults', args={{name='color', choices={'red','blue'}}, {name='label', default='hello world'}, {name='suffix', default=''}}, verbs={{name='Copy', run=function(args) return host.copy(table.concat(args, '|')) end}}})"; }
    {
        std::vector<command::Command> defaults{loadLuaCommand(temp / "defaults.lua", copy, openUrl)};
        auto choice = command::evaluate(defaults, "/defaults r");
        check(choice.actions[0]().empty() && copied == "red|hello world|", "completing a required choice also supplies trailing text defaults");
        auto implicit = command::evaluate(defaults, choice.view.rows[0].completion);
        check(implicit.actions[0]().empty() && copied == "red|hello world|", "required choice completion keeps its default behavior");
        check(hint(implicit.view) == "<label: \"hello world\"> <suffix: \"\"> " && implicit.view.rows[0].completion.empty(), "actions never insert implicit defaults including empty text");
        auto empty = command::evaluate(defaults, "/defaults red \"\"");
        check(empty.actions[0]().empty() && copied == "red||", "explicit empty text overrides a nonempty default");
    }
    std::filesystem::remove(temp / "defaults.lua");
    for (const auto schema : {
        "{{name='a', default='x'}, 'required'}",
        "{{name='a', choices={'x'}, default='missing'}}",
        "{{name='a', choices={}, default='x'}}",
        "{{name='a', default=false}}"}) {
        { std::ofstream f(temp / "defaults.lua"); f << "return {name='defaults', help='Test defaults', args=" << schema << ", verbs={{name='Run', run=function() end}}}"; }
        bool rejected = false;
        try { loadLuaCommand(temp / "defaults.lua", copy, openUrl); } catch (const std::exception&) { rejected = true; }
        check(rejected, "invalid default declarations are rejected during loading");
    }
    { std::ofstream f(temp / "defaults.lua"); f << "return {name='defaults', help='Test defaults', args={{name='color', choices={'Red'}, default='RED'}}, verbs={{name='Copy', run=function(args) return host.copy(args[1]) end}}}"; }
    {
        std::vector<command::Command> defaults{loadLuaCommand(temp / "defaults.lua", copy, openUrl)};
        check(command::evaluate(defaults, "/defaults ").actions[0]().empty() && copied == "Red", "choice defaults use canonical spelling");
    }
    std::filesystem::remove(temp / "defaults.lua");
    { std::ofstream f(temp / "choice.lua"); f << R"(return {name='test', help='Test choices', args={{name='item', choices={'red','blue'}}}, verbs={{name='Use', run=function(args) if args[1] ~= 'red' then error('wrong argument') end end}, {name='Remove All', danger=true, run=function() return false end}}})"; }
    { std::ofstream f(temp / "bad.lua"); f << "return { old_api = true }"; }
    { std::ofstream f(temp / "url.lua"); f << R"(return {name='url', help='Test URL dispatch', args={'url'}, verbs={{name='Open', run=function(args) return host.open_url(args[1]) end}}})"; }
    std::vector<std::string> errors;
    loadLuaCommands(commands, temp, copy, openUrl, [&](auto error) { errors.push_back(error); });
    check(errors.size() == 1 && commands.size() == 6, "bad plugin does not disable valid commands");
    for (const auto url : {"https://example.com/?q=a%20b", "HTTP://example.com/"}) {
        opened.clear();
        check(query("/url " + command::quote(url)).actions[0]().empty() && opened == url, "host permits complete HTTP(S) URLs");
    }
    for (const std::string url : std::vector<std::string>{"", "https://", "relative/path", "file:///C:/Windows/notepad.exe", "javascript:alert(1)",
            "mailto:test@example.com", "https://example.com/raw space", "https://example.com/\n", std::string("https://example.com/\0hidden", 27)}) {
        opened.clear();
        check(!query("/url " + command::quote(url)).actions[0]().empty() && opened.empty(), "host rejects invalid URLs before native dispatch");
    }
    auto verbs = query("/test red ");
    check(verbs.view.rows.size() == 2 && verbs.view.rows[0].title == "Use" && verbs.actions[0]().empty(), "Lua choices and declared default verb order");
    check(verbs.view.rows[0].completion.empty() && verbs.view.rows[1].completion.empty(), "default and alternate actions share the same no-completion rule");
    auto danger = query("/test red Remove All");
    check(danger.actions.size() == 1 && danger.view.rows[0].danger, "exact multiword verb resolves to one row");
    check(danger.view.rows[0].completion.empty(), "explicitly typed verbs still execute without offering an edit");
    check(!danger.actions[0]().empty(), "invalid Lua return is a failure");
    check(!query("/test blue Use").actions[0]().empty(), "Lua exception is a failure");
    std::filesystem::remove(temp / "choice.lua"); std::filesystem::remove(temp / "bad.lua"); std::filesystem::remove(temp / "url.lua"); std::filesystem::remove(temp);

    {
        std::string target, failure;
        desktop::AppAction operation{};
        std::vector<command::Command> catalog{appCommand({{"Editor", "first"}, {"Editor", "second"}},
            [&](const auto& app, desktop::AppAction action) { target = app; operation = action; return failure; })};
        const auto choices = command::evaluate(catalog, "/app ");
        const std::vector<desktop::AppAction> expected{desktop::AppAction::Open, desktop::AppAction::Admin,
            desktop::AppAction::FileLocation, desktop::AppAction::CopyPath};
        for (const auto& row : choices.view.rows) {
            auto verbs = command::evaluate(catalog, row.completion);
            check(verbs.actions.size() == expected.size(), "app completion exposes all four verbs");
            for (size_t i = 0; i < verbs.actions.size(); ++i) {
                check(verbs.actions[i]().empty() && target == row.iconKey && operation == expected[i],
                    "all app verbs preserve duplicate app identities and dispatch distinct operations");
                failure = "Unavailable; try Open instead";
                check(verbs.actions[i]() == failure, "app operation failures retain their recovery message");
                failure.clear();
            }
        }
    }
    {
        std::vector<command::Command> catalog{appCommand({{"Editor", "old-target"}},
            [&](const auto& target, desktop::AppAction) { launched = target; return std::string{}; })};
        auto displayed = command::evaluate(catalog, "Editor");
        catalog[0] = appCommand({{"Editor", "new-target"}},
            [&](const auto& target, desktop::AppAction) { launched = target; return std::string{}; });
        check(displayed.actions[0]().empty() && launched == "old-target",
            "displayed actions own their bindings across catalog replacement");
        check(command::evaluate(catalog, "Editor").actions[0]().empty() && launched == "new-target",
            "new queries bind the rescanned catalog");
    }
    {
        Signal scanSignal;
        Engine scanEngine;
        std::thread::id worker;
        bool failScan = false, pauseScan = false;
        int pluginLoads = 0, scans = 0;
        std::vector<desktop::AppEntry> entries{{"Old App", "old"}};
        std::promise<void> scanStarted, releaseScan;
        auto gate = releaseScan.get_future();
        auto fakeApp = [&](auto entries) {
            return appCommand(std::move(entries), [&](const auto& target, desktop::AppAction) {
                launched = target; return std::string{};
            });
        };
        scanEngine.start([&](auto reload, auto rescan) {
            worker = std::this_thread::get_id();
            return std::vector<command::Command>{fakeApp(entries),
                relayCommand(temp / "unused.lua", temp, "test", [](auto&) { return std::string{}; },
                    [](auto&) { return std::string{}; }, copy, [] { return std::string{}; }, reload,
                    [] { return std::string{}; }, [] { return std::string{}; }, rescan)};
        }, [&](auto& commands) {
            ++pluginLoads;
            command::Command plugin{"probe", "Test plugin state"};
            plugin.verbs.push_back({"Run", false, [count = std::make_shared<int>(0)](auto&) { return std::to_string(++*count); }});
            commands.push_back(std::move(plugin));
        }, [&] { scanSignal.notify(); }, [&] {
            check(std::this_thread::get_id() == worker, "app enumeration runs on the engine worker");
            ++scans;
            if (pauseScan) { scanStarted.set_value(); gate.wait(); }
            if (failScan) throw std::runtime_error("Scan failed");
            return fakeApp(entries);
        });
        auto ask = [&](std::string input) {
            const auto generation = scanEngine.submit(std::move(input));
            scanSignal.wait();
            auto result = scanEngine.takeResult();
            check(result && result->generation == generation, "rescan queries retain their generation");
            return result.value();
        };
        auto run = [&](const Engine::Result& result) {
            check(scanEngine.execute(result.generation, 0, true), "rescan dispatch accepted");
            scanSignal.wait();
            auto done = scanEngine.takeCompletion();
            check(done && done->generation == result.generation, "rescan dispatch completes with its generation");
            return done->error;
        };
        check(ask("").view.rows[0].title == "Old App" && scans == 0, "ordinary queries do not rescan apps");
        check(run(ask("/probe ")) == "1", "plugin state starts once");
        entries = {{"New App", "new"}};
        failScan = true;
        const auto failed = ask("/relay Rescan Apps");
        check(!run(failed).empty(), "scan failure reports an action error");
        failScan = false;
        check(run(failed).empty() && run(failed).empty(), "successful scan leaves the same displayed action executable again");
        check(ask("").view.rows[0].title == "New App" && pluginLoads == 1,
            "retrying a failed scan replaces apps without reloading plugins");
        check(run(ask("/probe ")) == "2", "rescan preserves plugin state");
        failScan = true;
        check(!run(ask("rescan")).empty() && ask("").view.rows[0].title == "New App",
            "failed rescan retains the last working catalog");
        failScan = false;
        check(run(ask("/relay Reload Plugins")).empty() && ask("").view.rows[0].title == "New App",
            "plugin reload retains the rescanned native app catalog");
        entries.clear();
        check(run(ask("rescan")).empty() && ask("").view.rows.empty(), "a successful empty scan removes uninstalled apps");
        entries = {{"Latest App", "latest"}};
        pauseScan = true;
        const auto scanning = ask("rescan");
        check(scanEngine.execute(scanning.generation, 0, true), "slow scan accepted");
        scanStarted.get_future().wait();
        scanEngine.submit("Old App");
        scanEngine.cancel();
        const auto reopened = scanEngine.submit("");
        releaseScan.set_value();
        scanSignal.wait(); scanSignal.wait();
        auto done = scanEngine.takeCompletion();
        auto latest = scanEngine.takeResult();
        check(done && done->generation == scanning.generation && done->error.empty() && latest &&
            latest->generation == reopened && latest->view.rows[0].title == "Latest App",
            "late rescan completion cannot replace a new session; its query uses the refreshed apps");
        check(!scanEngine.execute(scanning.generation, 0, true), "stale scan results cannot execute again");
        scanEngine.stop();
    }
    Signal signal;
    Engine engine;
    std::promise<void> loaded, releaseLoad, running, releaseRun;
    auto loadGate = releaseLoad.get_future(); auto runGate = releaseRun.get_future();
    engine.start([&](auto, auto) {
        loaded.set_value(); loadGate.wait();
        command::Command cmd{"test", "Test execution"};
        cmd.verbs.push_back({"Run", false, [&](auto&) { running.set_value(); runGate.wait(); return std::string("Failed; retry"); }});
        return std::vector<command::Command>{std::move(cmd)};
    }, {}, [&] { signal.notify(); });
    loaded.get_future().wait();
    auto old = engine.submit("/missing ");
    auto generation = engine.submit("/test ");
    releaseLoad.set_value(); signal.wait();
    auto result = engine.takeResult();
    check(result && result->generation == generation && result->view.rows.size() == 1, "pending queries coalesce to the newest input");
    check(!engine.execute(old, 0, true), "old input cannot dispatch an action");
    check(engine.execute(generation, 0, true), "selected current row dispatches");
    running.get_future().wait();
    check(!engine.execute(generation, 0, true), "repeated activation while running is rejected");
    auto newer = engine.submit("/missing ");
    releaseRun.set_value(); signal.wait();
    auto done = engine.takeCompletion();
    check(done && done->generation == generation && !done->error.empty(), "late failure retains its original input generation");
    signal.wait(); result = engine.takeResult();
    check(result && result->generation == newer && result->view.rows.empty(), "input typed during execution is evaluated next");
    auto cancelled = engine.cancel();
    check(cancelled != newer && !engine.execute(newer, 0, true), "hiding invalidates the previous session");
    engine.stop();

    // Hold the worker just after publishing a row. An edit can cancel Enter
    // before execution starts; cancellation must still release the UI's wait.
    Signal cancelSignal;
    Engine cancelledEngine;
    std::promise<void> published, releasePublish;
    auto publishGate = releasePublish.get_future();
    int calls = 0;
    bool firstWake = true;
    cancelledEngine.start([&](auto, auto) {
        command::Command cmd{"test", "Test confirmation"};
        cmd.search = true;
        cmd.verbs.push_back({"Remove", true, [&](auto&) { ++calls; return std::string(); }});
        return std::vector<command::Command>{std::move(cmd)};
    }, {}, [&] {
        if (firstWake) { firstWake = false; published.set_value(); publishGate.wait(); }
        cancelSignal.notify();
    });
    generation = cancelledEngine.submit("/test ");
    published.get_future().wait(); result = cancelledEngine.takeResult();
    check(result && result->view.rows[0].danger, "worker preserves destructive metadata");
    check(cancelledEngine.execute(generation, 0, true), "Enter queued behind worker publication");
    newer = cancelledEngine.submit("Remove");
    releasePublish.set_value();
    cancelSignal.wait(); cancelSignal.wait(); cancelSignal.wait();
    done = cancelledEngine.takeCompletion(); result = cancelledEngine.takeResult();
    check(done && done->generation == generation && calls == 0, "cancelled dispatch completes without executing");
    check(result && result->generation == newer, "cancelled dispatch still permits the next query");
    check(cancelledEngine.execute(newer, 0, false), "unconfirmed dispatch reaches worker validation");
    cancelSignal.wait(); done = cancelledEngine.takeCompletion();
    check(done && !done->error.empty() && calls == 0, "worker rejects an unconfirmed destructive action");
    check(cancelledEngine.execute(newer, 0, true), "confirmed destructive action dispatches");
    cancelSignal.wait(); done = cancelledEngine.takeCompletion();
    check(done && done->error.empty() && calls == 1, "confirmed action executes exactly once");
    cancelledEngine.stop();

    {
        const auto bundled = temp / "bundled";
        const auto user = temp / "user";
        std::filesystem::create_directories(bundled);
        std::filesystem::create_directories(user);
        auto writePlugin = [](const std::filesystem::path& path, const std::string& name, const std::string& version) {
            std::ofstream file(path);
            file << "local count = 0; return {name='" << name << "', help='Test reload', verbs={{name='Copy', run=function() "
                 << "count = count + 1; return host.copy('" << version << "' .. count) end}}}";
        };
        writePlugin(bundled / "probe.lua", "probe", "old");
        writePlugin(user / "removed.lua", "removed", "removed");
        Signal reloadSignal;
        std::vector<std::string> notices;
        std::vector<std::pair<int, std::thread::id>> destroyed;
        std::thread::id worker;
        int nativeLoads = 0, pluginLoads = 0;
        bool failReload = false, pauseReload = false;
        std::promise<void> reloadStarted, releaseReload;
        auto reloadGate = releaseReload.get_future();
        struct Lifetime {
            int load = 0;
            std::vector<std::pair<int, std::thread::id>>* destroyed = nullptr;
            ~Lifetime() { destroyed->emplace_back(load, std::this_thread::get_id()); }
        };
        Engine reloadEngine;
        reloadEngine.start([&](auto reload, auto rescan) {
            worker = std::this_thread::get_id();
            ++nativeLoads;
            return std::vector<command::Command>{
                appCommand({{"Test App", "fake-target"}}, [](auto&, desktop::AppAction) { return std::string{}; }),
                relayCommand(temp / "unused.lua", user, "test", [](auto&) { return std::string{}; },
                    [](auto&) { return std::string{}; }, copy, [] { return std::string{}; }, std::move(reload),
                    [] { return std::string{}; }, [] { return std::string{}; }, std::move(rescan))};
        }, [&](auto& catalog) {
            ++pluginLoads;
            if (pauseReload) { reloadStarted.set_value(); reloadGate.wait(); }
            if (failReload) throw std::runtime_error("Test loader failure");
            auto lifetime = std::make_shared<Lifetime>();
            lifetime->load = pluginLoads;
            lifetime->destroyed = &destroyed;
            auto trackedCopy = [&, lifetime](const std::string& value) {
                check(std::this_thread::get_id() == worker, "reloaded Lua actions execute on their worker");
                copied = value;
                return std::string{};
            };
            auto report = [&](std::string error) { notices.push_back(std::move(error)); };
            loadLuaCommands(catalog, bundled, trackedCopy, openUrl, report);
            loadLuaCommands(catalog, user, trackedCopy, openUrl, report);
        }, [&] { reloadSignal.notify(); });
        auto ask = [&](std::string text) {
            const auto generation = reloadEngine.submit(std::move(text));
            reloadSignal.wait();
            auto result = reloadEngine.takeResult();
            check(result && result->generation == generation, "reload query publishes its own input generation");
            return result.value();
        };
        auto run = [&](const Engine::Result& result) {
            check(reloadEngine.execute(result.generation, 0, true), "reload test dispatches displayed action");
            reloadSignal.wait();
            auto done = reloadEngine.takeCompletion();
            check(done && done->generation == result.generation, "reload action completes its accepted dispatch");
            return done->error;
        };
        check(run(ask("/probe ")).empty() && copied == "old1", "initial plugin runs");
        check(run(ask("/probe ")).empty() && copied == "old2", "plugin state lives between runs");
        const auto oldResult = ask("/probe ");
        writePlugin(bundled / "probe.lua", "probe", "new");
        std::filesystem::remove(user / "removed.lua");
        writePlugin(user / "added.lua", "added", "added");
        { std::ofstream file(user / "broken.lua"); file << "return {"; }
        writePlugin(user / "duplicate.lua", "probe", "duplicate");
        writePlugin(user / "native.lua", "app", "duplicate");
        check(run(ask("/relay Reload Plugins")).empty(), "reload succeeds while individual invalid plugins report notices");
        check(nativeLoads == 1 && pluginLoads == 2 && notices.size() == 3 &&
            std::all_of(notices.begin(), notices.end(), [](auto& text) { return text.find("/relay Reload Plugins") != text.npos; }),
            "reload preserves natives and reports invalid and duplicate plugins with a reload recovery step");
        check(destroyed.size() == 1 && destroyed[0].first == 1 && destroyed[0].second == worker,
            "replacing a catalog releases its old Lua host captures on the worker");
        check(!reloadEngine.execute(oldResult.generation, 0, true), "old plugin views cannot execute after reload");
        check(run(ask("/probe ")).empty() && copied == "new1", "edited plugin gets a fresh Lua state");
        check(run(ask("/added ")).empty() && copied == "added1" && ask("/removed ").view.rows.empty(),
            "reload adds new files and removes deleted files");

        // Keep the previous catalog usable after an unexpected loader failure,
        // including retrying the exact Reload Plugins row without editing.
        failReload = true;
        const auto failed = ask("/relay Reload Plugins");
        check(!run(failed).empty(), "unexpected reload failure completes with an action error");
        failReload = false;
        check(run(failed).empty(), "failed reload can retry the retained action row");
        check(run(ask("/probe ")).empty() && copied == "new1", "retry installs a fresh catalog");

        // Input may change and the launcher may hide while reload runs. The
        // completion belongs to the old request; only the latest query survives.
        const auto reloading = ask("/relay Reload Plugins");
        pauseReload = true;
        writePlugin(bundled / "probe.lua", "probe", "latest");
        check(reloadEngine.execute(reloading.generation, 0, true), "slow reload starts");
        reloadStarted.get_future().wait();
        reloadEngine.submit("/removed ");
        reloadEngine.cancel();
        const auto reopened = reloadEngine.submit("");
        releaseReload.set_value();
        reloadSignal.wait(); reloadSignal.wait();
        const auto late = reloadEngine.takeCompletion();
        const auto latest = reloadEngine.takeResult();
        check(late && late->generation == reloading.generation && late->error.empty() && latest &&
            latest->generation == reopened && latest->view.text.empty() && latest->view.rows.size() == 1 &&
            latest->view.rows[0].title == "Test App", "reload completion cannot replace a reopened session or its native app list");
        pauseReload = false;
        check(run(ask("/probe ")).empty() && copied == "latest1" && nativeLoads == 1,
            "queries after an interrupted session use the new plugins without rescanning apps");
        // Same empty query sent by Shift+Enter's successful completion.
        check(run(ask("reload")).empty() && ask("").view.rows[0].title == "Test App",
            "bare reload followed by clear input returns the retained app catalog");
        reloadEngine.stop();
        check(destroyed.size() == static_cast<size_t>(pluginLoads - 1) &&
            std::all_of(destroyed.begin(), destroyed.end(), [&](auto& item) { return item.second == worker; }),
            "all plugin generations are destroyed on the worker, including shutdown");
        for (auto name : {"added.lua", "broken.lua", "duplicate.lua", "native.lua"}) std::filesystem::remove(user / name);
        std::filesystem::remove(bundled / "probe.lua");
        std::filesystem::remove(user);
        std::filesystem::remove(bundled);
        std::filesystem::remove(temp);
    }
    printf("command checks: %d failure(s)\n", failures);
    return failures;
}
