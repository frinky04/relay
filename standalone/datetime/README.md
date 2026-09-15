# Standalone datetime queries

`datetime.lua` turns a short English expression into a local date or an exact
instant. It returns a Lua module, **not an installed Relay plugin**. Nothing in
this directory is connected to Relay's build or plugin discovery.

Requires Lua 5.3 or newer with the standard `os`, `math`, `string`, and `table`
libraries. Tested on Lua 5.5.1 on Windows. No packages, network access, global
state changes, `host.*` calls, timers, or scheduling. Queries default to the
process's local timezone, as provided by `os.time` and `os.date`, and may specify
a supported source timezone. The result's top-level calendar fields remain local.

## Use

Load the file with `dofile` using its absolute path, or use `require` after adding
its directory to `package.path`.

```lua
local datetime = dofile("C:/path/to/datetime.lua")

local result, err = datetime.parse("now + 8 hours")
if not result then
    print(err.message)
    return
end

local display = assert(datetime.format(result, "time"))
print(display.title)     -- e.g. "07:30 tomorrow"
print(display.subtitle)  -- "Wednesday, 16 September 2026 | Local time"
print(display.copy)      -- "2026-09-16 07:30:00"
```

Pass a reference timestamp for deterministic calculations:

```lua
local reference = os.time {
    year = 2026, month = 9, day = 15, hour = 23, min = 30, sec = 0,
}
local wake = assert(datetime.parse("now + 8h", reference))
local bed = assert(datetime.parse("tomorrow at 7am - 8h", reference))
assert(datetime.format(wake, "now").copy == "2026-09-16 07:30:00")
assert(datetime.format(bed, "now").copy == "2026-09-15 23:00:00")
```

## API

`parse(text, reference?)` returns a result, or `nil, error`. The reference defaults
to one reading of `os.time()` per call and must be whole Unix seconds.

| Result field | Meaning |
| --- | --- |
| `precision` | `"date"` or `"datetime"` |
| `year`, `month`, `day` | Local calendar date |
| `hour`, `min`, `sec` | Present only for a datetime |
| `timestamp` | Exact Unix seconds; present only for a datetime |
| `reference` | Reference timestamp used to resolve the query |
| `source` | Present for an explicit foreign zone: resulting source-zone calendar fields, `timezone`, `offset_minutes`, and `zone_label` |

Date-only results deliberately have no timestamp or inferred clock time. For
example, `in 2 weeks` is a date; `now + 2 weeks` retains the current clock time.
Adding hours to a date-only anchor requires an explicit time: use
`tomorrow at 7am + 8h`, not `tomorrow + 8h`.

`format(result, mode?, timezone?)` accepts a result returned by `parse` and returns
`{ title, subtitle, copy, kind }`, or `nil, error`. It does not modify the result.
Its default mode is `date` for dates and `time` for datetimes.

| Mode | Output |
| --- | --- |
| `time` | Local clock with today/tomorrow/yesterday, or an ISO date; copies the full absolute local datetime |
| `date` | `YYYY-MM-DD` |
| `now` | `YYYY-MM-DD HH:MM:SS` in local time |
| `utc` | `YYYY-MM-DD HH:MM:SS` in UTC |
| `epoch` | Unix seconds |
| `week` | ISO week with its week-year, e.g. `2020-W53` |
| `day` | English weekday and full date |

`time`, `now`, `utc`, and `epoch` require a datetime. The optional destination
timezone works with `time`, `now`, `date`, `day`, and `week`; the `utc` and `epoch`
modes have fixed meanings and do not accept it. Conversion to a foreign zone
requires a datetime even when showing only its date. Omitting the destination,
or using `local`, preserves the original local formatting behavior.

With a foreign destination, `time` and `now` copy the absolute datetime plus a
numeric UTC offset, such as `2026-09-15 14:00:00 UTC-07:00`. That copy value can
be parsed back to the same timestamp. Subtitles identify the source interpretation
and destination where applicable. Relative display labels use the reference in
the displayed timezone. Reparse to refresh a result after time passes.

Errors have `{ code, message }`. Codes are `input`, `reference`, `syntax`, `date`,
`time`, `duration`, `range`, `time_required`, `nonexistent_time`,
`ambiguous_time`, `timezone`, `timezone_range`, and `format`. Messages include a
recovery step.

## Grammar

Input is case-insensitive and tolerates whitespace. The entire expression must
parse; trailing words, extra arithmetic operators, and unknown units are errors.
Input is limited to 256 bytes and dates to 1970–2999, within the system runtime's
supported timestamp range.

