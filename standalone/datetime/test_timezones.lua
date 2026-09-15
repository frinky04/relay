-- lua path/to/test_timezones.lua; works under UTC or the system's local zone.
local directory = arg[0]:match("^(.*[/\\])") or "./"
local datetime = dofile(directory .. "datetime.lua")
local reference = 1789480800 -- 2026-09-15T14:00:00Z
local passed, failed = 0, 0
local function eq(actual, expected)
    assert(actual == expected, string.format("expected %s, got %s", tostring(expected), tostring(actual)))
end
local function test(name, fn)
    local ok, err = pcall(fn)
    if ok then passed = passed + 1
    else failed = failed + 1; io.stderr:write(name .. ": " .. tostring(err) .. "\n") end
end
local function parse(query, ref)
    local r, err = datetime.parse(query, ref or reference)
    assert(r, err and err.code .. ": " .. err.message)
    return r
end
local function reject(query, code, ref)
    local r, err = datetime.parse(query, ref or reference)
    eq(r, nil)
    assert(err and err.code and err.message)
    if code then eq(err.code, code) end
end
local function utc(query, ref)
    return datetime.format(parse(query, ref), "utc").copy
end

local cases = {
    { "4pm EST", "2026-09-15 21:00:00" },
    { "4pm ET", "2026-09-15 20:00:00" },
    { "October 10 at 4pm EST + 2h", "2026-10-10 23:00:00" },
    { "tomorrow at 4pm Eastern Time", "2026-09-16 20:00:00" },
    { "today at 4pm eastern standard time", "2026-09-15 21:00:00" },
    { "today at 4pm eastern daylight time", "2026-09-15 20:00:00" },
    { "2026-07-15 at 4pm CST", "2026-07-15 22:00:00" },
    { "2026-07-15 at 4pm CDT", "2026-07-15 21:00:00" },
    { "2026-07-15 at 4pm MST", "2026-07-15 23:00:00" },
    { "2026-07-15 at 4pm MDT", "2026-07-15 22:00:00" },
    { "2026-07-15 at 4pm PST", "2026-07-16 00:00:00" },
    { "2026-07-15 at 4pm PDT", "2026-07-15 23:00:00" },
    { "2026-07-15 at 4pm CT", "2026-07-15 21:00:00" },
    { "2026-07-15 at 4pm MT", "2026-07-15 22:00:00" },
    { "2026-07-15 at 4pm Pacific Time", "2026-07-15 23:00:00" },
    { "2026-01-15 at 4pm ET", "2026-01-15 21:00:00" },
    { "2026-01-15 at 4pm PT", "2026-01-16 00:00:00" },
    { "2026-01-15 at 4pm EDT", "2026-01-15 20:00:00" }, -- Fixed even in winter.
    { "2026-09-15 at 4pm UTC", "2026-09-15 16:00:00" },
    { "2026-09-15 at 4pm GMT", "2026-09-15 16:00:00" },
    { "2026-09-15 at 4pm UTC+09:30", "2026-09-15 06:30:00" },
    { "2026-09-15 at 4pm UTC+0930", "2026-09-15 06:30:00" },
    { "2026-09-15 at 4pm UTC+9:30", "2026-09-15 06:30:00" },
    { "2026-09-15 at 4pm GMT-3:30", "2026-09-15 19:30:00" },
    { "2026-09-15 at 4pm UTC+5:45", "2026-09-15 10:15:00" },
    { "2026-09-15 at 4pm UTC+14", "2026-09-15 02:00:00" },
    { "2026-09-15 at 4pm UTC-14:00", "2026-09-16 06:00:00" },
    { "2026-09-15 at 4pm UTC+00", "2026-09-15 16:00:00" },
    { "2026-09-15 at 4pm UTC + 2h", "2026-09-15 18:00:00" },
    { "2026-09-15 at 4pm UTC-05:00 - 2h", "2026-09-15 19:00:00" },
    { "now EST", "2026-09-15 14:00:00" },
    { "now ET + 2h", "2026-09-15 16:00:00" },
    { "in 2h ET", "2026-09-15 16:00:00" },
    { "2h ago UTC", "2026-09-15 12:00:00" },
    { "2026-03-08 at 1:59:59 ET", "2026-03-08 06:59:59" },
    { "2026-03-08 at 3am ET", "2026-03-08 07:00:00" },
    { "2026-11-01 at 00:59:59 ET", "2026-11-01 04:59:59" },
    { "2026-11-01 at 2am ET", "2026-11-01 07:00:00" },
    { "2026-11-01 at 1:30 EDT", "2026-11-01 05:30:00" },
    { "2026-11-01 at 1:30 EST", "2026-11-01 06:30:00" },
    { "2026-03-08 at 00:30 ET + 2h", "2026-03-08 07:30:00" },
    { "2026-03-07 at noon ET + 1d", "2026-03-08 16:00:00" },
    { "2026-03-07 at noon ET + 24h", "2026-03-08 17:00:00" },
    { "2026-03-07 at noon EST + 1d", "2026-03-08 17:00:00" },
    { "2026-11-01 at 00:30 ET + 2h", "2026-11-01 06:30:00" },
    { "2006-07-15 at 4pm EST", "2006-07-15 21:00:00" },
    { "2007-03-11 at 3am ET", "2007-03-11 07:00:00" },
    { "2026-10-04 at 02:30 EST", "2026-10-04 07:30:00" }, -- Adelaide's skipped wall time is irrelevant.
}
for _, case in ipairs(cases) do test(case[1], function() eq(utc(case[1]), case[2]) end) end

