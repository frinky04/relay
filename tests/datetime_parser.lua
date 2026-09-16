-- lua datetime_parser.lua [--adelaide]
-- The optional suite requires the process timezone to be Adelaide; it does not
-- change the machine timezone. All other tests work in UTC or local time.
local directory = (arg[0]:match("^(.*[/\\])") or "./")
host = { date = os.date, time = os.time }
local plugin = dofile(directory .. "../plugins/datetime.lua")
local function upvalue(fn, wanted)
    for i = 1, math.huge do
        local name, value = debug.getupvalue(fn, i)
        assert(name, "Missing private helper: " .. wanted)
        if name == wanted then return value end
    end
end
local datetime = upvalue(upvalue(plugin.preview, "query"), "datetime")
local passed, failed = 0, 0

local function eq(actual, expected)
    assert(actual == expected, string.format("expected %s, got %s", tostring(expected), tostring(actual)))
end

local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1
    else failed = failed + 1; io.stderr:write(name .. ": " .. tostring(err) .. "\n") end
end

local function at(y, m, d, h, min, sec, isdst)
    return os.time({ year = y, month = m, day = d, hour = h or 0, min = min or 0, sec = sec or 0, isdst = isdst })
end

local reference = at(2026, 9, 15, 23, 30)
local function parse(text, ref)
    local result, err = datetime.parse(text, ref or reference)
    assert(result, err and err.code .. ": " .. err.message)
    return result
end

local function reject(text, code, ref)
    local result, err = datetime.parse(text, ref or reference)
    eq(result, nil)
    assert(type(err) == "table" and type(err.message) == "string")
    if code then eq(err.code, code) end
    assert(not err.message:match("%.$"), "error copy has a trailing period")
end

local function rendered(text, mode, ref)
    local result, err = datetime.format(parse(text, ref), mode)
    assert(result, err and err.message)
    return result
end

local datetime_cases = {
    { "now", "2026-09-15 23:30:00" },
    { "now + 8 hours", "2026-09-16 07:30:00" },
    { "in 8h", "2026-09-16 07:30:00" },
    { "8 hours from now", "2026-09-16 07:30:00" },
    { "30 minutes ago", "2026-09-15 23:00:00" },
    { "now + 7h30m", "2026-09-16 07:00:00" },
    { "now + 7.5 hours", "2026-09-16 07:00:00" },
    { "now + 1h and 30m", "2026-09-16 01:00:00" },
    { "now + 1h,30m", "2026-09-16 01:00:00" },
    { "now + 0.1m", "2026-09-15 23:30:06" },
    { "now + 90s", "2026-09-15 23:31:30" },
    { "now - 24h", "2026-09-14 23:30:00" },
    { "now + 0h", "2026-09-15 23:30:00" },
    { "now + 1d2h", "2026-09-17 01:30:00" },
    { "in 1d2h", "2026-09-17 01:30:00" },
    { "now - 1w2d3h", "2026-09-06 20:30:00" },
    { "11pm + 8h", "2026-09-17 07:00:00" }, -- Next 11pm, then arithmetic.
    { "today at 11pm + 8h", "2026-09-16 07:00:00" },
    { "tomorrow at 7am - 8h", "2026-09-15 23:00:00" },
    { "today at 7am", "2026-09-15 07:00:00" },
    { "7am", "2026-09-16 07:00:00" },
    { "23:30", "2026-09-15 23:30:00" },
    { "23:29:59", "2026-09-16 23:29:59" },
    { "noon", "2026-09-16 12:00:00" },
    { "midnight", "2026-09-16 00:00:00" },
    { "today midnight", "2026-09-15 00:00:00" },
    { "tomorrow at 12am", "2026-09-16 00:00:00" },
    { "tomorrow at 12pm", "2026-09-16 12:00:00" },
    { "tomorrow 07:30:25", "2026-09-16 07:30:25" },
    { "friday 15:00", "2026-09-18 15:00:00" },
    { "tuesday 7am", "2026-09-22 07:00:00" },
    { "this tuesday 7am", "2026-09-15 07:00:00" },
    { "2027-08-17 at noon", "2027-08-17 12:00:00" },
    { "September 15 at 7am", "2027-09-15 07:00:00" },
    { "  ToMoRRoW   AT  7 AM - 8 H  ", "2026-09-15 23:00:00" },
}
for _, case in ipairs(datetime_cases) do
    test(case[1], function() eq(rendered(case[1], "now").title, case[2]) end)
end

