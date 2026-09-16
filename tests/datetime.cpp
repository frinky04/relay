#include "suites.h"
#include "command.h"
#include "lua_commands.h"
#include "app_command.h"
#include <sol/sol.hpp>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace {
void check(bool ok, const char* description) {
    if (!ok) throw std::runtime_error(description);
}
size_t row(const command::Evaluation& result, const std::string& action) {
    for (size_t i = 0; i < result.view.rows.size(); ++i)
        if (result.view.rows[i].actionLabel == action) return i;
    throw std::runtime_error("Missing action: " + action);
}
}

int runDatetimeTests() {
    const auto source = std::filesystem::path(RELAY_TEST_SOURCE_DIR);
    for (const auto name : {"datetime_parser.lua", "datetime_timezones.lua"}) {
        sol::state lua;
        lua.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math,
            sol::lib::io, sol::lib::os, sol::lib::debug);
        const auto path = source / "tests" / name;
        lua.create_named_table("arg", 0, path.generic_string());
        // Exercise native DST rules when the actual process timezone is Adelaide.
        auto setup = lua.safe_script(R"(
            if os.date('!%H:%M', os.time{year=2026,month=9,day=15,hour=12}) == '02:30'
                and os.date('!%H:%M', os.time{year=2026,month=1,day=15,hour=12}) == '01:30' then
                arg[1] = '--adelaide'
            end
        )", sol::script_pass_on_error);
        check(setup.valid(), "Datetime test setup succeeds");
        auto result = lua.safe_script_file(path.string(), sol::script_pass_on_error);
        if (!result.valid()) { sol::error error = result; throw std::runtime_error(error.what()); }
    }

    std::string copied, copyError;
    int copies = 0;
    auto copy = [&](const std::string& value) { ++copies; copied = value; return copyError; };
    auto open = [](const std::string&) -> std::string { throw std::runtime_error("Datetime must not open URLs"); };
    std::vector<command::Command> catalog{loadLuaCommand(source / "plugins/datetime.lua", copy, open)};
    constexpr std::time_t reference = 1789480800; // 2026-09-15T14:00:00Z
    auto query = [&](const std::string& input, std::time_t now = 1789480800) {
        return command::evaluate(catalog, input, {now});
    };
    check(query("").view.rows.empty(), "Datetime leaves empty input alone");
    check(query("/datetime").view.rows[0].completion == "/datetime ", "Noun discovery retains completion");
    auto defaults = query("/datetime ");
    check(defaults.view.rows.size() == 5 && defaults.view.slots[0].value == "now", "Omitted expression defaults to now");
    auto bare = query("now + 8h");
    check(bare.view.rows.size() == 1 && bare.view.rows[0].actionLabel == "Copy" &&
        bare.view.rows[0].context == "/datetime" && bare.view.rows[0].completion.empty(),
        "Bare recognition exposes only default action and command context without Tab completion");
    auto date = query("/datetime 2026-12-25");
    check(date.view.rows.size() == 3 && date.view.rows[0].title == "2026-12-25" &&
        date.view.rows[1].actionLabel == "Copy Full Date" && date.view.rows[2].actionLabel == "Copy ISO Week",
        "Date precision omits instant-only formats");
    check(date.actions[1]().empty() && copied == "Friday, 25 December 2026", "Weekday output copies full date");
    auto week = query("/datetime 2021-01-01");
    check(week.actions[row(week, "Copy ISO Week")]().empty() && copied == "2020-W53", "ISO week uses its week-year");

    const int beforePreview = copies;
    auto utc = query("/datetime now to UTC");
    check(copies == beforePreview && utc.view.rows.size() == 5, "All formats preview without clipboard operations");
    check(utc.view.rows[0].stacked && utc.view.rows[0].subtitle == "Tue, 15 Sep 2026 · UTC" &&
        utc.view.rows[1].title == "Discord timestamp" && utc.view.rows[2].title == "Discord relative" &&
        utc.view.rows[3].title == "ISO 8601 · UTC" && utc.view.rows[4].title == "Unix seconds",
        "Datetime leads with context once, followed by readable format labels in copy order");
    check(date.view.rows[1].title == "Full date" && date.view.rows[2].title == "ISO week" &&
        date.view.rows[1].subtitle == "Friday, 25 December 2026", "Date-only utilities use precise labels and separate values");
    const std::pair<const char*, const char*> formats[] = {
        {"Copy", "2026-09-15 14:00:00 UTC+00:00"},
        {"Copy ISO", "2026-09-15T14:00:00Z"},
        {"Copy Unix", "1789480800"},
        {"Copy Discord", "<t:1789480800:f>"},
        {"Copy Discord Relative", "<t:1789480800:R>"},
    };
    for (const auto& [action, expected] : formats) {
        const auto index = row(utc, action);
        check(utc.actions[index]().empty() && copied == expected, "Each datetime action copies its prepared format");
        if (std::string_view(action) != "Copy")
            check(utc.view.rows[index].subtitle == copied && utc.view.rows[index].stacked, "Alternate format detail shows exact copied value on its own line");
    }
    auto next = query("/datetime now to UTC", reference + 86400);
    check(next.actions[row(next, "Copy Unix")]().empty() && copied == "1789567200", "New evaluation reads new reference");
    check(utc.actions[row(utc, "Copy Unix")]().empty() && copied == "1789480800", "Old displayed action retains old reference");
    copyError = "Clipboard busy; try again";
    check(utc.actions[row(utc, "Copy Discord")]() == copyError, "Prepared action propagates clipboard failure");
    copyError.clear();
    check(utc.actions[row(utc, "Copy Discord")]().empty() && copied == "<t:1789480800:f>", "Retry preserves exact prepared value");
    auto pacific = query("/datetime 2026-09-15 at 4pm EST to PT");
    check(pacific.actions[0]().empty() && copied == "2026-09-15 14:00:00 UTC-07:00" &&
        pacific.view.rows[0].subtitle.find("From EST (US, UTC-05:00, fixed)") != std::string::npos,
        "Conversion retains source interpretation and destination offset");
    check(query("4pm ET to UTC").view.rows[0].title == "20:00 today", "Regional source observes DST");
    auto fractional = query("/datetime now UTC + 1.0d2.5h");
    check(fractional.actions[row(fractional, "Copy Unix")]().empty() && copied == "1789576200",
        "Whole decimal calendar units and fractional hours retain integer Unix seconds");
    auto local = query("now");
    check(local.actions[0]().empty() && copied.find(" UTC") != std::string::npos &&
        local.view.rows[0].subtitle.find("Local UTC") != std::string::npos,
        "Local display and copy identify exact UTC offset");
    auto roundTrip = query("/datetime " + copied);
    check(roundTrip.actions[row(roundTrip, "Copy Unix")]().empty() && copied == "1789480800",
        "Default copied local datetime parses back to the same instant");
    for (const auto* input : {"tomorrow +", "2026-02-30", "4pm IST", "tomorrow to UTC", "now to",
            "now to UTC to PT", "2026-03-08 at 2:30am ET", "2026-11-01 at 1:30am ET", "now Copy"}) {
        check(query(input).view.rows.empty(), "Invalid bare dates do not claim global input");
        auto result = query(std::string("/datetime ") + input);
        check(result.view.rows.size() == 1 && result.view.rows[0].kind == "Error" && !result.actions[0] &&
            !result.view.rows[0].subtitle.empty(), "Invalid explicit expressions give one non-executable recovery error");
    }
    check(query("/datetime \"tomorrow at 7pm").view.rows.empty(), "Unclosed quotes cannot execute");
    {
        auto all = catalog;
        all.insert(all.begin(), loadLuaCommand(source / "plugins/calc.lua", copy, open));
        all.push_back(appCommand({{"2026-12-25", "fake-date-app"}}, [](auto&, auto) { return std::string{}; }));
        auto result = command::evaluate(all, "2026-12-25", {reference});
        check(result.view.rows.size() == 3 && result.view.rows[0].context == "/datetime" &&
            result.view.rows[1].context == "/calc" && result.view.rows[2].title == "2026-12-25",
            "Date recognition outranks subtraction while preserving calculator and app matches");
        check(command::evaluate(all, "5 + 5", {reference}).view.rows[0].title == "10", "Arithmetic keeps calculator behavior");
    }

    // Exercise the API independently from datetime's grammar and formatting.
    const auto temporary = std::filesystem::temp_directory_path() / "relay-prepared-command-test.lua";
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{temporary};
    auto load = [&](const std::string& script) {
        { std::ofstream file(temporary); file << script; }
        return std::vector<command::Command>{loadLuaCommand(temporary, copy, open)};
    };
    auto prepared = load(R"(
        local latest
        return {name='probe', help='Test prepared actions', search=true,
            args={{name='Value', default='value'}},
            recognize=function(text, context) return {tostring(context.now)} end,
            preview=function(args, context)
                assert(os == nil)
                assert(not pcall(host.time) and not pcall(host.date, '*t'))
                local fields = {year=2026,month=9,day=15,hour=12}
                host.time(fields)
                assert(fields.min == nil and fields.isdst == nil)
                assert(type(host.date('*t', context.now)) == 'table')
                context.now = 0
                return args[1]
            end,
            verbs={
                {name='Copy Hidden', preview=function() return false end, run=function() error('hidden') end},
                {name='Copy Broken', preview=function() return nil,'Choose another value' end, run=function() error('broken') end},
                {name='Copy Value', preview=function(args, context)
                    latest = {title=args[1], subtitle='Prepared detail', value=tostring(context.now)..'\n'..args[1]}
                    return latest
                end, run=function(args, value) latest.value='changed'; return host.copy(value) end},
                {name='Copy Plain', preview=function() return 'Plain result' end,
                    run=function(args, value) assert(value == nil); return host.copy(args[1]) end}
            }}
    )");
    auto first = command::evaluate(prepared, "/probe first", {100});
    check(first.view.rows.size() == 3 && first.view.rows[0].kind == "Error" && !first.actions[0] &&
        first.view.rows[1].subtitle == "Prepared detail" && first.view.rows[1].stacked,
        "Hidden and failed previews retain action alignment");
    auto second = command::evaluate(prepared, "/probe second", {200});
    check(first.actions[1]().empty() && copied == "100\nfirst" && second.actions[1]().empty() && copied == "200\nsecond",
        "Prepared strings survive later queries and mutations of returned Lua tables");
    check(second.actions[2]().empty() && copied == "second", "String previews pass nil prepared value");
    auto searched = command::evaluate(prepared, "Copy", {300});
    check(searched.view.rows.size() == 3 && searched.view.rows.back().kind == "Error", "Search omits hidden actions and sorts errors last");
    check(searched.actions[row(searched, "Copy Value")]().empty() && copied == "300\nvalue", "Search binds same prepared execution");
    auto hidden = command::evaluate(prepared, "recognized", {400});
    check(hidden.view.rows.empty(), "Unavailable default recognition does not silently execute another verb");

    auto choices = load(R"(return {name='probe',help='Test choices',search=true,
        args={{name='Color',choices={'Red','Blue'}}}, verbs={{name='Copy',
        preview=function(args,context) if args[1]=='Blue' then return false end
            return {title=args[1],value=args[1]..context.now} end,
        run=function(_,value) return host.copy(value) end}}})");
    auto red = command::evaluate(choices, "/probe R", {500});
    check(red.actions[0]().empty() && copied == "Red500" && !red.view.rows[0].completion.empty(), "Choice completion binds prepared default action");
    auto blue = command::evaluate(choices, "/probe B", {500});
    check(!blue.actions[0] && !blue.view.rows[0].completion.empty() &&
        command::evaluate(choices, "Blue", {500}).view.rows.empty(), "Hidden choice action retains scoped completion but is absent globally");

    for (const auto* body : {"return {}", "return {title='x',value={}}", "return {title='x',subtitle='bad\\nline'}",
            "return {title='x',unknown=true}", "return true", "return nil", "return nil,''",
            "host.copy('bad')", "host.open_url('https://example.com')", "error('broken')"}) {
        auto invalid = load(std::string("return {name='probe',help='Test invalid previews', verbs={{name='Copy',preview=function() ") +
            body + " end,run=function() error('must not run') end}}}");
        const int before = copies;
        auto result = command::evaluate(invalid, "/probe ", {reference});
        check(result.view.rows.size() == 1 && !result.actions[0] && result.view.rows[0].kind == "Error" && copies == before,
            "Invalid or side-effecting per-verb previews fail without executing");
    }
    // The prepared closure owns its Lua state after the catalog is released.
    auto retained = command::evaluate(catalog, "/datetime now to UTC", {reference});
    catalog.clear();
    check(retained.actions[row(retained, "Copy ISO")]().empty() && copied == "2026-09-15T14:00:00Z",
        "Prepared action retains its callback after catalog replacement");
    std::puts("datetime and prepared command checks passed");
    return 0;
}
