#include "app.h"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    // Single instance: second launch just pokes the first.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\Relay");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND h = FindWindowW(L"Relay", nullptr)) PostMessageW(h, WM_HOTKEY, 1, 0);
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