| Form | Examples |
| --- | --- |
| Anchors | `now`, `today`, `tomorrow`, `yesterday` |
| Clocks | `7am`, `11:30 pm`, `23:30`, `07:30:15`, `noon`, `midnight` |
| Weekdays | `fri`, `friday`, `this friday`, `next friday`, `last friday` |
| Dates | `2027-08-17`, `17 August`, `August 17`, `17th Aug 2027`, `August 17, 2027` |
| Date and clock | `tomorrow at 7am`, `friday 15:00`, `2027-08-17 at noon` |
| Relative duration | `in 8h`, `8 hours from now`, `30 minutes ago` |
| Arithmetic | `now + 7h30m`, `11pm + 8 hours`, `tomorrow at 7am - 8h` |
| Source timezone | `4pm EST`, `tomorrow at 4pm Eastern Time`, `October 10 at 4pm EST + 2h` |

Durations accept digits with the following units:

- Seconds: `s`, `sec`, `secs`, `second`, `seconds`
- Minutes: `m`, `min`, `mins`, `minute`, `minutes`
- Hours: `h`, `hr`, `hrs`, `hour`, `hours`
- Days: `d`, `day`, `days`
- Weeks: `w`, `wk`, `wks`, `week`, `weeks`

Combine units without separators, with whitespace, with `and`, or with a comma:
`1h30m`, `1 hour 30 minutes`, `1h and 30m`, `1h,30m`. Each unit may occur once;
`1h 1hour` is an error. Days and weeks must be whole numbers. Decimal seconds,
minutes, and hours are accepted when they resolve to whole seconds, e.g. `7.5h`
or `0.1m`. Zero is allowed. Arithmetic permits one `+` or `-` followed by one
possibly compound duration; write `now + 1h30m`, not `now + 1h + 30m`.

Resolution rules:

- A clock alone means its next occurrence, including the reference instant.
  At 23:30, `11pm + 8h` starts at tomorrow's 23:00. Use `today at 11pm + 8h`
  to start at today's 23:00.
- Bare weekdays mean the upcoming occurrence, including today if applicable.
  An explicitly supplied clock that has passed advances a bare weekday a week.
- `next friday` is the first Friday strictly after today; `last friday` is the
  first Friday strictly before today. `this friday` is in the current
  Monday–Sunday week, even if it is in the past.
- Missing years select the next valid occurrence, including today if applicable.
  `29 Feb` can advance to the next leap year. Explicit years never advance.
- `midnight` follows the same next-occurrence rule as `00:00`.
  `tomorrow at midnight` is 00:00 at the **start** of tomorrow.
- Resolve the full anchor before applying arithmetic. Never push a calculated
  result forward because subtraction put it in the past.
- Days and weeks move the source calendar (local when no zone is given), preserving the clock when one exists.
  Seconds, minutes, and hours add elapsed seconds. Mixed durations apply calendar
  movement first, then elapsed time, regardless of the order of written units.
- Explicit local clocks and calendar moves into a daylight-saving gap or overlap
  return an error. DST interpretations are checked through `os.time` with both
  daylight/standard hints and round-tripped through `os.date`. This relies on the
  process runtime's local timezone rules; no timezone database is bundled.
- `now` and elapsed arithmetic retain their exact timestamp even within a
  repeated hour. Eight hours of elapsed time is always 28,800 seconds.

## Timezone queries

Put one source timezone after the date/time, before optional arithmetic:

```text
4pm EST
October 10 at 4pm EST + 2h
tomorrow at 4pm Eastern Time
now ET + 1d
2026-09-15 at 16:00 UTC+09:30
```

A trailing zone on a duration query also works: `in 8h ET` or `now + 8h EST`.
The timezone affects reference-date inference and calendar arithmetic, regardless
of its placement. `now` still denotes the exact reference instant.

| Accepted zone | Interpretation |
| --- | --- |
| `UTC`, `GMT` | Fixed UTC+00:00 |
| `UTC+09:30`, `GMT-05:00` | Fixed numeric offset; hours may have one/two digits; minutes require two |
| `UTC+0930`, `UTC-5` | Compact offset or whole-hour offset |
| `EST` / `EDT` | US fixed UTC−05:00 / UTC−04:00 |
| `CST` / `CDT` | US fixed UTC−06:00 / UTC−05:00 |
| `MST` / `MDT` | US fixed UTC−07:00 / UTC−06:00 |
| `PST` / `PDT` | US fixed UTC−08:00 / UTC−07:00 |
| `ET`, `Eastern`, `Eastern Time` | US Eastern with daylight saving |
| `CT`, `Central`, `Central Time` | US Central with daylight saving |
| `MT`, `Mountain`, `Mountain Time` | US Mountain with daylight saving |
| `PT`, `Pacific`, `Pacific Time` | US Pacific with daylight saving |
| `local` | The process's local timezone |

Full standard/daylight names also work, e.g. `Eastern Standard Time` is fixed EST.
Numeric offsets are limited to ±14:00. A signed quantity followed by a duration
unit is arithmetic: `4pm UTC + 2h` adds two hours; `4pm UTC+2` means UTC+02:00.

