#include "suites.h"
#include "config.h"
#include "fuzzy.h"
#include "notices.h"
#include "menu_layout.h"
#include <cstdio>
#include <vector>

// Regression checks for code retained through the engine deletion. Never opens
// the launcher or performs desktop actions.
int runSupportTests() {
    int failures = 0;
    auto check = [&](bool ok, const char* description) {
        if (!ok) { ++failures; printf("FAIL: %s\n", description); }
    };

    {
        std::vector<MenuRow> rows(5);
        rows[1].stacked = rows[3].stacked = true;
        const MenuLayout layout(rows, 30, 50);
        check(layout.position(1) == 30 && layout.position(2) == 80 && layout.position(5) == 190,
            "detail rows expand without changing neighboring compact rows");
        check(layout.position(4) - layout.position(1) == 130,
            "a three-row logical viewport includes its complete mixed-height rows");
        check(layout.position(1.5f) == 55 && layout.position(2.5f) == 95,
            "animated selection edges interpolate continuously between different row heights");
        check(layout.position(-1) == 0 && layout.position(9) == 190 && MenuLayout({}, 30, 50).position(1) == 0,
            "empty and replaced lists safely clamp animated positions");
    }

    check(fuzzy::score("xyz", "Google Chrome") == 0, "unrelated input does not match");
    check(fuzzy::score("gc", "Google Chrome") > 0, "abbreviations match word boundaries");
    check(fuzzy::score("CHR", "chrome") == fuzzy::score("chr", "chrome"), "matching ignores ASCII case");
    check(fuzzy::score("chrome", "chrome") > fuzzy::score("chr", "Google Chrome"), "exact names outrank partial names");

    UINT mods = 0, key = 0;
    check(Config::parseHotkey("alt+space", mods, key) && mods == MOD_ALT && key == VK_SPACE, "default hotkey");
    check(Config::parseHotkey(" Ctrl + Shift + F12 ", mods, key) && mods == (MOD_CONTROL | MOD_SHIFT) && key == VK_F12, "configured hotkey");
    check(!Config::parseHotkey("alt+unknown", mods, key), "invalid hotkey is rejected");

    NoticeStore notices;
    for (unsigned long long i = 0; i < 25; ++i) notices.push({ "Notice", "Body", i });
    std::vector<unsigned long long> ticks;
    notices.forEach([&](const NoticeStore::Notice& n) { ticks.push_back(n.tick); });
    check(ticks.size() == 20 && ticks.front() == 24 && ticks.back() == 5, "notices are bounded and newest first");
    notices.dismiss(24);
    ticks.clear();
    notices.forEach([&](const NoticeStore::Notice& n) { ticks.push_back(n.tick); });
    check(ticks.size() == 19 && ticks.front() == 23, "dismiss removes only the selected notice");
    notices.clear();
    int count = 0;
    notices.forEach([&](const NoticeStore::Notice&) { ++count; });
    check(count == 0, "clear removes all notices");

    notices.push({"Other notice", "Unrelated", 30});
    notices.push({"Checking for updates...", "Progress", 31, "updates"});
    notices.push({"Relay is up to date", "Result", 32, "updates"});
    ticks.clear();
    notices.forEach([&](const NoticeStore::Notice& n) { ticks.push_back(n.tick); });
    check(ticks == std::vector<unsigned long long>{32, 30}, "update results replace progress without removing unrelated notices");
    notices.dismiss(32);
    notices.push({"Update ready", "New check result", 33, "updates"});
    ticks.clear();
    notices.forEach([&](const NoticeStore::Notice& n) { ticks.push_back(n.tick); });
    check(ticks == std::vector<unsigned long long>{33, 30}, "a new update result can follow a dismissed notice");

    printf("support checks: %d failure(s)\n", failures);
    return failures;
}
