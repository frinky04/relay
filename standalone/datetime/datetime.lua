-- Standalone English date/time queries. Lua 5.3+, no host or external libraries.
-- parse(text, reference?) -> result | nil, { code, message }
-- format(result, mode?, timezone?) -> { title, subtitle, copy, kind } | nil, error
-- Reference and timestamp are Unix seconds; calendar fields use local time.
-- Grammar inspired by Chrono; this is an independent, deliberately smaller parser.

local M = {}
local MIN_YEAR, MAX_YEAR = 1970, 2999
local months = { "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December" }
local weekdays = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" }
local month_names, weekday_names = {}, {}
for i, name in ipairs(months) do
    month_names[name:lower()] = i
    month_names[name:sub(1, 3):lower()] = i
end
month_names.sept = 9
for i, name in ipairs(weekdays) do
    weekday_names[name:lower()] = i
    weekday_names[name:sub(1, 3):lower()] = i
end
weekday_names.tues, weekday_names.thurs = 2, 4

local function fail(code, message)
    return nil, { code = code, message = message }
end

local function syntax()
    return fail("syntax", "Use a date or time, optionally followed by + or - and a duration")
end

local function range_error()
    return fail("range", "Use a date between 1970 and 2999 supported by your system clock")
end

local function leap(y)
    return y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0)
end

local function month_days(y, m)
    if m == 2 then return leap(y) and 29 or 28 end
    if m == 4 or m == 6 or m == 9 or m == 11 then return 30 end
    return 31
end

local function valid_date(y, m, d)
    return m >= 1 and m <= 12 and d >= 1 and d <= month_days(y, m)
end

-- Gregorian ordinals: 0001-01-01 is day 1, a Monday. Calendar arithmetic
-- never adds 86400 seconds, so dates keep their meaning across DST changes.
local function ordinal(y, m, d)
    local n = y - 1
    local days = 365 * n + n // 4 - n // 100 + n // 400 + d
    for month = 1, m - 1 do days = days + month_days(y, month) end
    return days
end

local MIN_DAY, MAX_DAY = ordinal(MIN_YEAR, 1, 1), ordinal(MAX_YEAR, 12, 31)

local function from_ordinal(n)
    if n < MIN_DAY or n > MAX_DAY then return range_error() end
    local y = math.floor((n - 1) / 365.2425) + 1
    while ordinal(y, 1, 1) > n do y = y - 1 end
    while ordinal(y + 1, 1, 1) <= n do y = y + 1 end
    local d, m = n - ordinal(y, 1, 1) + 1, 1
    while d > month_days(y, m) do d, m = d - month_days(y, m), m + 1 end
    return { year = y, month = m, day = d }
end

local function day_number(d)
    return ordinal(d.year, d.month, d.day)
end

local function copy_date(d)
    return { year = d.year, month = d.month, day = d.day }
end

local function local_date(timestamp)
    local ok, d = pcall(os.date, "*t", timestamp)
    if not ok or not d or d.year < MIN_YEAR or d.year > MAX_YEAR then return range_error() end
    return d
end

local function same_clock(a, b)
    return a.year == b.year and a.month == b.month and a.day == b.day
        and a.hour == b.hour and a.min == b.min and a.sec == b.sec
end

local function select_timestamp(found, normalized, not_before)
    local timestamp, count = nil, 0
    for t in pairs(found) do timestamp, count = math.max(timestamp or t, t), count + 1 end
    if not_before and timestamp and timestamp < not_before then
        return fail("past", "Choose an upcoming date or time")
    end
    if count > 1 then
        return fail("ambiguous_time", "This time occurs twice when clocks change; specify a fixed UTC offset or choose another time")
    end
    if count == 0 then
        if not normalized then return range_error() end
        return fail("nonexistent_time", "This time is skipped when clocks change; choose a time outside the skipped interval")
    end
    return timestamp
end

