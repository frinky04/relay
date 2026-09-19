#include "solver_internal.h"
#include <algorithm>
#include <array>
#include <ctime>
#include <format>
#include <map>
#include <regex>
#include <set>

namespace solver::detail {
namespace {
using namespace std::chrono;
struct Tokens {
    std::vector<PeriodToken> values;
    Span end;
    size_t size() const { return values.size(); }
    bool empty() const { return values.empty(); }
    const std::string &operator[](size_t i) const { return values[i].text; }
    void push(std::string word, Span span) { values.push_back({std::move(word), span}); }
    Span at(size_t i) const { return i < size() ? values[i].span : end; }
};
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
    for (char c : s)
        n = n * 10 + c - '0';
    return n;
}
int fractionMilliseconds(std::string_view digits) {
    // Each timestamp grammar validates one to three digits before conversion.
    int value = int(integer(digits));
    for (size_t i = digits.size(); i < 3; ++i)
        value *= 10;
    return value;
}
const std::string &token(const Tokens &t, size_t i) {
    static const std::string empty;
    return i < t.size() ? t[i] : empty;
}
Tokens tokenize(const Source &source) {
    const auto &text = source.text;
    Tokens out;
    out.end = source.span(text.size());
    static const std::regex word(R"(^([a-z_]+/[a-z0-9_/+\-]+|[0-9]+\.[0-9]+|[0-9]+|[a-z]+|[+,:\-]))");
    for (size_t i = 0; i < text.size();) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }
        std::match_results<std::string::const_iterator> match;
        if (!std::regex_search(text.cbegin() + i, text.cend(), match, word))
            failAt("syntax", "Remove this character; use a date, time or duration", source.span(i));
        out.push(match.str(), source.span(i, match.length()));
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
            for (const auto &z : db.zones)
                result.emplace(lower(z.name()), z.name());
            for (const auto &z : db.links)
                result.emplace(lower(z.name()), z.name());
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
        d.label = "Local " + formatOffset(d.offset);
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
    if (!zone.id.empty())
        d.label = zone.name + " (" + abbreviation + ", " + formatOffset(offset) + ")";
    else if (zone.us)
        d.label = zone.name + " (US, " + formatOffset(offset) + ", fixed)";
    else if (zone.name == "UTC" || zone.name == formatOffset(offset))
        d.label = zone.name;
    else
        d.label = zone.name + " (" + formatOffset(offset) + ")";
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
        } catch (const Diagnostic &e) {
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
    } else if (zone.id.empty())
        accept(civilSeconds(d) - zone.offset);
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
std::optional<Zone> readZone(const Tokens &t, size_t &i) try {
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
        !periodUnit(token(t, i + 2))) {
        int sign = t[i++] == "-" ? -1 : 1;
        auto number = token(t, i++);
        auto hour = integer(number);
        int64_t minute = 0, second = 0;
        if (hour < 0) zoneError();
        if (number.size() == 4) {
            minute = hour % 100;
            hour /= 100;
        } else if (number.size() > 2)
            zoneError();
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
        zone = Zone{formatOffset(offset), "", offset};
    }
    return zone;
} catch (Diagnostic error) {
    if (!error.span) error.span = t.at(i ? i - 1 : 0);
    throw error;
}

Zone destination(const Source &source) {
    const auto &text = source.text;
    if (text.size() > 64) zoneError();
    auto t = tokenize(source);
    size_t i = 0;
    auto z = readZone(t, i);
    if (!z || i != t.size())
        failAt("timezone", "Use a supported city, IANA timezone, or numeric offset such as UTC+09:30", t.at(i));
    return *z;
}
Zone extractZone(Tokens &tokens) {
    Tokens output;
    output.end = tokens.end;
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
        } else {
            output.values.push_back(std::move(tokens.values[i]));
            ++i;
        }
    }
    tokens = std::move(output);
    return selected;
}
CalendarPeriod period(const Tokens &t, size_t &i) { return calendarPeriod(readPeriod(t.values, i)); }
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
void clock(const Tokens &t, size_t &i, Civil &d) try {
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
    } else if (!colon)
        fail("time", "Add am or pm, or use a 24-hour time such as 07:00");
    if (hour > 23 || minute > 59 || second > 59) fail("time", "Use hours 00 to 23 and minutes and seconds 00 to 59");
    d.h = int(hour);
    d.min = int(minute);
    d.sec = int(second);
} catch (Diagnostic error) {
    if (!error.span) error.span = t.at(i ? i - 1 : 0);
    throw error;
}
std::optional<Civil> dateAnchor(const Tokens &t, const Civil &ref, size_t &i) try {
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
    } else
        return {};
    if (!explicitYear) {
        bool comma = token(t, i) == ",";
        if (comma) ++i;
        if (token(t, i).size() == 4 && integer(token(t, i)) >= 0) {
            d.y = int(integer(t[i++]));
            explicitYear = true;
        } else if (comma)
            fail("date", "Add a four-digit year after the comma");
    }
    if (explicitYear) check(d);
    auto test = d;
    if (!explicitYear) test.y = 2000;
    if (!valid(test)) fail("date", "Use a valid day for the selected month and year");
    d.upcomingYear = !explicitYear;
    return d;
} catch (Diagnostic error) {
    if (!error.span) error.span = t.at(i ? i - 1 : 0);
    throw error;
}