local date_cases = {
    { "today", "2026-09-15" }, { "tomorrow", "2026-09-16" }, { "yesterday", "2026-09-14" },
    { "in 2 weeks", "2026-09-29" }, { "5 days ago", "2026-09-10" },
    { "2 weeks from now", "2026-09-29" }, { "tomorrow + 2w", "2026-09-30" },
    { "friday", "2026-09-18" }, { "next friday", "2026-09-18" },
    { "next tuesday", "2026-09-22" }, { "tues", "2026-09-15" },
    { "last tuesday", "2026-09-08" }, { "last friday", "2026-09-11" },
    { "this monday", "2026-09-14" }, { "this sunday", "2026-09-20" },
    { "17 August", "2027-08-17" }, { "August 17", "2027-08-17" },
    { "17 Aug 2027", "2027-08-17" }, { "17th August 2027", "2027-08-17" },
    { "August 17th, 2027", "2027-08-17" }, { "Sept 15", "2026-09-15" },
    { "29 Feb", "2028-02-29" }, { "29 february 2000", "2000-02-29" },
    { "2026-12-31 + 1d", "2027-01-01" }, { "2028-03-01 - 1d", "2028-02-29" },
    { "2100-03-01 - 1d", "2100-02-28" }, { "2000-02-28 + 1d", "2000-02-29" },
    { "2026-01-31 + 1d", "2026-02-01" }, { "1970-01-01", "1970-01-01" },
    { "2999-12-31", "2999-12-31" },
}
for _, case in ipairs(date_cases) do
    test(case[1], function()
        local result = parse(case[1])
        eq(result.precision, "date")
        eq(result.timestamp, nil)
        eq(result.hour, nil)
        eq(datetime.format(result).title, case[2])
    end)
end

local invalid = {
    "now + 8 elephants", "tomorrow nonsense", "tomorrow at", "tomorrow at 7am nonsense",
    "now + 1h + 30m", "now + 1h - 30m", "now +", "now + -8h", "now - -8h",
    "now + 1h 1h", "now + 1hr 1hour", "now + 1h and", "now + 1h,", "in 8h ago",
    "in 8h from now", "8h", "+8h", "-8h", "now + 1.5d", "in 0.5 weeks", "now + 0.5s",
    "in a few hours", "in eight hours", "this week", "next month", "now + 1month", "tonight",
    "tomorrow morning", "morning", "7", "730", "24:00", "12:60", "12:30:60", "12:3",
    "0pm", "13am", "23:30pm", "7.30pm", "2026-02-30", "2026-00-10", "2026-13-10",
    "2026-04-31", "29 Feb 2027", "31 April", "August 0", "August 32", "2026-2-01",
    "26-02-01", "August 17, 26", "August 17,", "17st August", "11st August", "21th August",
    "03/04", "2027-08-17T07:30Z", "7am IST", "8 hours before tomorrow at 7am",
    "from 7am to 8am", "(now + 8h)", "now + 9999999999999999999999999999999999999999h",
}
for _, query in ipairs(invalid) do test("reject " .. query, function() reject(query) end) end

test("date-only elapsed arithmetic needs a time", function()
    reject("tomorrow + 8h", "time_required")
    reject("today + 0s", "time_required")
    for _, mode in ipairs({ "time", "now", "utc", "epoch" }) do
        local result, err = datetime.format(parse("tomorrow"), mode)
        eq(result, nil); eq(err.code, "time_required")
    end
end)

test("input and range boundaries", function()
    reject("", "input"); reject("  ", "input"); reject(123, "input")
    reject(string.rep("x", 257), "input")
    reject("1969-12-31", "range"); reject("3000-01-01", "range")
    reject("1970-01-01 - 1d", "range"); reject("2999-12-31 + 1d", "range")
    for _, ref in ipairs({ "now", false, 0/0, math.huge, reference + 0.5 }) do
        local result, err = datetime.parse("now", ref)
        eq(result, nil); eq(err.code, "reference")
    end
end)

test("formatting and copy values", function()
    local row = rendered("now + 8h", "time")
    eq(row.title, "07:30 tomorrow")
    eq(row.copy, "2026-09-16 07:30:00")
    eq(row.subtitle, "Wednesday, 16 September 2026 | Local time")
    eq(row.kind, "Time")
    eq(rendered("now - 24h", "time").title, "23:30 yesterday")
    eq(rendered("now + 48h", "time").title, "23:30 2026-09-17")
    eq(rendered("now + 8h", "epoch").copy, tostring(reference + 28800))
    eq(rendered("now + 8h", "utc").copy, os.date("!%Y-%m-%d %H:%M:%S", reference + 28800))
    eq(rendered("today", "day").copy, "Tuesday, 15 September 2026")
    eq(rendered("2021-01-01", "week").copy, "2020-W53")
    eq(rendered("2024-12-30", "week").copy, "2025-W01")
    eq(rendered("2016-01-04", "week").copy, "2016-W01")
    local result, err = datetime.format(parse("now"), "bad")
    eq(result, nil); eq(err.code, "format")
end)

test("parsing and formatting do not mutate earlier results", function()
    local first = parse("now + 8h")
    parse("now - 8h")
    datetime.format(first, "utc")
    eq(datetime.format(first, "now").copy, "2026-09-16 07:30:00")
    eq(first.reference, reference)
end)

