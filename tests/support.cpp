#include "suites.h"
#include "config.h"
#include "fuzzy.h"
#include "notices.h"
#include <cstdio>
#include <vector>

// Regression checks for code retained through the engine deletion. Never opens
// the launcher or performs desktop actions.
int runSupportTests() {
    int failures = 0;
    auto check = [&](bool ok, const char* description) {
        if (!ok) { ++failures; printf("FAIL: %s\n", description); }
    };

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

    printf("support checks: %d failure(s)\n", failures);
    return failures;
}
