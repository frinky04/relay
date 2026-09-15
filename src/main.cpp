#include "app.h"
#include <windows.h>
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    runVelopack();
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool startup = false;
    for (int i = 1; argv && i < argc; ++i) if (wcscmp(argv[i], L"--startup") == 0) startup = true;
    if (argv) LocalFree(argv);
    // Single instance: second launch just pokes the first.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Relay");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (!startup) if (HWND h = FindWindowW(L"Relay", nullptr)) PostMessageW(h, WM_HOTKEY, 1, 0);
        CloseHandle(mutex);
        return 0;
    }
    App app;
    if (!app.init(inst)) {
        MessageBoxW(nullptr, L"Unable to create the window or Direct3D 11 device; update the graphics driver, then try again", L"Relay", MB_ICONERROR);
        return 1;
    }
    int rc = app.run();
    app.shutdown();
    CloseHandle(mutex);
    return rc;
}