Civil anchor(const Tokens &t, const Civil &ref, size_t &i) try {
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
        if (timeOnly)
            d.roll = 1;
        else if (d.upcomingWeekday)
            d.roll = 7;
        else if (d.upcomingYear)
            d.roll = 12;
    }
    return d;
} catch (Diagnostic error) {
    if (!error.span) error.span = t.at(i ? i - 1 : 0);
    throw error;
}

struct Parsed {
    Civil date;
    std::optional<int64_t> timestamp;
    Zone source;
};
Parsed parse(Source source, int64_t reference) {
    auto &text = source.text;
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
        for (char c : whole)
            n = n * 10 + c - '0';
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
        auto tailSource = source.slice(size_t(match.position(3)));
        if (tailSource.text.starts_with('.')) {
            auto end = tailSource.text.find_first_not_of("0123456789", 1);
            if (end == tailSource.text.npos) end = tailSource.text.size();
            auto fraction = tailSource.text.substr(1, end - 1);
            if (fraction.empty() || fraction.size() > 3) fail("timestamp", "Use one to three fractional second digits");
            milliseconds = fractionMilliseconds(fraction);
            tailSource = tailSource.slice(end);
        }
        if (tailSource.text.starts_with('z')) {
            auto origin = tailSource.span(0);
            tailSource.replace(0, 1, Source(""));
            tailSource.insert(0, " utc", origin);
        } else if (tailSource.text.size() >= 6 && (tailSource.text[0] == '+' || tailSource.text[0] == '-') &&
                   tailSource.text[3] == ':') {
            tailSource.insert(0, " utc", tailSource.span(0));
        } else {
            fail("timestamp", "End the ISO timestamp with Z or an offset such as +09:30");
        }
        auto rewritten = source.slice(0, 10);
        rewritten.insert(rewritten.text.size(), " at ", source.span(10));
        rewritten.replace(rewritten.text.size(), 0, source.slice(11, size_t(match.length(2))));
        rewritten.replace(rewritten.text.size(), 0, tailSource);
        rewritten.end = source.end;
        source = std::move(rewritten);
    }
    auto t = tokenize(source);
    if (t.empty()) fail("input", "Enter now, tomorrow, or an expression such as now + 8h");
    auto zone = extractZone(t);
    auto ref = zoneDate(reference, zone);
    Civil d;
    std::optional<CalendarPeriod> duration;
    int sign = 1;
    size_t i = 0;
    if (token(t, 0) == "in" ||
        (periodUnit(token(t, 1)) && !token(t, 0).empty() && std::isdigit(static_cast<unsigned char>(t[0][0])))) {
        bool prefix = token(t, 0) == "in";
        i = prefix ? 1 : 0;
        duration = period(t, i);
        if (!prefix) {
            if (token(t, i) == "ago") {
                sign = -1;
                ++i;
            } else if (token(t, i) == "from" && token(t, i + 1) == "now")
                i += 2;
            else
                fail("duration", "Use in 8h, 8h from now, or 8h ago");
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
    if (i != t.size()) failAt("syntax", "Remove extra input or add a valid date/time duration", t.at(i));
    if (!iso) milliseconds = d.ms;
    if (!zone.local() && !d.clock)
        fail("time_required", "Add a clock time to convert between timezones, such as tomorrow at 4pm EST");
    if (duration && duration->hasTime && !d.clock)
        fail("time_required", "Add a starting time, such as tomorrow at 7am + 8h");
    std::optional<int64_t> timestamp;
    if (d.clock) {
        if (d.exact)
            timestamp = reference;
        else {
            try {
                timestamp = zoneTimestamp(d, zone, d.roll ? std::optional<int64_t>{reference} : std::nullopt);
            } catch (const Diagnostic &e) {
                if (!d.roll ||
                    (e.code != "past" && !(e.code == "nonexistent_time" &&
                                           d.h * 3600 + d.min * 60 + d.sec < ref.h * 3600 + ref.min * 60 + ref.sec)))
                    throw;
                if (d.roll == 12) {
                    do {
                        ++d.y;
                        check(d);
                    } while (!valid(d));
                } else
                    moveDays(d, d.roll);
                timestamp = zoneTimestamp(d, zone);
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
            if (d.clock) timestamp = zoneTimestamp(d, zone);
        }
        if (duration->days) {
            moveDays(d, sign * duration->days);
            if (d.clock) timestamp = zoneTimestamp(d, zone);
        }
        if (d.clock && duration->seconds) {
            *timestamp += sign * duration->seconds;
            d = zoneDate(*timestamp, zone);
        }
    }
    if (timestamp) d = zoneDate(*timestamp, {});
    d.ms = milliseconds;
    return {d, timestamp, zone};
}
Solution resolved(const Parsed &result, const Zone &dest, int64_t reference) {
    auto d = result.date;
    if (!result.timestamp) {
        if (!dest.local()) fail("time_required", "Add a clock time before converting to another timezone");
        return {Date{ymd(d)}, true};
    }
    auto timestamp = *result.timestamp;
    d = zoneDate(timestamp, dest);
    d.ms = result.date.ms;
    Instant value;
    value.time = sys_time<milliseconds>{seconds{timestamp} + milliseconds{d.ms}};
    value.destination = {ymd(d), d.h, d.min, d.sec, d.ms, d.offset, d.label};
    value.relativeDays = dayNumber(d) - dayNumber(zoneDate(reference, dest));
    if (!result.source.local() && result.source.name != dest.name)
        value.sourceLabel = zoneDate(timestamp, result.source).label;
    return {std::move(value), true};
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
Solution datetime(Source source, int64_t reference) {
    auto &text = source.text;
    if (reference < 0 || reference >= 32503680000LL) rangeError();
    if (text.starts_with("days until ") || text.starts_with("days since ")) {
        auto value = parse(source.slice(11), reference);
        if (value.timestamp) fail("date", "Use a date without a clock for calendar day differences");
        auto days =
            (dayNumber(value.date) - dayNumber(zoneDate(reference, {}))) * (text.starts_with("days since") ? -1 : 1);
        return {Duration{days * 86400, true}, true};
    }
    if (auto minus = text.find(" - "); minus != text.npos && temporalForm(text.substr(minus + 3))) {
        auto a = parse(source.slice(0, minus), reference);
        auto b = parse(source.slice(minus + 3), reference);
        if (bool(a.timestamp) != bool(b.timestamp))
            fail("date", "Subtract two dates or two times; add a clock to both for elapsed time");
        if (a.timestamp) {
            if (a.date.ms || b.date.ms) fail("duration", "Use whole-second timestamps for elapsed differences");
            return {Duration{*a.timestamp - *b.timestamp}, true};
        }
        return {Duration{(dayNumber(a.date) - dayNumber(b.date)) * 86400, true}, true};
    }
    if (text.starts_with("time diff ") || text.starts_with("diff ")) {
        auto name = text.substr(text.starts_with("time diff ") ? 10 : 5);
        auto zone = destination(source.slice(text.starts_with("time diff ") ? 10 : 5));
        auto there = zoneDate(reference, zone), here = zoneDate(reference, {});
        return {Duration{civilSeconds(there) - civilSeconds(here), false, true, name}, true};
    }
    if (text == "time") {
        auto origin = source.span(0, 4);
        source.replace(0, 4, Source(""));
        source.insert(0, "now", origin);
    } else if (text.starts_with("time in ")) {
        source = source.slice(5);
        try {
            destination(source.slice(3));
            auto origin = source.span(0, 3);
            source = source.slice(3);
            source.insert(0, "now to ", origin);
        } catch (const Diagnostic &) {
            if (text.size() <= 3 || text[3] < '0' || text[3] > '9') throw;
        }
    }
    std::optional<Zone> dest;
    auto split = text.find(" to ");
    if (split != text.npos) {
        dest = destination(source.slice(split + 4));
        source = source.slice(0, split);
    } else {
        // A duration's `in` remains arithmetic. Only a complete zone suffix converts.
        for (auto pos = text.rfind(" in "); pos != text.npos; pos = pos ? text.rfind(" in ", pos - 1) : text.npos) {
            try {
                dest = destination(source.slice(pos + 4));
                source = source.slice(0, pos);
                break;
            } catch (const Diagnostic &) {
                if (pos + 4 == text.size() || text[pos + 4] < '0' || text[pos + 4] > '9') throw;
            }
        }
        if (text.ends_with(" to"))
            failAt("timezone", "Add a destination after 'to', such as UTC or PT", source.span(text.size()));
    }
    auto value = parse(source, reference);
    if (dest && !value.timestamp) fail("time_required", "Add a clock time before converting to another timezone");
    return resolved(value, dest.value_or(Zone{}), reference);
}
} // namespace solver::detail
