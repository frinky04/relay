#include "calculator.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <ctime>
#include <format>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace calculator::detail {
namespace {
using namespace std::chrono;
using Tokens = std::vector<std::string>;
const std::array<std::string, 12> months = {"January", "February", "March",     "April",   "May",      "June",
                                            "July",    "August",   "September", "October", "November", "December"};
const std::array<std::string, 7> weekdays = {"Monday", "Tuesday",  "Wednesday", "Thursday",
                                             "Friday", "Saturday", "Sunday"};
[[noreturn]] void syntax() { fail("syntax", "Use a date or time, optionally followed by + or - and a duration"); }
[[noreturn]] void rangeError() { fail("range", "Use a date between 1970 and 2999 supported by your system clock"); }
[[noreturn]] void zoneError() {
    fail("timezone", "Use a supported city, IANA timezone, or numeric offset such as UTC+09:30");
}
bool digits(std::string_view s) { return !s.empty() && s.find_first_not_of("0123456789") == s.npos; }
int64_t integer(std::string_view s) {
    if (!digits(s) || s.size() > 12) return -1;
    int64_t n = 0;
    for (char c : s) n = n * 10 + c - '0';
    return n;
}
int fractionMilliseconds(std::string_view digits) {
    // Each timestamp grammar validates one to three digits before conversion.
    int value = int(integer(digits));
    for (size_t i = digits.size(); i < 3; ++i) value *= 10;
    return value;
}
const std::string &token(const Tokens &t, size_t i) {
    static const std::string empty;
    return i < t.size() ? t[i] : empty;
}
Tokens tokenize(const std::string &text) {
    Tokens out;
    static const std::regex word(R"(^([a-z_]+/[a-z0-9_/+\-]+|[0-9]+\.[0-9]+|[0-9]+|[a-z]+|[+,:\-]))");
    for (size_t i = 0; i < text.size();) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::match_results<std::string::const_iterator> match;
        if (!std::regex_search(text.cbegin() + i, text.cend(), match, word)) syntax();
        out.push_back(match.str());
        i += match.length();
    }
    return out;
}
struct Civil {
    int y = 1970, m = 1, d = 1, h = 0, min = 0, sec = 0, ms = 0;
    bool clock = false, exact = false, upcomingYear = false, upcomingWeekday = false;
    int roll = 0; // 1 day, 7 days, 12 months
    int offset = 0;
    std::string label;
};
year_month_day ymd(const Civil &d) {
    return year{d.y} / month{static_cast<unsigned>(d.m)} / day{static_cast<unsigned>(d.d)};
}
bool valid(const Civil &d) { return ymd(d).ok(); }
int64_t dayNumber(const Civil &d) { return sys_days{ymd(d)}.time_since_epoch().count(); }
void check(const Civil &d) {
    if (d.y < 1970 || d.y > 2999) rangeError();
}
Civil fromDay(int64_t n) {
    if (n < 0 || n > sys_days{year{2999} / 12 / 31}.time_since_epoch().count()) rangeError();
    const year_month_day date{sys_days{days{n}}};
    return {int(date.year()), int(unsigned(date.month())), int(unsigned(date.day()))};
}
void moveDays(Civil &d, int64_t amount) {
    auto next = fromDay(dayNumber(d) + amount);
    d.y = next.y;
    d.m = next.m;
    d.d = next.d;
}
int weekdayNumber(const Civil &d) { return int(weekday{sys_days{ymd(d)}}.iso_encoding()); }
int64_t civilSeconds(const Civil &d) { return dayNumber(d) * 86400 + d.h * 3600 + d.min * 60 + d.sec; }
Civil fromSeconds(int64_t timestamp) {
    auto day = floor<days>(sys_seconds{seconds{timestamp}});
    auto d = fromDay(day.time_since_epoch().count());
    auto rest = timestamp - day.time_since_epoch().count() * 86400;
    d.h = int(rest / 3600);
    d.min = int(rest / 60 % 60);
    d.sec = int(rest % 60);
    d.clock = true;
    return d;
}
bool sameClock(const Civil &a, const Civil &b) {
    return a.y == b.y && a.m == b.m && a.d == b.d && a.h == b.h && a.min == b.min && a.sec == b.sec;
}
std::string offsetLabel(int offset) {
    auto n = std::abs(offset);
    return std::format("UTC{}{:02}:{:02}", offset < 0 ? "-" : "+", n / 3600, n / 60 % 60) +
           (n % 60 ? std::format(":{:02}", n % 60) : "");
}
struct Zone {
    std::string name = "local", id;
    int offset = 0;
    bool us = false;
    int standard = 0;
    std::string standardName, daylightName;
    bool local() const { return name == "local"; }
};
const time_zone *findZone(const std::string &name) {
    try {
        static const auto names = [] {
            std::map<std::string, std::string> result;
            const auto &db = get_tzdb();
            for (const auto &z : db.zones) result.emplace(lower(z.name()), z.name());
            for (const auto &z : db.links) result.emplace(lower(z.name()), z.name());
            return result;
        }();
        auto found = names.find(lower(name));
        if (found == names.end()) zoneError();
        return locate_zone(found->second);
    } catch (const std::exception &) {
        zoneError();
    }
}
const std::map<std::string, Zone> &zones() {
    static const auto result = [] {
        std::map<std::string, Zone> out;
        out["local"] = {};
        out["utc"] = {"UTC"};
        out["gmt"] = {"GMT"};
        struct US {
            const char *name, *shortName, *standard, *daylight, *id;
            int offset;
        };
        for (auto r : {US{"Eastern", "ET", "EST", "EDT", "America/New_York", -18000},
                       US{"Central", "CT", "CST", "CDT", "America/Chicago", -21600},
                       US{"Mountain", "MT", "MST", "MDT", "America/Denver", -25200},
                       US{"Pacific", "PT", "PST", "PDT", "America/Los_Angeles", -28800}}) {
            Zone region{std::string(r.name) + " Time", r.id, 0, false, r.offset, r.standard, r.daylight};
            out[lower(r.name)] = out[lower(r.shortName)] = out[lower(region.name)] = region;
            out[lower(r.standard)] = out[lower(r.name) + " standard time"] = {r.standard, "", r.offset, true};
            out[lower(r.daylight)] = out[lower(r.name) + " daylight time"] = {r.daylight, "", r.offset + 3600, true};
        }
        struct City {
            const char *name, *id, *alias = "";
        };
        for (auto c : {City{"Adelaide", "Australia/Adelaide"},
                       City{"Sydney", "Australia/Sydney"},
                       City{"Melbourne", "Australia/Melbourne"},
                       City{"Brisbane", "Australia/Brisbane"},
                       City{"Perth", "Australia/Perth"},
                       City{"Darwin", "Australia/Darwin"},
                       City{"Auckland", "Pacific/Auckland"},
                       City{"London", "Europe/London", "ldn"},
                       City{"Paris", "Europe/Paris"},
                       City{"Berlin", "Europe/Berlin"},
                       City{"Tokyo", "Asia/Tokyo"},
                       City{"Singapore", "Asia/Singapore"},
                       City{"Hong Kong", "Asia/Hong_Kong"},
                       City{"Shanghai", "Asia/Shanghai"},
                       City{"Kolkata", "Asia/Kolkata"},
                       City{"Mumbai", "Asia/Kolkata"},
                       City{"Kathmandu", "Asia/Kathmandu"},
                       City{"Dubai", "Asia/Dubai"},
                       City{"New York", "America/New_York", "nyc"},
                       City{"Chicago", "America/Chicago"},
                       City{"Denver", "America/Denver"},
                       City{"Los Angeles", "America/Los_Angeles", "la"},
                       City{"San Francisco", "America/Los_Angeles", "sf"},
                       City{"Toronto", "America/Toronto"},
                       City{"Vancouver", "America/Vancouver"},
                       City{"Sao Paulo", "America/Sao_Paulo"},
                       City{"Johannesburg", "Africa/Johannesburg"},
                       City{"Cairo", "Africa/Cairo"}}) {
            out[lower(c.name)] = {c.name, c.id};
            if (*c.alias) out[c.alias] = {c.name, c.id};
        }
        return out;
    }();
    return result;
}
Civil zoneDate(int64_t timestamp, const Zone &zone) {
    if (zone.local()) {
        __time64_t t = timestamp;
        std::tm tm{};
        if (_localtime64_s(&tm, &t)) rangeError();
        Civil d{tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
        check(d);
        d.clock = true;
        d.offset = int(civilSeconds(d) - timestamp);
        d.label = "Local " + offsetLabel(d.offset);
        return d;
    }
    int offset = zone.offset;
    std::string abbreviation;
    if (!zone.id.empty()) {
        try {
            auto info = findZone(zone.id)->get_info(sys_seconds{seconds{timestamp}});
            offset = int(info.offset.count());
            abbreviation = info.abbrev;
        } catch (const std::exception &) {
            zoneError();
        }
        if (!zone.standardName.empty()) abbreviation = offset == zone.standard ? zone.standardName : zone.daylightName;
    }
    auto d = fromSeconds(timestamp + offset);
    d.offset = offset;
    if (!zone.id.empty()) d.label = zone.name + " (" + abbreviation + ", " + offsetLabel(offset) + ")";
    else if (zone.us) d.label = zone.name + " (US, " + offsetLabel(offset) + ", fixed)";
    else if (zone.name == "UTC" || zone.name == offsetLabel(offset)) d.label = zone.name;
    else d.label = zone.name + " (" + offsetLabel(offset) + ")";
    return d;
}
int64_t zoneTimestamp(const Civil &d, const Zone &zone, std::optional<int64_t> notBefore = {}) {
    std::set<int64_t> found;
    bool normalized = false;
    auto accept = [&](int64_t t) {
        try {
            auto actual = zoneDate(t, zone);
            normalized = true;
            if (sameClock(d, actual)) found.insert(t);
        } catch (const Error &e) {
            if (e.code != "range") throw;
        }
    };
    if (zone.local()) {
        for (int hint : {-1, 0, 1}) {
            std::tm tm{};
            tm.tm_year = d.y - 1900;
            tm.tm_mon = d.m - 1;
            tm.tm_mday = d.d;
            tm.tm_hour = d.h;
            tm.tm_min = d.min;
            tm.tm_sec = d.sec;
            tm.tm_isdst = hint;
            auto t = _mktime64(&tm);
            if (t != -1) accept(t);
        }
    } else if (zone.id.empty()) accept(civilSeconds(d) - zone.offset);
    else {
        try {
            auto info = findZone(zone.id)->get_info(local_seconds{seconds{civilSeconds(d)}});
            accept(civilSeconds(d) - info.first.offset.count());
            if (info.result != local_info::unique) accept(civilSeconds(d) - info.second.offset.count());
        } catch (const std::exception &) {
            zoneError();
        }
    }
    if (notBefore && !found.empty() && *found.rbegin() < *notBefore) fail("past", "Choose an upcoming date or time");
    if (found.size() > 1)
        fail("ambiguous_time",
             "This time occurs twice when clocks change; specify a fixed UTC offset or choose another time");
    if (found.empty()) {
        if (!normalized) rangeError();
        fail("nonexistent_time", "This time is skipped when clocks change; choose a time outside the skipped interval");
    }
    return *found.begin();
}
struct CalendarPeriod {
    int64_t months = 0, days = 0, seconds = 0;
    bool hasTime = false; // Even 0s requires an instant rather than a date.
};
enum class PeriodField { Seconds, Days, Months };
struct PeriodUnit {
    PeriodField field;
    int scale;
};
const std::map<std::string, PeriodUnit> &periodUnits() {
    static const auto result = [] {
        std::map<std::string, PeriodUnit> out;
        auto add = [&](std::string words, PeriodField field, int scale) {
            std::istringstream in(words);
            for (std::string word; in >> word;) out[word] = {field, scale};
        };
        add("s sec secs second seconds", PeriodField::Seconds, 1);
        add("m min mins minute minutes", PeriodField::Seconds, 60);
        add("h hr hrs hour hours", PeriodField::Seconds, 3600);
        add("d day days", PeriodField::Days, 1);
        add("w wk wks week weeks", PeriodField::Days, 7);
        add("mo month months", PeriodField::Months, 1);
        add("y yr yrs year years", PeriodField::Months, 12);
        return out;
    }();
    return result;
}
std::optional<Zone> readZone(const Tokens &t, size_t &i) {
    if (token(t, i).find('/') != std::string::npos) {
        auto z = findZone(t[i++]);
        return Zone{std::string(z->name()), std::string(z->name())};
    }
    std::optional<Zone> zone;
    for (size_t count = 3; count > 0; --count) {
        std::string key;
        for (size_t j = 0; j < count; ++j) {
            if (j) key += ' ';
            key += token(t, i + j);
        }
        auto found = zones().find(key);
        if (found != zones().end()) {
            zone = found->second;
            i += count;
            break;
        }
    }
    if (!zone) return {};
    if ((zone->name == "UTC" || zone->name == "GMT") && (token(t, i) == "+" || token(t, i) == "-") &&
        !periodUnits().contains(token(t, i + 2))) {
        int sign = t[i++] == "-" ? -1 : 1;
        auto number = token(t, i++);
        auto hour = integer(number);
        int64_t minute = 0, second = 0;
        if (hour < 0) zoneError();
        if (number.size() == 4) {
            minute = hour % 100;
            hour /= 100;
        } else if (number.size() > 2) zoneError();
        if (token(t, i) == ":") {
            ++i;
            auto part = token(t, i++);
            minute = integer(part);
            if (number.size() > 2 || minute < 0 || part.size() != 2) zoneError();
            if (token(t, i) == ":") {
                ++i;
                part = token(t, i++);
                second = integer(part);
                if (second < 0 || part.size() != 2) zoneError();
            }
        }
        if (hour > 14 || minute > 59 || second > 59 || (hour == 14 && (minute || second))) zoneError();
        auto offset = sign * int(hour * 3600 + minute * 60 + second);
        zone = Zone{offsetLabel(offset), "", offset};
    }
    return zone;
}
Zone destination(const std::string &text) {
    if (text.size() > 64) zoneError();
    auto t = tokenize(text);
    size_t i = 0;
    auto z = readZone(t, i);
    if (!z || i != t.size()) zoneError();
    return *z;
}
Zone extractZone(Tokens &tokens) {
    Tokens output;
    Zone selected;
    bool found = false;
    for (size_t i = 0; i < tokens.size();) {
        size_t after = i;
        auto zone = readZone(tokens, after);
        if (zone) {
            if (found || i == 0 ||
                (after < tokens.size() && token(tokens, after) != "+" && token(tokens, after) != "-" &&
                 token(tokens, after) != "in"))
                fail("timezone", "Put one timezone after the date/time, optionally followed by duration arithmetic");
            selected = *zone;
            found = true;
            i = after;
        } else output.push_back(tokens[i++]);
    }
    tokens = std::move(output);
    return selected;
}
CalendarPeriod period(const Tokens &t, size_t &i) {
    CalendarPeriod out;
    std::set<std::pair<PeriodField, int>> seen;
    while (i < t.size()) {
        auto u = periodUnits().find(token(t, i + 1));
        if (u == periodUnits().end()) break;
        const auto &spelling = token(t, i);
        static const std::regex numeric(R"([0-9]+(\.[0-9]+)?)");
        if (!std::regex_match(spelling, numeric)) break;
        double n = 0;
        auto parsed = std::from_chars(spelling.data(), spelling.data() + spelling.size(), n);
        if (parsed.ec != std::errc{}) fail("duration", "Use a smaller duration");
        auto [field, scale] = u->second;
        if (!std::isfinite(n) || n > 4e10) fail("duration", "Use a smaller duration");
        if (field != PeriodField::Seconds && std::floor(n) != n)
            fail("duration", "Use whole days, weeks, months and years");
        if (!seen.emplace(field, scale).second) fail("duration", "Use each duration unit once");
        double value = n * scale;
        if (std::abs(value - std::round(value)) > 1e-6)
            fail("duration", "Use a duration that resolves to whole seconds");
        auto amount = static_cast<int64_t>(std::round(value));
        if (field == PeriodField::Seconds) {
            out.seconds += amount;
            out.hasTime = true;
        } else if (field == PeriodField::Days) out.days += amount;
        else out.months += amount;
        i += 2;
        if (token(t, i) == "and" || token(t, i) == ",") {
            ++i;
            if (!std::regex_match(token(t, i), numeric) || !periodUnits().contains(token(t, i + 1)))
                fail("duration", "Add a duration after the separator, such as 1h and 30m");
        }
    }
    if (seen.empty()) fail("duration", "Add a duration such as 8h or 7h30m");
    return out;
}
int nameIndex(std::string_view word, bool month) {
    if (month && word == "sept") return 9;
    if (!month && word == "tues") return 2;
    if (!month && word == "thurs") return 4;
    const size_t count = month ? months.size() : weekdays.size();
    for (size_t i = 0; i < count; ++i) {
        const auto name = lower(month ? months[i] : weekdays[i]);
        if (word == name || word == name.substr(0, 3)) return int(i + 1);
    }
    return 0;
}
void ordinalSuffix(const Tokens &t, size_t &i, int day) {
    const auto &suffix = token(t, i);
    if (suffix != "st" && suffix != "nd" && suffix != "rd" && suffix != "th") return;
    std::string expected = "th";
    if (day % 100 < 11 || day % 100 > 13) {
        if (day % 10 == 1) expected = "st";
        if (day % 10 == 2) expected = "nd";
        if (day % 10 == 3) expected = "rd";
    }
    if (suffix != expected) fail("date", "Correct the ordinal suffix or use a day number such as 17");
    ++i;
}
void clock(const Tokens &t, size_t &i, Civil &d) {
    auto word = token(t, i++);
    d.clock = true;
    if (word == "noon" || word == "midnight") {
        d.h = word == "noon" ? 12 : 0;
        return;
    }
    auto hour = integer(word);
    if (hour < 0 || word.size() > 2) fail("time", "Use a clock time such as 7am or 23:30");
    bool colon = token(t, i) == ":";
    int64_t minute = 0, second = 0;
    if (colon) {
        ++i;
        auto part = token(t, i++);
        minute = integer(part);
        if (minute < 0 || part.size() != 2) fail("time", "Use two digits for minutes, such as 07:30");
        if (token(t, i) == ":") {
            ++i;
            part = token(t, i++);
            auto dot = part.find('.');
            auto whole = part.substr(0, dot);
            second = integer(whole);
            if (second < 0 || whole.size() != 2) fail("time", "Use two digits for seconds, such as 07:30:00");
            if (dot != part.npos) {
                auto fraction = part.substr(dot + 1);
                if (!digits(fraction) || fraction.size() > 3)
                    fail("time", "Use at most three fractional second digits");
                d.ms = fractionMilliseconds(fraction);
            }
        }
    }
    auto meridiem = token(t, i);
    if (meridiem == "am" || meridiem == "pm") {
        if (hour < 1 || hour > 12) fail("time", "Use an hour from 1 to 12 with am or pm");
        hour = hour % 12 + (meridiem == "pm" ? 12 : 0);
        ++i;
    } else if (!colon) fail("time", "Add am or pm, or use a 24-hour time such as 07:00");
    if (hour > 23 || minute > 59 || second > 59) fail("time", "Use hours 00 to 23 and minutes and seconds 00 to 59");
    d.h = int(hour);
    d.min = int(minute);
    d.sec = int(second);
}
std::optional<Civil> dateAnchor(const Tokens &t, const Civil &ref, size_t &i) {
    auto word = token(t, 0);
    Civil d{ref.y, ref.m, ref.d};
    if (word == "today" || word == "tomorrow" || word == "yesterday") {
        moveDays(d, word == "tomorrow" ? 1 : word == "yesterday" ? -1 : 0);
        i = 1;
        return d;
    }
    std::string modifier;
    int weekday = nameIndex(word, false);
    i = 1;
    if (word == "this" || word == "next" || word == "last") {
        modifier = word;
        weekday = nameIndex(token(t, 1), false);
        i = 2;
        if (!weekday) syntax();
    }
    if (weekday) {
        int current = weekdayNumber(ref), offset = (weekday - current + 7) % 7;
        if (modifier == "next" && offset == 0) offset = 7;
        if (modifier == "last") offset = -((current - weekday + 6) % 7 + 1);
        if (modifier == "this") offset = weekday - current;
        moveDays(d, offset);
        d.upcomingWeekday = modifier.empty();
        return d;
    }
    bool explicitYear = false;
    if (integer(word) >= 0 && token(t, 1) == "-") {
        if (word.size() != 4 || token(t, 2).size() != 2 || integer(token(t, 2)) < 0 || token(t, 3) != "-" ||
            token(t, 4).size() != 2 || integer(token(t, 4)) < 0)
            fail("date", "Use an ISO date such as 2027-08-17");
        d.y = int(integer(word));
        d.m = int(integer(t[2]));
        d.d = int(integer(t[4]));
        i = 5;
        explicitYear = true;
    } else if (auto month = nameIndex(word, true)) {
        d.m = month;
        auto n = integer(token(t, 1));
        if (n < 0 || token(t, 1).size() > 2) fail("date", "Add a day, such as August 17");
        d.d = int(n);
        i = 2;
        ordinalSuffix(t, i, d.d);
    } else if (integer(word) >= 0 && word.size() <= 2) {
        d.d = int(integer(word));
        i = 1;
        ordinalSuffix(t, i, d.d);
        d.m = nameIndex(token(t, i), true);
        if (!d.m) return {};
        ++i;
    } else return {};
    if (!explicitYear) {
        bool comma = token(t, i) == ",";
        if (comma) ++i;
        if (token(t, i).size() == 4 && integer(token(t, i)) >= 0) {
            d.y = int(integer(t[i++]));
            explicitYear = true;
        } else if (comma) fail("date", "Add a four-digit year after the comma");
    }
    if (explicitYear) check(d);
    auto test = d;
    if (!explicitYear) test.y = 2000;
    if (!valid(test)) fail("date", "Use a valid day for the selected month and year");
    d.upcomingYear = !explicitYear;
    return d;
}
Civil anchor(const Tokens &t, const Civil &ref, size_t &i) {
    if (token(t, 0) == "now") {
        auto d = ref;
        d.exact = true;
        i = 1;
        return d;
    }
    auto date = dateAnchor(t, ref, i);
    bool timeOnly = !date;
    Civil d = date.value_or(Civil{ref.y, ref.m, ref.d});
    if (timeOnly) i = 0;
    bool at = token(t, i) == "at";
    if (at) ++i;
    bool hasTime = timeOnly || at || (i < t.size() && token(t, i) != "+" && token(t, i) != "-" && token(t, i) != "in");
    if (hasTime) clock(t, i, d);
    if (d.upcomingYear) {
        d.y = ref.y;
        while (!valid(d) || dayNumber(d) < dayNumber(ref)) {
            ++d.y;
            check(d);
        }
    }
    if (hasTime && dayNumber(d) == dayNumber(ref)) {
        if (timeOnly) d.roll = 1;
        else if (d.upcomingWeekday) d.roll = 7;
        else if (d.upcomingYear) d.roll = 12;
    }
    return d;
}
struct Parsed {
    Civil date;
    std::optional<int64_t> timestamp;
    Zone source;
};
Parsed parse(std::string text, int64_t reference) {
    if (text.size() > 256) fail("input", "Use a date/time expression of at most 256 bytes");
    int milliseconds = 0;
    static const std::regex unixPattern(R"(^unix\s+([0-9]+\.?[0-9]*)\s*([a-z]*)$)");
    static const std::regex isoPattern(R"(^([0-9]{4}-[0-9]{2}-[0-9]{2})t([0-9]{2}:[0-9]{2}:[0-9]{2})(.*)$)");
    std::smatch match;
    if (std::regex_match(text, match, unixPattern)) {
        auto value = match[1].str(), unit = match[2].str();
        if (!unit.empty() && unit != "s" && unit != "ms")
            fail("timestamp", "Use unix <seconds> or unix <milliseconds> ms");
        auto dot = value.find('.');
        std::string fraction = dot == value.npos ? "" : value.substr(dot + 1);
        if (dot != value.npos && (unit == "ms" || fraction.empty() || fraction.size() > 3))
            fail("timestamp", "Use whole Unix milliseconds or seconds with at most three fractional digits");
        auto whole = value.substr(0, dot);
        if (!digits(whole) || whole.size() > 14) rangeError();
        int64_t n = 0;
        for (char c : whole) n = n * 10 + c - '0';
        if (unit == "ms") {
            milliseconds = int(n % 1000);
            n /= 1000;
        } else if (!fraction.empty()) {
            milliseconds = fractionMilliseconds(fraction);
        }
        if (n > 32503680000LL) rangeError();
        auto d = zoneDate(n, {});
        d.ms = milliseconds;
        return {d, n, {}};
    }
    bool iso = std::regex_match(text, match, isoPattern);
    if (iso) {
        auto day = match[1].str(), time = match[2].str(), tail = match[3].str();
        if (tail.starts_with('.')) {
            size_t end = tail.find_first_not_of("0123456789", 1);
            if (end == tail.npos) end = tail.size();
            auto fraction = tail.substr(1, end - 1);
            if (fraction.empty() || fraction.size() > 3) fail("timestamp", "Use one to three fractional second digits");
            milliseconds = fractionMilliseconds(fraction);
            tail.erase(0, end);
        }
        if (tail.starts_with('z')) tail = " utc" + tail.substr(1);
        else if (tail.size() >= 6 && (tail[0] == '+' || tail[0] == '-') && tail[3] == ':') tail = " utc" + tail;
        else fail("timestamp", "End the ISO timestamp with Z or an offset such as +09:30");
        text = day + " at " + time + tail;
    }
    auto t = tokenize(text);
    if (t.empty()) fail("input", "Enter now, tomorrow, or an expression such as now + 8h");
    auto source = extractZone(t);
    auto ref = zoneDate(reference, source);
    Civil d;
    std::optional<CalendarPeriod> duration;
    int sign = 1;
    size_t i = 0;
    if (token(t, 0) == "in" || (periodUnits().contains(token(t, 1)) && !token(t, 0).empty() &&
                                std::isdigit(static_cast<unsigned char>(t[0][0])))) {
        bool prefix = token(t, 0) == "in";
        i = prefix ? 1 : 0;
        duration = period(t, i);
        if (!prefix) {
            if (token(t, i) == "ago") {
                sign = -1;
                ++i;
            } else if (token(t, i) == "from" && token(t, i + 1) == "now") i += 2;
            else fail("duration", "Use in 8h, 8h from now, or 8h ago");
        }
        d = {ref.y, ref.m, ref.d};
        if (duration->hasTime) {
            d = ref;
            d.exact = true;
        }
    } else {
        d = anchor(t, ref, i);
        if (token(t, i) == "+" || token(t, i) == "-" || token(t, i) == "in") {
            sign = t[i++] == "-" ? -1 : 1;
            duration = period(t, i);
        }
    }
    if (i != t.size()) syntax();
    if (!iso) milliseconds = d.ms;
    if (!source.local() && !d.clock)
        fail("time_required", "Add a clock time to convert between timezones, such as tomorrow at 4pm EST");
    if (duration && duration->hasTime && !d.clock)
        fail("time_required", "Add a starting time, such as tomorrow at 7am + 8h");
    std::optional<int64_t> timestamp;
    if (d.clock) {
        if (d.exact) timestamp = reference;
        else {
            try {
                timestamp = zoneTimestamp(d, source, d.roll ? std::optional<int64_t>{reference} : std::nullopt);
            } catch (const Error &e) {
                if (!d.roll ||
                    (e.code != "past" && !(e.code == "nonexistent_time" &&
                                           d.h * 3600 + d.min * 60 + d.sec < ref.h * 3600 + ref.min * 60 + ref.sec)))
                    throw;
                if (d.roll == 12) {
                    do {
                        ++d.y;
                        check(d);
                    } while (!valid(d));
                } else moveDays(d, d.roll);
                timestamp = zoneTimestamp(d, source);
            }
        }
    }
    if (duration) {
        if (duration->months) {
            int64_t month = d.y * 12 + d.m - 1 + sign * duration->months;
            if (month < 1970 * 12 || month >= 3000 * 12) rangeError();
            d.y = int(month / 12);
            d.m = int(month % 12 + 1);
            auto last = year_month_day_last{year{d.y}, month_day_last{std::chrono::month{unsigned(d.m)}}};
            d.d = std::min(d.d, int(unsigned(last.day())));
            if (d.clock) timestamp = zoneTimestamp(d, source);
        }
        if (duration->days) {
            moveDays(d, sign * duration->days);
            if (d.clock) timestamp = zoneTimestamp(d, source);
        }
        if (d.clock && duration->seconds) {
            *timestamp += sign * duration->seconds;
            d = zoneDate(*timestamp, source);
        }
    }
    if (timestamp) d = zoneDate(*timestamp, {});
    d.ms = milliseconds;
    return {d, timestamp, source};
}
std::string isoDate(const Civil &d) { return std::format("{:04}-{:02}-{:02}", d.y, d.m, d.d); }
std::string fraction(const Civil &d) { return d.ms ? std::format(".{:03}", d.ms) : ""; }
std::string isoTime(const Civil &d) {
    return isoDate(d) + std::format(" {:02}:{:02}:{:02}", d.h, d.min, d.sec) + fraction(d);
}
std::string longDate(const Civil &d, bool abbreviated = false) {
    auto weekday = weekdays[weekdayNumber(d) - 1], month = months[d.m - 1];
    if (abbreviated) {
        weekday.resize(3);
        month.resize(3);
    }
    return std::format("{}, {} {} {}", weekday, d.d, month, d.y);
}
std::string isoWeek(const Civil &d) {
    // A week-year may cross the supported input-year boundary.
    auto thursday = sys_days{ymd(d)} + days{4 - weekdayNumber(d)};
    auto year = year_month_day{thursday}.year();
    auto start = sys_days{year / January / 1};
    return std::format("{:04}-W{:02}", int(year), (thursday - start).count() / 7 + 1);
}
std::string timespan(int64_t seconds) {
    auto n = std::abs(seconds);
    std::string text = seconds < 0 ? "-" : "";
    bool any = false;
    for (auto [scale, label] : {std::pair{86400, "day"}, {3600, "hour"}, {60, "minute"}, {1, "second"}}) {
        auto count = n / scale;
        n %= scale;
        if (!count) continue;
        if (any) text += ' ';
        any = true;
        text += std::to_string(count) + " " + label + (count == 1 ? "" : "s");
    }
    return any ? text : "0 seconds";
}
Result durationResult(int64_t seconds, bool calendar, const std::string &expression, bool plus = false) {
    Result result;
    result.status = Status::Success;
    result.recognize = true;
    result.value = Duration{seconds, calendar};
    auto count = seconds / 86400;
    auto title = calendar ? std::to_string(count) + " day" + (std::abs(count) == 1 ? "" : "s") : timespan(seconds);
    if (plus && seconds > 0) title = "+" + title;
    result.outputs.push_back({"Copy", title, expression, title});
    return result;
}
Result formatted(const Parsed &result, const Zone &dest, int64_t reference) {
    Result out;
    out.status = Status::Success;
    out.recognize = true;
    auto d = result.date;
    if (!result.timestamp) {
        if (!dest.local()) fail("time_required", "Add a clock time before converting to another timezone");
        out.value = Date{ymd(d)};
        auto date = isoDate(d), full = longDate(d), week = isoWeek(d);
        out.outputs = {{"Copy", date, longDate(d, true), date},
                       {"Copy Full Date", "Full date", full, full},
                       {"Copy ISO Week", "ISO week", week, week}};
        return out;
    }
    auto timestamp = *result.timestamp;
    d = zoneDate(timestamp, dest);
    d.ms = result.date.ms;
    out.value = Instant{sys_time<milliseconds>{seconds{timestamp} + milliseconds{d.ms}}};
    auto ref = zoneDate(reference, dest);
    auto delta = dayNumber(d) - dayNumber(ref);
    std::string relative = delta == 0 ? "today" : delta == 1 ? "tomorrow" : delta == -1 ? "yesterday" : isoDate(d);
    auto title = std::format("{:02}:{:02}", d.h, d.min);
    if (d.sec || d.ms) title += std::format(":{:02}", d.sec);
    title += fraction(d) + " " + relative;
    auto subtitle = longDate(d, true) + " · " + d.label;
    if (!result.source.local() && result.source.name != dest.name)
        subtitle += " · From " + zoneDate(timestamp, result.source).label;
    auto utc = zoneDate(timestamp, Zone{"UTC"});
    utc.ms = d.ms;
    auto iso = isoTime(utc);
    iso[10] = 'T';
    iso += 'Z';
    auto unix = std::to_string(timestamp) + fraction(d);
    auto discord = "<t:" + std::to_string(timestamp) + ":f>";
    auto discordRelative = "<t:" + std::to_string(timestamp) + ":R>";
    out.outputs = {{"Copy", title, subtitle, isoTime(d) + " " + offsetLabel(d.offset)},
                   {"Copy Discord", "Discord timestamp", discord, discord},
                   {"Copy Discord Relative", "Discord relative", discordRelative, discordRelative},
                   {"Copy ISO", "ISO 8601 · UTC", iso, iso},
                   {"Copy Unix", "Unix seconds", unix, unix}};
    return out;
}
} // namespace

