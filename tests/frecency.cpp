#include "suites.h"
#include "app_command.h"
#include <chrono>
#include <cstdio>
#include <fstream>

int runFrecencyTests() {
    int failures = 0;
    auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failures; printf("FAIL: %s\n", message); }
    };
    const auto directory = std::filesystem::temp_directory_path() / ("relay-history-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "history.tsv";
    Frecency history(path);
    check(history.load().empty() && history.score("missing") == 0, "missing history starts empty");
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    {
        std::ofstream file(path);
        file << "recent\t2\t" << now << "\ndaily\t2\t" << now - 7200
             << "\nweekly\t2\t" << now - 172800 << "\nold\t2\t" << now - 864000
             << "\nsaturated\t2147483647\t" << now << "\nfuture\t1\t" << now + 86400
             << "\nnegative\t-1\t1\nbad\tnope\t1\nextra\t1\t1junk\noverflow\t999999999999\t1\n"
             << "negative-time\t1\t-1\n\t1\t1\n";
    }
    check(history.load().empty() && history.score("recent") == 80 && history.score("daily") == 40 &&
        history.score("weekly") == 20 && history.score("old") == 10, "history weights frequency by recency");
    history.bump("saturated");
    check(history.score("saturated") == 2000 && history.score("future") == 40, "large counts and future dates stay bounded");
    for (auto key : {"negative", "bad", "extra", "overflow", "negative-time", ""})
        check(history.score(key) == 0, "malformed history records are ignored");
    check(history.save().empty(), "save replaces an existing history file");
    history.bump("recent");
    check(history.save().empty(), "history can be saved repeatedly");
    Frecency restored(path);
    check(restored.load().empty() && restored.score("recent") == 120, "history survives reconstruction");
    { std::ofstream file(path); file << "replacement\t1\t" << now << '\n'; }
    check(restored.load().empty() && restored.score("recent") == 0, "loading replaces previous records");

    const auto appPath = directory / "apps.tsv";
    auto appHistory = std::make_shared<Frecency>(appPath);
    std::string launched, failure, notice;
    auto launch = [&](const std::string& target, desktop::AppAction) { launched = target; return failure; };
    std::vector<command::Command> apps{appCommand({{"Fire", "short-target"}, {"Firefox", "browser-target"}, {"Other", "other-target"}},
        launch, appHistory, [&](std::string error) { notice = std::move(error); })};
    auto query = [&](const std::string& text) { return command::evaluate(apps, text); };
    auto unused = query("");
    check(unused.view.rows.size() == 3 && unused.view.rows[0].title == "Fire" &&
        unused.view.rows[1].title == "Firefox" && unused.view.rows[2].title == "Other", "empty input without history is alphabetical");
    check(query("fi").view.rows[0].title == "Fire" && query("/app fi").view.rows[0].title == "Fire",
        "unused apps use fuzzy scores");
    query(""); query("/"); query("/app "); query("Firefox");
    check(!std::filesystem::exists(appPath) && appHistory->score("browser-target") == 0, "discovery never writes launch history");
    check(query("/app Firefox").actions[0]().empty() && launched == "browser-target" && notice.empty(), "successful explicit launch records its shell identity");
    auto ranked = query("fi");
    check(ranked.view.rows[0].title == "Firefox" && query("/app fi").view.rows[0].title == "Firefox",
        "successful launches boost matching apps in bare and scoped searches");
    auto home = query("");
    check(home.view.rows[0].title == "Firefox" && home.view.rows[1].title == "Fire" && home.view.rows[2].title == "Other",
        "empty input puts used apps first and keeps unused apps alphabetical");
    check(query("/app ").view.rows[0].title == "Fire", "explicit app catalog stays alphabetical despite history");
    const int beforeFileActions = appHistory->score("browser-target");
    for (const auto* verb : {"Copy Path", "Open File Location"})
        check(query(std::string("/app Firefox ") + verb).actions[0]().empty() && launched == "browser-target" &&
            appHistory->score("browser-target") == beforeFileActions, "file actions retain app identity without counting as launches");
    check(query("/app Firefox Run as Administrator").actions[0]().empty() &&
        appHistory->score("browser-target") == beforeFileActions + 40, "successful administrator launches count toward history");
    check(home.actions[0]().empty() && launched == "browser-target" &&
        query(home.view.rows[0].completion).actions[0]().empty() && launched == "browser-target",
        "empty-input activation and completion bind the displayed app");
    auto completed = query(ranked.view.rows[0].completion);
    check(completed.actions[0]().empty() && launched == "browser-target", "ranked completion keeps the displayed app target");
    for (int i = 0; i < 100; ++i) appHistory->bump("browser-target");
    command::Command noun{"fire", "Exact command"};
    noun.verbs.push_back({"Run", false, [](auto&) { return std::string{}; }});
    auto combined = apps;
    combined.push_back(noun);
    auto exact = command::evaluate(combined, "Fire");
    check(exact.view.rows[0].title == "Fire" && exact.view.rows[1].title == "fire" && exact.view.rows[2].title == "Firefox",
        "exact app and command names outrank a maximally boosted fuzzy app");
    check(query("other").view.rows.size() == 1 && query("zzz").view.rows.empty(), "history never creates unrelated matches");
    failure = "Cannot launch; try again";
    check(query("/app Fire").actions[0]() == failure && appHistory->score("short-target") == 0, "failed launches do not count");
    failure.clear();
    for (int i = 0; i < 100; ++i) appHistory->bump("short-target");
    check(query("").view.rows[0].title == "Fire", "equal frecency scores use alphabetical app order");
    check(ranked.actions[0]().empty() && launched == "browser-target" && home.actions[0]().empty() && launched == "browser-target",
        "typed and empty-input actions retain their target after history changes");
    auto persisted = std::make_shared<Frecency>(appPath);
    check(persisted->load().empty() && persisted->score("browser-target") > 0, "successful app launches save history immediately");
    std::vector<command::Command> renamed{appCommand({{"Renamed Browser", "browser-target"}, {"Renamed Browser", "new-target"}}, launch, persisted)};
    auto duplicates = command::evaluate(renamed, "Renamed Browser");
    check(duplicates.view.rows.size() == 2 && duplicates.view.rows[0].iconKey == "browser-target" &&
        persisted->score("new-target") == 0, "renames and duplicate names keep distinct shell histories");

    const auto homePath = directory / "home.tsv";
    {
        std::ofstream file(homePath);
        file << "beta\t1\t" << now - 864000 << "\nzebra\t1\t" << now - 864000
             << "\ngamma\t2\t" << now - 864000 << '\n';
    }
    auto oldHistory = std::make_shared<Frecency>(homePath);
    check(oldHistory->load().empty(), "load persisted empty-input ranking");
    std::vector<command::Command> oldApps{appCommand({{"Zebra", "zebra"}, {"Gamma", "gamma"}, {"Alpha", "alpha"}, {"Beta", "beta"}},
        launch, oldHistory)};
    auto restoredHome = command::evaluate(oldApps, "");
    check(restoredHome.view.rows.size() == 4 && restoredHome.view.rows[0].title == "Gamma" &&
        restoredHome.view.rows[1].title == "Beta" && restoredHome.view.rows[2].title == "Zebra" && restoredHome.view.rows[3].title == "Alpha",
        "empty input uses persisted full scores so old single launches still outrank unused apps, with alphabetical ties");
    check(command::evaluate(oldApps, "/app ").view.rows[0].title == "Alpha", "persisted history preserves alphabetical catalog browsing");
    check(restoredHome.actions[3]().empty() && command::evaluate(oldApps, "").view.rows[0].title == "Alpha",
        "the next empty-input request reflects a successful launch immediately");

    const auto blocked = directory / "blocked";
    { std::ofstream file(blocked); file << "not a directory"; }
    auto blockedHistory = std::make_shared<Frecency>(blocked / "history.tsv");
    std::vector<command::Command> unwritable{appCommand({{"Test", "test-target"}}, launch, blockedHistory,
        [&](std::string error) { notice = std::move(error); })};
    check(command::evaluate(unwritable, "Test").actions[0]().empty() && launched == "test-target" &&
        !notice.empty() && blockedHistory->score("test-target") > 0, "save errors report a notice without failing the launch or losing session history");
    Frecency unreadable(directory);
    check(!unreadable.load().empty(), "unreadable history returns a recovery error");
    // A failed replacement must preserve the previous history file.
    const auto tempPath = std::filesystem::path(path.wstring() + L".tmp");
    std::filesystem::create_directory(tempPath);
    check(!history.save().empty(), "blocked temporary history reports a save failure");
    Frecency intact(path);
    check(intact.load().empty() && intact.score("replacement") == 40, "failed saves retain existing history");
    std::filesystem::remove(tempPath);
    std::filesystem::remove(blocked);
    std::filesystem::remove(path);
    std::filesystem::remove(appPath);
    std::filesystem::remove(homePath);
    std::filesystem::remove(directory);
    printf("frecency checks: %d failure(s)\n", failures);
    return failures;
}