test("reference dates belong to the source zone", function()
    local ref = 1789520400 -- 2026-09-16T01:00:00Z, still September 15 in Eastern Time.
    eq(utc("today at 4pm ET", ref), "2026-09-15 20:00:00")
    eq(utc("tomorrow at 4pm ET", ref), "2026-09-16 20:00:00")
    eq(utc("4pm ET", ref), "2026-09-16 20:00:00")
    eq(utc("tuesday at 4pm ET", ref), "2026-09-22 20:00:00")
    eq(utc("September 15 at 4pm ET", ref), "2027-09-15 20:00:00")
end)

test("regional gap and overlap rejection", function()
    for _, zone in ipairs({ "ET", "CT", "MT", "PT" }) do
        reject("2026-03-08 at 02:30 " .. zone, "nonexistent_time")
        reject("2026-11-01 at 01:30 " .. zone, "ambiguous_time")
    end
    reject("2026-03-07 at 02:30 ET + 1d", "nonexistent_time")
    reject("2026-10-31 at 01:30 ET + 1d", "ambiguous_time")
    reject("2006-07-15 at 4pm ET", "timezone_range")
    reject("2007-01-01 at noon ET - 1d", "timezone_range")
    reject("1:30 ET", "ambiguous_time", 1793511900)
    eq(utc("1:30 ET", 1793518200), "2026-11-02 06:30:00")
end)

test("local result fields and source interpretation", function()
    local result = parse("4pm EST")
    eq(result.source.timezone, "EST")
    eq(result.source.offset_minutes, -300)
    eq(result.source.hour, 16)
    eq(result.source.day, 15)
    eq(result.source.zone_label, "EST (US, UTC-05:00, fixed)")
    local local_fields = os.date("*t", result.timestamp)
    for _, field in ipairs({ "year", "month", "day", "hour", "min", "sec" }) do
        eq(result[field], local_fields[field])
    end
    assert(datetime.format(result).subtitle:find("From EST (US, UTC-05:00, fixed)", 1, true))
    eq(parse("2026-03-07 at noon ET + 24h").source.hour, 13)
    local repeated = parse("2026-11-01 at 00:30 ET + 2h")
    eq(repeated.source.hour, 1); eq(repeated.source.offset_minutes, -300)
    eq(parse("now").source, nil)
end)

test("destination conversion and copy round-trip", function()
    local result = parse("2026-09-15 at 4pm EST")
    local hour = result.hour
    local target = assert(datetime.format(result, "now", "PT"))
    eq(target.copy, "2026-09-15 14:00:00 UTC-07:00")
    assert(target.subtitle:find("Pacific Time (PDT, UTC-07:00)", 1, true))
    eq(parse(target.copy).timestamp, result.timestamp)
    eq(datetime.format(result, "time", "EST").title, "16:00 today")
    eq(datetime.format(result, "now", "UTC+09:30").copy, "2026-09-16 06:30:00 UTC+09:30")
    eq(datetime.format(result, "date", "UTC+09:30").copy, "2026-09-16")
    eq(datetime.format(result, "day", "PT").copy, "Tuesday, 15 September 2026")
    eq(datetime.format(result, "week", "PT").copy, "2026-W38")
    eq(datetime.format(result, "now", "local").copy, datetime.format(result, "now").copy)
    eq(result.hour, hour); eq(result.source.hour, 16)
    for _, name in ipairs({ "", "IST", "Europe/London", "UTC+15", "UTC+05:60", "ET junk", "ET + 2h" }) do
        local row, err = datetime.format(result, "time", name)
        eq(row, nil); eq(err.code, "timezone")
    end
    local row, err = datetime.format(parse("tomorrow"), "date", "ET")
    eq(row, nil); eq(err.code, "time_required")
    row, err = datetime.format(result, "utc", "ET")
    eq(row, nil); eq(err.code, "format")
end)

local invalid = {
    "4pm EST UTC", "EST 4pm", "tomorrow ET at 4pm", "4pm ET + 2h PST",
    "4pm UTC+15", "4pm UTC+14:01", "4pm UTC+00:60", "4pm UTC-05:0",
    "4pm UTC+053", "4pm UTC+05:30:00", "4pm UTC+5.5", "4pm UTC+",
    "4pm UTC+-5", "4pm UTC+05:30 rubbish", "4pm UTC+5 dayz", "4pm UTC +",
    "4pm EST-05:00", "4pm IST", "4pm BST", "4pm UTC + 2h + 3h", "4pm +09:30",
}
for _, query in ipairs(invalid) do test("reject " .. query, function() reject(query) end) end
test("a source timezone needs a clock time", function()
    reject("tomorrow EST", "time_required")
    reject("in 1d ET", "time_required")
end)

test("malformed zone expressions return errors, not Lua exceptions", function()
    local parts = { "4", "pm", "UTC", "+", "-", ":", "05", "30", "ET", "time", "EST", "1h", "tomorrow", "local" }
    local seed = 192
    for _ = 1, 3000 do
        local words = {}
        for _ = 1, 7 do
            seed = seed * 48271 % 2147483647
            words[#words + 1] = parts[seed % #parts + 1]
        end
        local text = table.concat(words, " ")
        local ok, r, err = pcall(datetime.parse, text, reference)
        assert(ok, text .. ": " .. tostring(r))
        assert(r or (err and err.code and err.message), text)
    end
end)

print(string.format("%d timezone tests passed, %d failed (%s)", passed, failed, _VERSION))
os.exit(failed == 0 and 0 or 1)
