#pragma once
int runUpdateTests();
int runUpdateHttpTest(const char* directory);
int runUpdateFeedTest(const char* portableRoot, const char* releaseFeed);

int runSupportTests();
int runConfigTests();
int runWatcherTests();
int runCommandTests();
int runFrecencyTests();
