#include "suites.h"
#include <cstdio>
#include <exception>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "update-http") {
        try { return runUpdateHttpTest(argv[2]); }
        catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
    }
    if (argc == 4 && std::string_view(argv[1]) == "update-feed") {
        try { return runUpdateFeedTest(argv[2], argv[3]); }
        catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
    }
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    const struct { const char* name; int (*run)(); } suites[] = {
        {"support", runSupportTests},
        {"watcher", runWatcherTests},
        {"commands", runCommandTests},
        {"frecency", runFrecencyTests},
        {"updates", runUpdateTests},
    };
    int failures = 0;
    bool found = false;
    if (argc <= 2) for (const auto& suite : suites) {
        if (selected != "all" && selected != suite.name) continue;
        found = true;
        try { failures += suite.run(); }
        catch (const std::exception& e) {
            std::fprintf(stderr, "FAIL: %s suite: %s\n", suite.name, e.what());
            ++failures;
        }
    }
    if (!found) {
        std::fprintf(stderr, "Usage: relay_test [all|support|watcher|commands|frecency|updates]\n");
        return 2;
    }
    return failures ? 1 : 0;
}