-- mktime normalizes invalid wall times. Round-trip each DST interpretation
-- instead of accepting that normalization or silently choosing a repeated hour.
local function local_timestamp(d, not_before)
    local found, normalized = {}, false
    for _, hint in ipairs({ "auto", false, true }) do
        local input = copy_date(d)
        input.hour, input.min, input.sec = d.hour, d.min, d.sec
        if hint ~= "auto" then input.isdst = hint end
        local ok, t = pcall(os.time, input)
        if ok and t then
            local actual = local_date(t)
            if actual then
                normalized = true
                if same_clock(d, actual) then found[t] = true end
            end
        end
    end
    -- A repeated clock reading can still lie ahead even if the reference's
    -- wall clock is later. Only roll forward once both occurrences have passed.
    return select_timestamp(found, normalized, not_before)
end

-- Foreign zones never change TZ or pass foreign wall-clock fields to os.time.
-- Fixed offsets use Gregorian arithmetic; US regional zones use the rules in
-- effect since 2007 (NIST DST rules). They are not a historical/IANA database.
local EPOCH_DAY = ordinal(1970, 1, 1)
local function civil_timestamp(d)
    return (day_number(d) - EPOCH_DAY) * 86400 + d.hour * 3600 + d.min * 60 + d.sec
end

local function civil_date(timestamp)
    local days = math.floor(timestamp / 86400)
    local d, err = from_ordinal(EPOCH_DAY + days)
    if not d then return nil, err end
    local seconds = timestamp - days * 86400
    d.hour, d.min, d.sec = seconds // 3600, (seconds % 3600) // 60, seconds % 60
    return d
end