US abbreviations are intentionally interpreted as US timezones, and the subtitle
says so; `CST` is not China Standard Time. Unsupported names such as `IST`, `BST`,
and IANA city identifiers are rejected. Use a numeric offset for other regions.

The distinction between `EST` and `ET` is deliberate: EST always uses UTC−05:00,
even in summer. ET chooses EST or EDT for the resolved date. The subtitle shows
the choice, e.g. `From EST (US, UTC-05:00, fixed)` or
`From Eastern Time (EDT, UTC-04:00)`.

The regional profiles use the US rules in effect since 2007: spring forward on
the second Sunday of March at 02:00 standard time; fall back on the first Sunday
of November at 02:00 daylight time. Source/destination dates and reference times
before 2007 require a fixed zone. Future dates project these same rules; changes
to the law would require updating the module. MT represents the areas observing
DST; use MST for a year-round fixed Mountain offset. These are small rule profiles,
not a worldwide or historical timezone database. Rules and offsets were checked
against [NIST's local time FAQ](https://www.nist.gov/pml/time-and-frequency-division/local-time-faqs)
and [DST rules](https://www.nist.gov/pml/time-and-frequency-division/popular-links/daylight-saving-time-dst).

Reference dates belong to the source zone. `4pm EST` selects the next 16:00 in
EST, and `tomorrow at 4pm ET` uses tomorrow's date in Eastern Time. A foreign
zone needs an explicit time; `tomorrow EST` asks for one. Calendar arithmetic is
applied there before conversion: `2026-03-07 at noon ET + 1d` advances 23 elapsed
hours, while `+ 24h` advances exactly 24. Skipped/repeated regional clock times
return errors; a fixed abbreviation or UTC offset disambiguates repeated times.

For a concrete example, at reference `2026-09-15T14:00:00Z`:

| Query | UTC result | Local result in Adelaide |
| --- | --- | --- |
| `4pm EST` | September 15, 21:00 | September 16, 06:30 |
| `4pm ET` | September 15, 20:00 | September 16, 05:30 |
| `October 10 at 4pm EST + 2h` | October 10, 23:00 | October 11, 09:30 |

Convert a result to another supported zone through the formatter:

```lua
local release = assert(datetime.parse("2026-09-15 at 4pm EST"))
local local_display = assert(datetime.format(release, "time"))
local pacific = assert(datetime.format(release, "now", "PT"))
assert(pacific.copy == "2026-09-15 14:00:00 UTC-07:00")
```

## Scope limits

Not supported: sentence extraction, date ranges, slash dates, two-digit years,
vague times (`tonight`, `morning`), number words, month/year arithmetic, holidays,
business days, worldwide/historical timezone databases, bare offsets without
UTC/GMT, `to <zone>` query syntax, chained arithmetic, date differences,
recurrence, alarms, or scheduling.

## Relay integration later

Keep this module outside the directory of top-level plugin scripts, or in a
module subdirectory the plugin loader does not scan. Load it once from the
eventual datetime plugin. Pass keyword arguments to `parse`, defaulting empty
arguments to `now`; pass the selected keyword to `format`.

The returned display data maps directly to a result row:

```lua
local function query_datetime(args, keyword)
    local parsed, err = datetime.parse(args:match("^%s*$") and "now" or args)
    local display
    if parsed then display, err = datetime.format(parsed, keyword) end
    if not display then
        return {{ title = "Check Date Or Time", subtitle = err.message,
            kind = "Notice", score = 950, frecency = false }}
    end
    return {{ title = display.title, subtitle = display.subtitle,
        kind = display.kind, score = 950, frecency = false,
        label = "Copy", action = { copy = display.copy } }}
end
```

This is an integration example, not an installed adapter. Invoke it on Relay's
engine thread. It adds no host API and leaves existing keyword ownership to the
eventual plugin integration.

## Tests

From any working directory:

```text
lua path/to/standalone/datetime/test_datetime.lua
lua path/to/standalone/datetime/test_timezones.lua
```

On a process using Adelaide's local timezone, include native DST tests:

```text
lua path/to/standalone/datetime/test_datetime.lua --adelaide
```

The optional suite asserts the timezone precondition; it never changes the
machine timezone. Tests use fixed references and cover composition, complete
input consumption, leap years, ISO week-years, date precision, malformed input,
and DST gaps, overlaps, and elapsed versus calendar arithmetic. Exit status is
nonzero on failure. The separate timezone suite tests source-zone date inference,
fixed offsets, US regional transitions, conversion/copy round-trips, arithmetic,
malformed input, and source metadata independently of the machine's local zone.

Chrono was studied as a grammar reference at
[`ebac8e4`](https://github.com/wanasit/chrono/tree/ebac8e43d9d9a700961515eb150e1ebc720b621c).
This module does not bundle or depend on Chrono and does not claim compatibility
with its full grammar or inference rules.
