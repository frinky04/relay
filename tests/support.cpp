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

    {
        int first = 0;
        for (int selected = 0; selected < 20; ++selected)
            first = menuViewportStart(first, selected, 40, 9, true);
        check(first == 11, "down navigation scrolls only after crossing the bottom edge");
        for (int selected = 18; selected >= 11; --selected)
            first = menuViewportStart(first, selected, 40, 9, true);
        check(first == 11, "reversing direction walks up within the existing viewport");
        first = menuViewportStart(first, 10, 40, 9, true);
        check(first == 10, "crossing the top edge scrolls only one row");
        check(menuViewportStart(first, 14, 40, 9, false) == 10 &&
              menuViewportStart(first, 9, 40, 9, false) == 10,
            "hover preserves the viewport including a trailing animated row");
        check(menuViewportStart(31, 0, 40, 9, true) == 0 &&
              menuViewportStart(0, 39, 40, 9, true) == 31,
            "wrapping targets the opposite end of the list");
        check(menuViewportStart(31, 9, 10, 9, false) == 1 &&
              menuViewportStart(31, 0, 0, 9, false) == 0 &&
              menuViewportStart(31, 3, 4, 9, true) == 0,
            "shrinking, empty and short lists clamp the viewport");
        check(menuViewportStart(31, 39, 40, 12, false) == 28,
            "increasing visible row count clamps the viewport at the end");
    }

    {
        float remainder = 0;
        int first = 0;
        for (int i = 0; i < 3; ++i) first = menuWheelStart(first, 40, 9, 0.25f, remainder);
        check(first == 0 && remainder == 0.75f, "small wheel deltas accumulate without losing input");
        first = menuWheelStart(first, 40, 9, 0.25f, remainder);
        check(first == 1 && remainder == 0, "accumulated wheel input advances a whole row");
        first = menuWheelStart(first, 40, 9, 100, remainder);
        check(first == 31 && remainder == 0, "wheel clamps at the bottom without banking overscroll");
        first = menuWheelStart(first, 40, 9, -1, remainder);
        check(first == 30, "wheel reverses immediately after reaching the bottom");
        first = menuWheelStart(first, 40, 9, -100, remainder);
        check(first == 0 && remainder == 0, "wheel clamps at the top");
        remainder = 0.75f;
        check(menuWheelStart(10, 40, 9, -1, remainder) == 9 && remainder == 0,
            "reversing direction clears stale fractional wheel motion");
        check(menuWheelStart(0, 0, 9, 3, remainder) == 0 &&
              menuWheelStart(0, 4, 9, 3, remainder) == 0,
            "wheel leaves empty and short lists stationary");

        const int down = menuNavigationSelection(4, 40, 9, 1, true, false, 0, 8);
        check(down == 12 && menuNavigationSelection(down, 40, 9, -1, true, false, 8, 16) == 4,
            "page navigation moves eight rows in a nine-row viewport and reverses symmetrically");
        check(menuNavigationSelection(38, 40, 9, 1, true, false, 31, 39) == 39 &&
              menuNavigationSelection(1, 40, 9, -1, true, false, 0, 8) == 0,
            "page navigation clamps instead of wrapping");
        check(menuNavigationSelection(0, 40, 9, 1, false, true, 12, 19) == 12 &&
              menuNavigationSelection(39, 40, 9, -1, false, true, 12, 19) == 19,
            "arrows re-enter manually scrolled content at the visible edge");
        check(menuNavigationSelection(0, 40, 9, -1, true, true, 12, 19) == 19 &&
              menuNavigationSelection(15, 40, 9, 1, false, true, 12, 19) == 16,
            "offscreen page selection re-enters while visible arrow selection advances normally");
        check(menuNavigationSelection(3, 10, 1, 1, true, false, 3, 3) == 4 &&
              menuNavigationSelection(0, 0, 9, 1, true, false, 0, 0) == 0,
            "page navigation handles one-row and empty viewports");

        std::vector<MenuRow> rows(5);
        rows[1].stacked = rows[3].stacked = true;
        const MenuLayout layout(rows, 30, 50);
        check(layout.visibleRows(0.5f, 130) == std::pair{1, 2},
            "re-entry excludes partially clipped rows at both edges with mixed heights");
        check(layout.visibleRows(1, 130) == std::pair{1, 3},
            "re-entry includes rows exactly aligned with viewport edges");
    }

    check(fuzzy::score("xyz", "Google Chrome") == 0, "unrelated input does not match");
    check(fuzzy::score("gc", "Google Chrome") > 0, "abbreviations match word boundaries");
    check(fuzzy::score("CHR", "chrome") == fuzzy::score("chr", "chrome"), "matching ignores ASCII case");
    check(fuzzy::score("chrome", "chrome") > fuzzy::score("chr", "Google Chrome"), "exact names outrank partial names");

    using Kind = HotkeyBinding::Kind;
    check(HotkeyBinding::parse("alt+space") == HotkeyBinding{Kind::Shortcut, MOD_ALT, VK_SPACE}, "default hotkey");
    check(HotkeyBinding::parse(" Ctrl + Shift + F12 ") == HotkeyBinding{Kind::Shortcut, MOD_CONTROL | MOD_SHIFT, VK_F12}, "configured hotkey");
    check(HotkeyBinding::parse(" WIN ") == HotkeyBinding{Kind::WinTap} &&
        HotkeyBinding::parse("super") == HotkeyBinding{Kind::WinTap}, "bare Win aliases select a tap binding");
    check(HotkeyBinding::parse("win+space") == HotkeyBinding{Kind::Shortcut, MOD_WIN, VK_SPACE} &&
        HotkeyBinding::parse("control+super+f24") == HotkeyBinding{Kind::Shortcut, MOD_CONTROL | MOD_WIN, VK_F24},
        "Win chords keep native shortcut semantics");
    for (const auto* invalid : {"", "alt+unknown", "alt", "ctrl+win", "win+", "+space", "alt++space",
            "ctrl+control+a", "a+b", "win+win", "f0", "f25", "f12oops"})
        check(!HotkeyBinding::parse(invalid), "malformed or ambiguous hotkeys are rejected");

    for (UINT win : {VK_LWIN, VK_RWIN}) {
        WinKeyTap tap;
        check(!tap.key(win, false), "unmatched Win release does not activate");
        check(!tap.key(win, true) && !tap.key(win, true) && tap.key(win, false) && !tap.key(win, false),
            "either Win key activates exactly once on release, including autorepeat");
        for (UINT other : {UINT('E'), UINT('L'), UINT(VK_TAB), UINT(VK_LSHIFT), UINT(VK_LCONTROL), UINT(VK_LMENU)}) {
            tap.reset();
            tap.key(win, true);
            tap.key(other, true);
            tap.key(other, false);
            tap.key(win, true);
            check(!tap.key(win, false), "a chord stays cancelled after the other key releases and Win repeats");
            tap.reset();
            tap.key(other, true);
            tap.key(win, true);
            tap.key(other, false);
            check(!tap.key(win, false), "a key held before Win prevents a standalone tap");
            tap.reset();
            tap.key(win, true);
            tap.key(other, true);
            check(!tap.key(win, false), "releasing Win before the chord key does not activate");
        }
        tap.reset();
        tap.key(win, true, true);
        check(!tap.key(win, false, true), "injected Win taps do not activate Relay");
        tap.key(win, true);
        tap.key('A', true, true);
        tap.key('A', false, true);
        check(!tap.key(win, false), "foreign injected keys cancel a physical Win tap");
        WinKeyTap::Keys held{};
        held[win] = true;
        tap.reset(held);
        tap.key(win, true);
        check(!tap.key(win, false), "installing or reloading while Win is held does not activate");
        tap.key(win, true);
        tap.key('L', true);
        tap.reset(); // fresh physical state after returning from the lock screen
        tap.key(win, true);
        check(tap.key(win, false), "a fresh tap works after missed releases on another desktop");
    }
    WinKeyTap both;
    both.key(VK_LWIN, true);
    both.key(VK_RWIN, true);
    check(!both.key(VK_LWIN, false) && !both.key(VK_RWIN, false), "holding both Win keys does not activate");

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