local function offset_label(minutes)
    if minutes == 0 then return "UTC+00:00" end
    return string.format("UTC%s%02d:%02d", minutes < 0 and "-" or "+", math.abs(minutes) // 60, math.abs(minutes) % 60)
end

local zones = {
    utc = { name = "UTC", offset = 0 }, gmt = { name = "GMT", offset = 0 },
    ["local"] = false,
}
for _, item in ipairs({ { "Eastern", "ET", "EST", "EDT", -300 },
    { "Central", "CT", "CST", "CDT", -360 },
    { "Mountain", "MT", "MST", "MDT", -420 },
    { "Pacific", "PT", "PST", "PDT", -480 } }) do
    local name, short, standard, daylight, offset = table.unpack(item)
    local region = { name = name .. " Time", standard = offset, daylight = offset + 60,
        standard_name = standard, daylight_name = daylight }
    zones[short:lower()], zones[name:lower()], zones[name:lower() .. " time"] = region, region, region
    local std = { name = standard, offset = offset, us = true }
    local dst = { name = daylight, offset = offset + 60, us = true }
    zones[standard:lower()], zones[daylight:lower()] = std, dst
    zones[name:lower() .. " standard time"] = std
    zones[name:lower() .. " daylight time"] = dst
end

local function us_transitions(year, zone)
    local function sunday(month, occurrence)
        local first = ordinal(year, month, 1)
        local day = 1 + (7 - first % 7) % 7 + (occurrence - 1) * 7
        return civil_timestamp({ year = year, month = month, day = day, hour = 2, min = 0, sec = 0 })
    end
    return sunday(3, 2) - zone.standard * 60, sunday(11, 1) - zone.daylight * 60
end

local function zone_date(timestamp, zone)
    if not zone then return local_date(timestamp) end
    local offset, abbreviation = zone.offset, zone.name
    if not offset then
        local standard, err = civil_date(timestamp + zone.standard * 60)
        if not standard then return nil, err end
        if standard.year < 2007 then
            return fail("timezone_range", "For dates before 2007, specify a fixed abbreviation or UTC offset")
        end
        local start, finish = us_transitions(standard.year, zone)
        local daylight = timestamp >= start and timestamp < finish
        offset = daylight and zone.daylight or zone.standard
        abbreviation = daylight and zone.daylight_name or zone.standard_name
    end
    local d, err = civil_date(timestamp + offset * 60)
    if not d then return nil, err end
    d.offset_minutes = offset
    if zone.standard then
        d.zone_label = zone.name .. " (" .. abbreviation .. ", " .. offset_label(offset) .. ")"
    elseif zone.us then
        d.zone_label = zone.name .. " (US, " .. offset_label(offset) .. ", fixed)"
    elseif zone.name == "UTC" or zone.name == offset_label(offset) then d.zone_label = zone.name
    else d.zone_label = zone.name .. " (" .. offset_label(offset) .. ")" end
    return d
end

local function zone_timestamp(d, zone, not_before)
    if not zone then return local_timestamp(d, not_before) end
    if zone.standard and d.year < 2007 then
        return fail("timezone_range", "For dates before 2007, specify a fixed abbreviation or UTC offset")
    end
    local candidates, normalized = {}, false
    for _, offset in ipairs(zone.offset and { zone.offset } or { zone.standard, zone.daylight }) do
        local t = civil_timestamp(d) - offset * 60
        local actual = zone_date(t, zone)
        if actual then
            normalized = true
            if same_clock(d, actual) then candidates[t] = true end
        end
    end
    return select_timestamp(candidates, normalized, not_before)
end

-- Tokens keep numeric spelling so 07:30 and 2026-09-15 can be checked
-- without interpreting arbitrary bare numbers as clocks or years.
local function tokenize(text)
    local tokens, i = {}, 1
    text = text:lower()
    while i <= #text do
        local rest = text:sub(i)
        local spaces = rest:match("^%s+")
        if spaces then
            i = i + #spaces
        else
            local token = rest:match("^%d+%.%d+") or rest:match("^%d+") or rest:match("^[a-z]+")
            if not token then
                token = rest:sub(1, 1)
                if not token:match("^[%+%-%:,]$") then return syntax() end
            end
            tokens[#tokens + 1] = token
            i = i + #token
        end
    end
    return tokens
end

local function integer(s)
    if s and s:match("^%d+$") then return tonumber(s) end
end

local function clock(tokens, i)
    local word = tokens[i]
    if word == "noon" or word == "midnight" then
        return { hour = word == "noon" and 12 or 0, min = 0, sec = 0 }, i + 1
    end
    local hour = integer(word)
    if not hour or #word > 2 then
        return fail("time", "Use a clock time such as 7am or 23:30")
    end
    local minute, second, colon = 0, 0, tokens[i + 1] == ":"
    i = i + 1
    if colon then
        minute = integer(tokens[i + 1])
        if not minute or #tokens[i + 1] ~= 2 then
            return fail("time", "Use two digits for minutes, such as 07:30")
        end
        i = i + 2
        if tokens[i] == ":" then
            second = integer(tokens[i + 1])
            if not second or #tokens[i + 1] ~= 2 then
                return fail("time", "Use two digits for seconds, such as 07:30:00")
            end
            i = i + 2
        end
    end
    local meridiem = tokens[i]
    if meridiem == "am" or meridiem == "pm" then
        if hour < 1 or hour > 12 then return fail("time", "Use an hour from 1 to 12 with am or pm") end
        hour = hour % 12 + (meridiem == "pm" and 12 or 0)
        i = i + 1
    elseif not colon then
        return fail("time", "Add am or pm, or use a 24-hour time such as 07:00")
    end
    if hour > 23 or minute > 59 or second > 59 then
        return fail("time", "Use hours 00 to 23 and minutes and seconds 00 to 59")
    end
    return { hour = hour, min = minute, sec = second }, i
end

local function ordinal_suffix(tokens, i, day)
    local suffix = tokens[i]
    if suffix ~= "st" and suffix ~= "nd" and suffix ~= "rd" and suffix ~= "th" then return i end
    local expected = "th"
    if day % 100 < 11 or day % 100 > 13 then
        expected = ({ [1] = "st", [2] = "nd", [3] = "rd" })[day % 10] or "th"
    end
    if suffix ~= expected then return nil end
    return i + 1
end

local function date_anchor(tokens, ref)
    local word, today = tokens[1], day_number(ref)
    local relative = { today = 0, tomorrow = 1, yesterday = -1 }
    if relative[word] then
        local d, err = from_ordinal(today + relative[word])
        if not d then return nil, err end
        return d, 2
    end
    local modifier, weekday, i = nil, weekday_names[word], 2
    if word == "this" or word == "next" or word == "last" then
        modifier, weekday, i = word, weekday_names[tokens[2]], 3
        if not weekday then return syntax() end
    end
    if weekday then
        local current = (today - 1) % 7 + 1
        local offset = (weekday - current) % 7
        if modifier == "next" and offset == 0 then offset = 7 end
        if modifier == "last" then offset = -((current - weekday - 1) % 7 + 1) end
        if modifier == "this" then offset = weekday - current end
        local d, err = from_ordinal(today + offset)
        if not d then return nil, err end
        d.upcoming_weekday = modifier == nil
        return d, i
    end

    local year, month, day
    if integer(word) and tokens[2] == "-" then
        year, month, day = integer(word), integer(tokens[3]), integer(tokens[5])
        if #word ~= 4 or not month or #tokens[3] ~= 2 or tokens[4] ~= "-"
            or not day or #tokens[5] ~= 2 then
            return fail("date", "Use an ISO date such as 2027-08-17")
        end
        i = 6
    elseif month_names[word] then
        month, day = month_names[word], integer(tokens[2])
        if not day or #tokens[2] > 2 then return fail("date", "Add a day, such as August 17") end
        i = ordinal_suffix(tokens, 3, day)
        if not i then return fail("date", "Correct the ordinal suffix or use a day number such as 17") end
    elseif integer(word) and #word <= 2 then
        day = integer(word)
        i = ordinal_suffix(tokens, 2, day)
        if not i then return fail("date", "Correct the ordinal suffix or use a day number such as 17") end
        month = month_names[tokens[i]]
        if not month then return nil end -- A clock, not a named date.
        i = i + 1
    else
        return nil
    end
    if not year then
        local comma = tokens[i] == ","
        if comma then i = i + 1 end
        if tokens[i] and tokens[i]:match("^%d%d%d%d$") then
            year, i = tonumber(tokens[i]), i + 1
        elseif comma then
            return fail("date", "Add a four-digit year after the comma")
        end
    end
    if year and (year < MIN_YEAR or year > MAX_YEAR) then return range_error() end
    -- A missing year may resolve to a leap year; reject impossible month/day
    -- pairs before looking for their next occurrence.
    if not valid_date(year or 2000, month, day) then
        return fail("date", "Use a valid day for the selected month and year")
    end
    return { year = year, month = month, day = day, upcoming_year = not year }, i
end

local function seconds_of_day(d)
    return d.hour * 3600 + d.min * 60 + d.sec
end

local function anchor(tokens, ref)
    if tokens[1] == "now" then
        local d = copy_date(ref)
        d.hour, d.min, d.sec = ref.hour, ref.min, ref.sec
        d.exact_now = true
        return d, 2
    end
    local d, i = date_anchor(tokens, ref)
    if not d and i then return nil, i end
    local time_only = not d
    if time_only then d, i = copy_date(ref), 1 end
    local at = tokens[i] == "at"
    if at then i = i + 1 end
    local has_time = time_only or at or (tokens[i] and tokens[i] ~= "+" and tokens[i] ~= "-")
    if has_time then
        local time, next_i = clock(tokens, i)
        if not time then return nil, next_i end
        d.hour, d.min, d.sec, i = time.hour, time.min, time.sec, next_i
    end
    if d.upcoming_year then
        d.year = ref.year
        while not valid_date(d.year, d.month, d.day) or day_number(d) < day_number(ref) do
            d.year = d.year + 1
            if d.year > MAX_YEAR then return range_error() end
        end
    end
    if has_time and day_number(d) == day_number(ref) then
        if time_only then d.roll = "day"
        elseif d.upcoming_weekday then d.roll = "week"
        elseif d.upcoming_year then d.roll = "year" end
    end
    return d, i
end

local units = {}
local function unit(names, field, scale)
    for name in names:gmatch("%S+") do units[name] = { field = field, scale = scale } end
end
unit("s sec secs second seconds", "seconds", 1)
unit("m min mins minute minutes", "seconds", 60)
unit("h hr hrs hour hours", "seconds", 3600)
unit("d day days", "days", 1)
unit("w wk wks week weeks", "days", 7)

local function duration(tokens, i)
    local out, seen = { days = 0, seconds = 0, has_time = false }, {}
    local count = 0
    while tokens[i] do
        local amount, u = tonumber(tokens[i]), units[tokens[i + 1]]
        if not amount or not u then break end
        -- Keep products and later additions inside a small exact numeric range.
        if amount > 40000000000 then return range_error() end
        local key = u.field .. u.scale
        if seen[key] then return fail("duration", "Use each duration unit once, such as 2h30m") end
        seen[key] = true
        if u.field == "days" and amount % 1 ~= 0 then
            return fail("duration", "Use whole days or weeks; use hours for elapsed fractions")
        end
        local value = amount * u.scale
        if u.field == "seconds" then
            local rounded = math.floor(value + 0.5)
            if math.abs(value - rounded) > 0.000001 then
                return fail("duration", "Use a duration that resolves to whole seconds")
            end
            value, out.has_time = rounded, true
        end
        out[u.field] = out[u.field] + value
        count, i = count + 1, i + 2
        if tokens[i] == "and" or tokens[i] == "," then
            i = i + 1
            if not tonumber(tokens[i]) or not units[tokens[i + 1]] then
                return fail("duration", "Add a duration after the separator, such as 1h and 30m")
            end
        end
    end
    if count == 0 then return fail("duration", "Add a duration such as 8h or 7h30m") end
    return out, i
end

local function timezone_error()
    return fail("timezone", "Use UTC, a numeric offset such as UTC+09:30, or a supported US timezone")
end

local function read_zone(tokens, i)
    local zone, after
    for count = 3, 1, -1 do
        if tokens[i + count - 1] then
            local key = table.concat(tokens, " ", i, i + count - 1)
            if zones[key] ~= nil then zone, after = zones[key], i + count; break end
        end
    end
    if not after then return nil end
    -- UTC+05:30 is an offset, while UTC + 2h is arithmetic. Unit-bearing
    -- durations remain in the expression for its existing duration parser.
    if zone and (zone.name == "UTC" or zone.name == "GMT")
        and (tokens[after] == "+" or tokens[after] == "-") and not units[tokens[after + 2]] then
        local sign, number = tokens[after] == "-" and -1 or 1, tokens[after + 1]
        if not integer(number) then return timezone_error() end
        local hour, minute = tonumber(number), 0
        if #number == 4 then hour, minute = tonumber(number:sub(1, 2)), tonumber(number:sub(3, 4))
        elseif #number > 2 then return timezone_error() end
        after = after + 2
        if tokens[after] == ":" then
            local part = tokens[after + 1]
            if #number > 2 or not integer(part) or #part ~= 2 then return timezone_error() end
            minute, after = tonumber(part), after + 2
        end
        if hour > 14 or minute > 59 or (hour == 14 and minute ~= 0) then return timezone_error() end
        local offset = sign * (hour * 60 + minute)
        zone = { name = offset_label(offset), offset = offset }
    end
    return zone, after
end

local function extract_zone(tokens)
    local output, selected, found = {}, nil, false
    local i = 1
    while tokens[i] do
        local zone, after = read_zone(tokens, i)
        if type(after) == "table" then return nil, after end
        if after then
            if found or i == 1 or (tokens[after] and tokens[after] ~= "+" and tokens[after] ~= "-") then
                return fail("timezone", "Put one timezone after the date/time, optionally followed by duration arithmetic")
            end
            selected, found, i = zone, true, after
        else
            output[#output + 1], i = tokens[i], i + 1
        end
    end
    return output, selected
end

local function destination_zone(text)
    if type(text) ~= "string" or #text > 64 then return timezone_error() end
    local tokens = tokenize(text)
    if not tokens or #tokens == 0 then return timezone_error() end
    local zone, after = read_zone(tokens, 1)
    if type(after) == "number" and not tokens[after] then return zone end
    return timezone_error()
end

local function expression(tokens, ref)
    local start = tokens[1] == "in" and 2 or 1
    if start == 2 or (tonumber(tokens[start]) and units[tokens[start + 1]]) then
        local dur, i = duration(tokens, start)
        if not dur then return nil, i end
        local sign = 1
        if start == 1 then
            if tokens[i] == "ago" then sign, i = -1, i + 1
            elseif tokens[i] == "from" and tokens[i + 1] == "now" then i = i + 2
            else return fail("duration", "Use in 8h, 8h from now, or 8h ago") end
        end
        if tokens[i] then return syntax() end
        local d = copy_date(ref)
        if dur.has_time then
            d.hour, d.min, d.sec, d.exact_now = ref.hour, ref.min, ref.sec, true
        end
        return { base = d, duration = dur, sign = sign }
    end
    local d, i = anchor(tokens, ref)
    if not d then return nil, i end
    local dur, sign
    if tokens[i] == "+" or tokens[i] == "-" then
        sign = tokens[i] == "+" and 1 or -1
        dur, i = duration(tokens, i + 1)
        if not dur then return nil, i end
    end
    if tokens[i] then return syntax() end
    return { base = d, duration = dur, sign = sign }
end

function M.parse(text, reference)
    if type(text) ~= "string" or #text > 256 then
        return fail("input", "Use a date/time expression of at most 256 bytes")
    end
    if reference == nil then reference = os.time() end
    if type(reference) ~= "number" or not math.tointeger(reference) then
        return fail("reference", "Supply the reference time as whole Unix seconds")
    end
    local tokens, err
    tokens, err = tokenize(text)
    if not tokens then return nil, err end
    if #tokens == 0 then return fail("input", "Enter now, tomorrow, or an expression such as now + 8h") end
    local zone
    tokens, zone = extract_zone(tokens)
    if not tokens then return nil, zone end
    local ref
    ref, err = zone_date(reference, zone)
    if not ref then return nil, err end
    local parsed
    parsed, err = expression(tokens, ref)
    if not parsed then return nil, err end
    local d, dur = parsed.base, parsed.duration
    if zone and not d.hour then
        return fail("time_required", "Add a clock time to convert between timezones, such as tomorrow at 4pm EST")
    end
    if dur and dur.has_time and not d.hour then
        return fail("time_required", "Add a starting time, such as tomorrow at 7am + 8h")
    end
    local timestamp
    if d.hour then
        if d.exact_now then timestamp = reference
        else timestamp, err = zone_timestamp(d, zone, d.roll and reference) end
        if not timestamp and d.roll and (err.code == "past"
            or (err.code == "nonexistent_time" and seconds_of_day(d) < seconds_of_day(ref))) then
            if d.roll == "year" then
                repeat
                    d.year = d.year + 1
                    if d.year > MAX_YEAR then return range_error() end
                until valid_date(d.year, d.month, d.day)
            else
                local future
                future, err = from_ordinal(day_number(d) + (d.roll == "day" and 1 or 7))
                if not future then return nil, err end
                d.year, d.month, d.day = future.year, future.month, future.day
            end
            timestamp, err = zone_timestamp(d, zone)
        end
        if not timestamp then return nil, err end
    end
    if dur then
        if dur.days ~= 0 then
            local shifted
            shifted, err = from_ordinal(day_number(d) + parsed.sign * dur.days)
            if not shifted then return nil, err end
            d.year, d.month, d.day = shifted.year, shifted.month, shifted.day
            if d.hour then
                timestamp, err = zone_timestamp(d, zone)
                if not timestamp then return nil, err end
            end
        end
        if d.hour and dur.seconds ~= 0 then
            timestamp = timestamp + parsed.sign * dur.seconds
            d, err = zone_date(timestamp, zone)
            if not d then return nil, err end
        end
    end
    local source
    if timestamp and zone then
        source, err = zone_date(timestamp, zone)
        if not source then return nil, err end
        source.timezone = zone.name
        d, err = local_date(timestamp)
        if not d then return nil, err end
    end
    local result = copy_date(d)
    result.source = source
    result.precision = d.hour and "datetime" or "date"
    result.reference = reference
    if d.hour then
        result.hour, result.min, result.sec = d.hour, d.min, d.sec
        result.timestamp = timestamp
    end
    return result
end

local function iso_date(d)
    return string.format("%04d-%02d-%02d", d.year, d.month, d.day)
end

local function iso_datetime(d)
    return iso_date(d) .. string.format(" %02d:%02d:%02d", d.hour, d.min, d.sec)
end

local function long_date(d)
    return string.format("%s, %d %s %d", weekdays[(day_number(d) - 1) % 7 + 1], d.day, months[d.month], d.year)
end

function M.format(result, mode, timezone)
    mode = mode or (result.precision == "date" and "date" or "time")
    local source, zone = result.source, nil
    if timezone ~= nil then
        local err
        zone, err = destination_zone(timezone)
        if err then return nil, err end
        if mode == "utc" or mode == "epoch" then
            return fail("format", "Use time, now, date, day, or week when specifying a destination timezone")
        end
        if zone then
            if not result.timestamp then return fail("time_required", "Add a clock time before converting to another timezone") end
            local converted
            converted, err = zone_date(result.timestamp, zone)
            if not converted then return nil, err end
            converted.timestamp, converted.reference = result.timestamp, result.reference
            result = converted
        end
    end
    local title, subtitle, copy, kind
    if mode == "date" or mode == "day" or mode == "week" then
        kind, subtitle = "Date", long_date(result)
        if mode == "date" then title = iso_date(result)
        elseif mode == "day" then title, subtitle = long_date(result), iso_date(result)
        else
            local n = day_number(result)
            local thursday = n + 3 - (n - 1) % 7
            local year = result.year
            if thursday < ordinal(year, 1, 1) then year = year - 1 end
            if thursday >= ordinal(year + 1, 1, 1) then year = year + 1 end
            title = string.format("%04d-W%02d", year, (thursday - ordinal(year, 1, 1)) // 7 + 1)
        end
        copy = title
    elseif mode == "time" or mode == "now" or mode == "utc" or mode == "epoch" then
        if not result.timestamp then
            return fail("time_required", "Add a clock time to get a datetime, UTC value, or Unix timestamp")
        end
        kind = "Time"
        if mode == "time" then
            local ref, err = zone_date(result.reference, zone)
            if not ref then return nil, err end
            local delta = day_number(result) - day_number(ref)
            local relative = ({ [-1] = "yesterday", [0] = "today", [1] = "tomorrow" })[delta] or iso_date(result)
            title = string.format("%02d:%02d", result.hour, result.min)
            if result.sec ~= 0 then title = title .. string.format(":%02d", result.sec) end
            title = title .. " " .. relative
            subtitle = long_date(result) .. " | " .. (zone and result.zone_label or "Local time")
            copy = iso_datetime(result) .. (zone and " " .. offset_label(result.offset_minutes) or "")
        elseif mode == "now" then
            title = iso_datetime(result) .. (zone and " " .. offset_label(result.offset_minutes) or "")
            subtitle = zone and result.zone_label or "Local datetime"
        elseif mode == "utc" then
            local ok, utc = pcall(os.date, "!*t", result.timestamp)
            if not ok or not utc then return range_error() end
            title, subtitle = iso_datetime(utc), "UTC"
        else
            title, subtitle = tostring(result.timestamp), "Unix seconds"
        end
        copy = copy or title
    else
        return fail("format", "Use time, date, now, utc, epoch, week, or day")
    end
    if zone and kind == "Date" then subtitle = subtitle .. " | " .. result.zone_label end
    if source and (not zone or zone.name ~= source.timezone) then subtitle = subtitle .. " | From " .. source.zone_label end
    return { title = title, subtitle = subtitle, copy = copy, kind = kind }
end

return M