test("Gregorian day arithmetic across an entire leap cycle", function()
    -- Compare against the runtime calendar at noon, far from DST transitions.
    for year = 1997, 2004 do
        for month = 1, 12 do
            local start = string.format("%04d-%02d-28", year, month)
            local expected = os.date("%Y-%m-%d", at(year, month, 35, 12))
            eq(rendered(start .. " + 1w", "date").title, expected)
        end
    end
end)

test("malformed input cannot raise a Lua error", function()
    local alphabet = { "now", "tomorrow", "2026", "-", "+", "08", "17", "at", "7", "am", "h", ":", "noon", "in", "," }
    -- Local deterministic generator; does not change math.random's global seed.
    local seed = 117
    for _ = 1, 3000 do
        local words = {}
        for _ = 1, 6 do
            seed = (seed * 48271) % 2147483647
            words[#words + 1] = alphabet[seed % #alphabet + 1]
        end
        local query = table.concat(words, " ")
        local ok, result, err = pcall(datetime.parse, query, reference)
        assert(ok, query .. ": " .. tostring(result))
        assert(result or (err and err.code and err.message), query)
    end
end)

if arg[1] == "--adelaide" then
    test("Adelaide timezone precondition", function()
        eq(os.date("!%H:%M", at(2026, 9, 15, 12)), "02:30")
        eq(os.date("!%H:%M", at(2026, 1, 15, 12)), "01:30")
    end)
    test("spring transition uses elapsed hours", function()
        local ref = at(2026, 10, 4, 0, 30)
        local result = parse("in 2 hours", ref)
        eq(result.timestamp - ref, 7200)
        eq(datetime.format(result, "now").copy, "2026-10-04 03:30:00")
        local night = at(2026, 10, 3, 23, 30)
        eq(rendered("now + 8h", "time", night).title, "08:30 tomorrow")
        eq(parse("now + 8h", night).timestamp - night, 28800)
    end)
    test("autumn transition uses elapsed hours", function()
        local ref = at(2026, 4, 4, 23, 30)
        eq(rendered("now + 8h", "time", ref).title, "06:30 tomorrow")
        eq(parse("now + 8h", ref).timestamp - ref, 28800)
    end)
    test("calendar days differ from 24 elapsed hours", function()
        local ref = at(2026, 10, 3, 23, 30)
        local day, hours = parse("now + 1d", ref), parse("now + 24h", ref)
        eq(day.timestamp - ref, 23 * 3600)
        eq(hours.timestamp - ref, 24 * 3600)
        eq(datetime.format(day, "now").copy, "2026-10-04 23:30:00")
        eq(datetime.format(hours, "now").copy, "2026-10-05 00:30:00")
        eq(rendered("in 1d", "date", ref).title, "2026-10-04")
        eq(parse("now + 1d2h", ref).timestamp - ref, 25 * 3600)
    end)
    test("reject DST gaps and folds", function()
        reject("2026-10-04 at 02:30", "nonexistent_time")
        reject("2026-04-05 at 02:30", "ambiguous_time")
        reject("now + 1d", "nonexistent_time", at(2026, 10, 3, 2, 30))
        reject("now + 1d", "ambiguous_time", at(2026, 4, 4, 2, 30))
    end)
    test("an exact instant inside the repeated hour remains exact", function()
        local early = at(2026, 4, 5, 2, 30, 0, true)
        local late = at(2026, 4, 5, 2, 30, 0, false)
        eq(late - early, 3600)
        for _, ref in ipairs({ early, late }) do
            eq(parse("now", ref).timestamp, ref)
            eq(parse("now + 0h", ref).timestamp, ref)
            eq(parse("now + 30m", ref).timestamp, ref + 1800)
        end
    end)
    test("next occurrence respects both readings of a repeated clock time", function()
        local between = at(2026, 4, 5, 2, 45, 0, true)
        reject("2:30", "ambiguous_time", between)
        reject("sunday at 2:30", "ambiguous_time", between)
        reject("April 5 at 2:30", "ambiguous_time", between)
        local after = at(2026, 4, 5, 3, 30)
        eq(rendered("2:30", "now", after).copy, "2026-04-06 02:30:00")
        eq(rendered("sunday at 2:30", "now", after).copy, "2026-04-12 02:30:00")
        eq(rendered("April 5 at 2:30", "now", after).copy, "2027-04-05 02:30:00")
    end)
    test("a skipped clock time that has passed rolls to its next occurrence", function()
        local after = at(2026, 10, 4, 3, 30)
        eq(rendered("2:30", "now", after).copy, "2026-10-05 02:30:00")
        reject("today at 2:30", "nonexistent_time", after)
    end)
else
    print("Adelaide DST suite skipped (use --adelaide in that timezone)")
end

print(string.format("%d passed, %d failed (%s)", passed, failed, _VERSION))
assert(failed == 0, "Datetime parser tests failed")