bool temporalForm(const std::string &text) {
    static const std::regex form(
        R"(^([0-9]{4}-[0-9]{1,2}-[0-9]{1,2}|now\b|today\b|tomorrow\b|yesterday\b|this\b|next\b|last\b|in\b|unix\b|time\b|diff\b|days\s+(until|since)\b|noon\b|midnight\b|[0-9]{1,2}\s*(am\b|pm\b|:)))");
    if (std::regex_search(text, form)) return true;
    auto first = text.substr(0, text.find_first_not_of("abcdefghijklmnopqrstuvwxyz"));
    if (nameIndex(first, true) || nameIndex(first, false)) return true;
    if (first == "now" || first == "today" || first == "tomorrow" || first == "yesterday" || first == "in") return true;
    static const std::regex namedDate(R"(^[0-9]{1,2}(st|nd|rd|th)?\s*([a-z]+)\b)");
    std::smatch match;
    if (std::regex_search(text, match, namedDate) && nameIndex(match[2].str(), true)) return true;
    return text.find(" ago ") != text.npos || text.find(" from now ") != text.npos || text.ends_with(" ago") ||
           text.ends_with(" from now") || text.ends_with(" timespan");
}
Result datetime(std::string text, int64_t reference) {
    if (reference < 0 || reference >= 32503680000LL) rangeError();
    if (text.starts_with("days until ") || text.starts_with("days since ")) {
        auto value = parse(text.substr(11), reference);
        if (value.timestamp) fail("date", "Use a date without a clock for calendar day differences");
        auto days =
            (dayNumber(value.date) - dayNumber(zoneDate(reference, {}))) * (text.starts_with("days since") ? -1 : 1);
        return durationResult(days * 86400, true, text);
    }
    if (auto minus = text.find(" - "); minus != text.npos) {
        std::optional<Parsed> a, b;
        try {
            a = parse(text.substr(0, minus), reference);
            b = parse(text.substr(minus + 3), reference);
        } catch (const Error &) {
            b.reset();
        }
        if (a && b) {
            if (bool(a->timestamp) != bool(b->timestamp))
                fail("date", "Subtract two dates or two times; add a clock to both for elapsed time");
            if (a->timestamp) {
                if (a->date.ms || b->date.ms) fail("duration", "Use whole-second timestamps for elapsed differences");
                return durationResult(*a->timestamp - *b->timestamp, false, text);
            }
            return durationResult((dayNumber(a->date) - dayNumber(b->date)) * 86400, true, text);
        }
    }
    if (text.ends_with(" to timespan") || text.ends_with(" in timespan")) {
        auto t = tokenize(text.substr(0, text.size() - 12));
        size_t i = 0;
        auto value = period(t, i);
        if (i != t.size() || value.months) fail("duration", "Use seconds through weeks for an elapsed timespan");
        return durationResult(value.days * 86400 + value.seconds, false, text);
    }
    if (text.starts_with("time diff ") || text.starts_with("diff ")) {
        auto name = text.substr(text.starts_with("time diff ") ? 10 : 5);
        auto zone = destination(name);
        auto there = zoneDate(reference, zone), here = zoneDate(reference, {});
        return durationResult(civilSeconds(there) - civilSeconds(here), false, name + " versus local at this instant",
                              true);
    }
    if (text == "time") text = "now";
    else if (text.starts_with("time in ")) {
        text.erase(0, 5);
        try {
            destination(text.substr(3));
            text = "now to " + text.substr(3);
        } catch (const Error &) {
        }
    }
    std::optional<Zone> dest;
    auto split = text.find(" to ");
    if (split != text.npos) {
        dest = destination(text.substr(split + 4));
        text.resize(split);
    } else {
        // A duration's `in` remains arithmetic. Only a complete zone suffix converts.
        for (auto pos = text.rfind(" in "); pos != text.npos; pos = pos ? text.rfind(" in ", pos - 1) : text.npos) {
            try {
                dest = destination(text.substr(pos + 4));
                text.resize(pos);
                break;
            } catch (const Error &) {
            }
        }
        if (text.ends_with(" to")) fail("timezone", "Add a destination after to, such as UTC or PT");
    }
    auto value = parse(text, reference);
    if (dest && !value.timestamp) fail("time_required", "Add a clock time before converting to another timezone");
    return formatted(value, dest.value_or(Zone{}), reference);
}
} // namespace calculator::detail
