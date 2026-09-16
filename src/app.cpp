#include "app.h"
#include "theme.h"
#include "icons.h"
#include "util.h"
#include "app_command.h"
#include "relay_command.h"
#include "window_command.h"
#include "process_command.h"
#include "system_command.h"
#include "lua_commands.h"
#include "menu_layout.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace fs = std::filesystem;

static constexpr int HOTKEY_ID = 1;
static constexpr wchar_t CLASS_NAME[] = L"Relay";
static constexpr UINT WM_APP_NOTICE = WM_APP + 1;   // lParam: new std::pair<std::string,std::string>*
static constexpr UINT WM_APP_CONFIG = WM_APP + 2;  // lParam: ConfigUpdate*
static constexpr UINT WM_APP_ENGINE = WM_APP + 3;
static constexpr UINT WM_APP_QUIT = WM_APP + 4;
static constexpr UINT WM_APP_UPDATE_NOTICE = WM_APP + 5;
static constexpr unsigned long long PEEK_MS = 4000;

struct ConfigUpdate {
    Config settings;
    std::string error;
};

// ---------------------------------------------------------------- window ---

LRESULT CALLBACK App::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)l;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        ((App*)cs->lpCreateParams)->m_hwnd = h;
    }
    auto* self = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (self) return self->handle(m, w, l);
    return DefWindowProcW(h, m, w, l);
}

LRESULT App::handle(UINT m, WPARAM w, LPARAM l) {
    if (ImGui_ImplWin32_WndProcHandler(m_hwnd, m, w, l)) return 1;
    switch (m) {
    case WM_HOTKEY:
        if (w == HOTKEY_ID) toggle();
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE && m_visible && !m_peeking &&
            !(m_pendingAction && m_pendingAction->generation == m_generation && m_pendingAction->stayOpen)) hide();
        return 0;
    case WM_SYSCOMMAND:
        if ((w & 0xfff0) == SC_KEYMENU) return 0; // alt alone must not open a menu
        break;
    case WM_DPICHANGED: {
        const float scale = HIWORD(w) / 96.0f;
        // placeWindow already supplies scaled geometry when opening on another monitor.
        if (scale == m_scale) return 0;
        setScale(scale);
        const RECT& rect = *reinterpret_cast<const RECT*>(l);
        m_winW = rect.right - rect.left;
        m_winH = rect.bottom - rect.top;
        if (m_swap) ensureBuffers(m_winW, maxHeight());
        SetWindowPos(m_hwnd, nullptr, rect.left, rect.top, m_winW, m_winH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_APP_NOTICE: {
        auto* p = (std::pair<std::string, std::string>*)l;
        onNotice(Notice{ p->first, p->second, GetTickCount64() });
        delete p;
        return 0;
    }
    case WM_APP_UPDATE_NOTICE: {
        auto* p = (std::pair<std::string, std::string>*)l;
        // Background checks never reveal a hidden launcher or replace input.
        logf("update: %s | %s", p->first.c_str(), p->second.c_str());
        m_notices.push({std::move(p->first), std::move(p->second), GetTickCount64(), "updates"});
        delete p;
        if (m_visible) { refreshUpdateRows(); refreshNotices(); }
        return 0;
    }
    case WM_APP_CONFIG: {
        auto* update = (ConfigUpdate*)l;
        m_config = std::move(update->settings);
        if (!update->error.empty()) onNotice({ "Config error", update->error, GetTickCount64() });
        delete update;
        registerHotkey();
        if (m_visible) placeWindow();
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_APP_QUIT:
        quit();
        return 0;
    case WM_APP_ENGINE:
        // Visible results are applied after queued input edits in drawUi.
        if (!m_visible) {
            if (auto done = m_engine.takeCompletion()) {
                if (!done->error.empty()) logf("command: %s", done->error.c_str());
                m_pendingAction.reset();
            }
        }
        return 0;
    }
    return DefWindowProcW(m_hwnd, m, w, l);
}

bool App::init(HINSTANCE inst) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
                             CLASS_NAME, L"Relay", WS_POPUP,
                             0, 0, 640, 100, nullptr, nullptr, inst, this);
    if (!m_hwnd) return false;

    m_scale = GetDpiForWindow(m_hwnd) / 96.0f;
    if (!createDevice()) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigInputTextCursorBlink = false;

    std::string font = narrow(exeDir()) + "\\assets\\NotoSansMono-Medium.ttf";
    ImFontConfig fc;
    fc.OversampleH = 2; fc.OversampleV = 1;
    if (!io.Fonts->AddFontFromFileTTF(font.c_str(), theme::FONT_SIZE, &fc))
        io.Fonts->AddFontDefault();
    setScale(m_scale);

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_dev.Get(), m_ctx.Get());
    IconCache::instance().init(m_dev.Get(), (int)(theme::ICON_SZ * m_scale * 2)); // 2x for crispness

    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    COLORREF border = RGB(0x2c, 0x2e, 0x31);
    DwmSetWindowAttribute(m_hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));

    // Configuration is loaded before the window becomes interactive. Subsequent
    // file changes are parsed on the watcher thread and delivered as plain data.
    std::string error;
    if (auto referenceError = Config::refreshReference(Config::path()); !referenceError.empty())
        m_notices.push({"Config reference", std::move(referenceError), GetTickCount64()});
    if (!m_config.load(error)) {
        logf("config: %s", error.c_str());
        m_notices.push({ "Config error", error, GetTickCount64() });
    } else if (auto startupError = desktop::setStartup(m_config.startWithWindows); !startupError.empty()) {
        m_notices.push({"Windows startup", std::move(startupError), GetTickCount64()});
    }
    const HWND hwnd = m_hwnd;
    auto report = [hwnd](std::string error) {
        auto* notice = new std::pair<std::string, std::string>("Command error", std::move(error));
        if (!PostMessageW(hwnd, WM_APP_NOTICE, 0, (LPARAM)notice)) delete notice;
    };
    auto copy = [hwnd](const std::string& text) { return clipboardCopy(text, hwnd); };
    m_updates.start(velopackUpdates([hwnd] {
        if (!PostMessageW(hwnd, WM_APP_QUIT, 0, 0)) throw std::runtime_error("Cannot request shutdown");
    }), [hwnd](std::string title, std::string body) {
        auto* notice = new std::pair<std::string, std::string>(std::move(title), std::move(body));
        if (!PostMessageW(hwnd, WM_APP_UPDATE_NOTICE, 0, (LPARAM)notice)) delete notice;
    });
    auto history = std::make_shared<Frecency>(fs::path(dataDir()) / L"frecency.tsv");
    auto runApp = [copy](const std::string& target, desktop::AppAction action) { return desktop::runApp(target, action, copy); };
    auto scanApps = [history, report, runApp] { return appCommand(desktop::listApps(), runApp, history, report); };
    m_engine.start([this, hwnd, report, copy, history, runApp, scanApps](auto reloadPlugins, auto rescanApps) {
        auto error = history->load();
        if (!error.empty()) report(std::move(error));
        std::vector<command::Command> commands;
        try { commands.push_back(scanApps()); }
        catch (const std::exception& e) {
            report(e.what());
            commands.push_back(appCommand({}, runApp, history, report));
        }
        commands.push_back(windowCommand(desktop::listWindows, desktop::runWindow));
        commands.push_back(processCommand(desktop::listProcesses, desktop::killProcess));
        commands.push_back(systemCommand(desktop::runSystem, desktop::canHibernate));
        commands.push_back(relayCommand(Config::path(), fs::path(dataDir()) / L"plugins", RELAY_VERSION,
            desktop::editTextFile, desktop::openFolder, copy, [hwnd]() -> std::string {
                if (PostMessageW(hwnd, WM_APP_QUIT, 0, 0)) return {};
                return "Cannot request shutdown; try Quit again";
            }, std::move(reloadPlugins), [this] { return m_updates.check(); }, [this] { return m_updates.restart(); }, std::move(rescanApps)));
        return commands;
    }, [copy, report](std::vector<command::Command>& commands) {
        loadLuaCommands(commands, fs::path(exeDir()) / L"plugins", copy, desktop::openUrl, report);
        loadLuaCommands(commands, fs::path(dataDir()) / L"plugins", copy, desktop::openUrl, report);
    }, [hwnd] { PostMessageW(hwnd, WM_APP_ENGINE, 0, 0); }, scanApps);
    startWatcher();
    registerHotkey();
    return true;
}

void App::startWatcher() {
    const fs::path directory(dataDir());
    std::error_code error;
    fs::create_directories(directory, error);
    const HWND hwnd = m_hwnd;
    m_watcher.start({ directory }, [hwnd](const fs::path& path) {
        if (path.filename() != L"init.lua") return;
        auto* update = new ConfigUpdate;
        if (update->settings.load(update->error))
            update->error = desktop::setStartup(update->settings.startWithWindows);
        if (!PostMessageW(hwnd, WM_APP_CONFIG, 0, (LPARAM)update)) delete update;
    });
}

void App::registerHotkey() {
    UnregisterHotKey(m_hwnd, HOTKEY_ID);
    if (!RegisterHotKey(m_hwnd, HOTKEY_ID, m_config.hotkeyMods | MOD_NOREPEAT, m_config.hotkeyVk)) {
        onNotice({ "Hotkey unavailable", "Unable to register " + m_config.hotkeyText + "; close the app that uses it, then save init.lua or restart Relay", GetTickCount64() });
    }
}

void App::shutdown() {
    UnregisterHotKey(m_hwnd, HOTKEY_ID);
    m_watcher.stop();
    m_engine.stop();
    m_updates.stop();
    MSG pending{};
    while (PeekMessageW(&pending, m_hwnd, WM_APP_CONFIG, WM_APP_CONFIG, PM_REMOVE))
        delete (ConfigUpdate*)pending.lParam;
    while (PeekMessageW(&pending, m_hwnd, WM_APP_NOTICE, WM_APP_NOTICE, PM_REMOVE))
        delete (std::pair<std::string, std::string>*)pending.lParam;
    while (PeekMessageW(&pending, m_hwnd, WM_APP_UPDATE_NOTICE, WM_APP_UPDATE_NOTICE, PM_REMOVE))
        delete (std::pair<std::string, std::string>*)pending.lParam;
    IconCache::instance().shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    if (m_hwnd) DestroyWindow(m_hwnd);
    CoUninitialize();
}

void App::quit() { m_running = false; if (m_visible) hide(); finishHide(); PostQuitMessage(0); }

// ---------------------------------------------------------------- d3d11 / dcomp ---

bool App::createDevice() {
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2,
                                   D3D11_SDK_VERSION, &m_dev, &got, &m_ctx);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2,
                               D3D11_SDK_VERSION, &m_dev, &got, &m_ctx);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDev;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(m_dev.As(&dxgiDev)) || FAILED(dxgiDev->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

    // Composition swapchain: DWM shows the buffer as a visual at its own size,
    // and the visual's opacity drives show/hide fades on the compositor thread.
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = 640; sd.Height = 100;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap1;
    if (FAILED(factory->CreateSwapChainForComposition(m_dev.Get(), &sd, nullptr, &swap1))) return false;
    m_swap = swap1;

    if (FAILED(DCompositionCreateDevice3(dxgiDev.Get(), IID_PPV_ARGS(&m_dcomp)))) return false;
    if (FAILED(m_dcomp->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget))) return false;
    if (FAILED(m_dcomp->CreateVisual(&m_visual))) return false;
    m_visual->SetContent(m_swap.Get());
    m_dcompTarget->SetRoot(m_visual.Get());
    setOpacity(0.0f);
    m_dcomp->Commit();
    createRtv();
    return true;
}

void App::setOpacity(float a) {
    if (a == m_opacity) return;
    m_opacity = a;
    Microsoft::WRL::ComPtr<IDCompositionVisual3> v3;
    if (SUCCEEDED(m_visual.As(&v3))) v3->SetOpacity(a);
}

void App::createRtv() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back;
    if (SUCCEEDED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back))))
        m_dev->CreateRenderTargetView(back.Get(), nullptr, &m_rtv);
}

void App::destroyDevice() {
    m_visual.Reset(); m_dcompTarget.Reset(); m_dcomp.Reset();
    m_rtv.Reset(); m_swap.Reset(); m_ctx.Reset(); m_dev.Reset();
}

// The swapchain stays allocated at the tallest layout for the current width
// and scale; the HWND clips the composition visual. Animating the height then
// only moves the window instead of stalling on ResizeBuffers every frame.
int App::maxHeight() const {
    const float S = m_scale;
    return (int)std::ceil(theme::INPUT_H * S + 1.0f + (m_config.maxRows + 1) * theme::ROW_H_DETAIL * S + theme::FOOT_H * S);
}

void App::ensureBuffers(int w, int h) {
    if (w == m_swapW && h == m_swapH) return;
    m_swapW = w; m_swapH = h;
    m_rtv.Reset();
    m_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    createRtv();
}

void App::resizeTo(int w, int h) {
    if (w == m_winW && h == m_winH) return;
    m_winW = w; m_winH = h;
    ensureBuffers(w, maxHeight());
    SetWindowPos(m_hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void App::placeWindow() {
    // Center horizontally on the monitor holding the foreground window; top at 22% of work area.
    HWND fg = GetForegroundWindow();
    HMONITOR mon = MonitorFromWindow(fg ? fg : m_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy);
    const float previousScale = m_scale;
    setScale(dx / 96.0f);
    int w = (int)(m_config.width * m_scale);
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + (int)((mi.rcWork.bottom - mi.rcWork.top) * 0.22f);
    int h = m_winH ? std::max(1, (int)std::lround(m_winH * m_scale / previousScale))
                  : (int)(theme::INPUT_H * m_scale);
    ensureBuffers(w, maxHeight());
    m_winW = w; m_winH = h;
    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

void App::setScale(float s) {
    m_animH *= s / m_scale;
    m_scale = s;
    if (ImGui::GetCurrentContext()) theme::apply(s);
}

// ---------------------------------------------------------------- show / hide ---

// Shared by show() and peek(): start a fresh session at zero height and opacity.
// Prime the hidden frame before showing; subsequent frames ease toward content.
void App::reveal(bool takeFocus) {
    if (m_fading) { // reopened mid fade-out: cancel it, restart from transparent
        m_fading = false;
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
    }
    m_visible = true;
    m_peeking = !takeFocus;
    replaceInput("");
    m_lastInput.clear();
    m_selected = 0;
    m_focusInput = takeFocus;
    m_animH = 0.0f;
    m_revealFrame = m_animSnapList = true;
    m_iconPending.clear();
    m_iconReveal.clear();
    m_results.clear();
    m_spans.clear();
    m_spanText.clear();
    m_hasView = false;
    m_altReleased = (GetAsyncKeyState(VK_MENU) & 0x8000) == 0; // Alt still held from the hotkey: ignore it until released
    m_altBlend = 0.0f;
    m_slots.clear();
    // Key-ups are lost while hidden (focus moved with the launched app), so
    // ImGui would still see Enter held and re-fire it as a repeat.
    ImGui::GetIO().ClearInputKeys();
    ImGui::GetIO().ClearInputMouse();
    refreshRows();
    placeWindow();
    frame();
    setOpacity(0.0f);
    m_dcomp->Commit();
    m_showTick = GetTickCount64();
    ShowWindow(m_hwnd, SW_SHOWNA);
}

void App::show() {
    if (m_visible && m_peeking) {
        // Promote the peek: keep the window, take focus, start a real session.
        m_peeking = false;
        m_focusInput = true;
        refreshRows();
    } else if (!m_visible) {
        reveal(true);
    }
    m_focusInput = true;

    // Foreground-steal: attach to the current foreground thread's input queue.
    // Attaching synchronizes with that thread, so a hung foreground app would
    // hang the hotkey handler; fall through to the Alt-tap fallback instead.
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg && !IsHungAppWindow(fg) ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD me = GetCurrentThreadId();
    if (fgThread && fgThread != me) AttachThreadInput(fgThread, me, TRUE);
    SetForegroundWindow(m_hwnd);
    SetFocus(m_hwnd);
    if (fgThread && fgThread != me) AttachThreadInput(fgThread, me, FALSE);
    if (GetForegroundWindow() != m_hwnd) {
        keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY, 0);
        keybd_event(VK_MENU, 0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);
    }
    logf("show fg_ok=%d", GetForegroundWindow() == m_hwnd);
}

void App::peek() {
    m_peekUntil = GetTickCount64() + PEEK_MS;
    if (m_visible) return;
    reveal(false); // no focus: the user keeps typing where they were
}

void App::hide() {
    if (!m_visible) return;
    m_visible = false;
    m_peeking = false;
    m_generation = m_engine.cancel();
    m_waiting = false;
    logf("hide");
    ImGui::GetIO().ClearInputKeys();
    ImGui::GetIO().ClearInputMouse();
    m_results.clear();
    m_fading = true;
    m_fadeFrom = std::max(0.0f, m_opacity);
    m_hideTick = GetTickCount64();
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) | WS_EX_TRANSPARENT);
}

void App::finishHide() {
    m_fading = false;
    SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE) & ~WS_EX_TRANSPARENT);
    setOpacity(0.0f);
    m_dcomp->Commit();
    ShowWindow(m_hwnd, SW_HIDE);
}

void App::toggle() {
    if (m_visible && !m_peeking && GetForegroundWindow() == m_hwnd) hide();
    else show();
}

// ---------------------------------------------------------------- input / rows ---

// Replacing a request also replaces its editor identity. Clearing focus alone
// lets ImGui's deferred deactivation write the previous text back next frame.
bool App::replaceInput(const std::string& text) {
    if (text.size() >= sizeof(m_input)) return false;
    memcpy(m_input, text.c_str(), text.size() + 1);
    ++m_inputRevision;
    if (ImGui::GetCurrentContext()) ImGui::ClearActiveID();
    return true;
}

void App::refreshRows() {
    m_lastInput = m_input;
    // Keep the displayed snapshot until its replacement arrives. Clearing it
    // here flashes an empty list and briefly shrinks the height target.
    m_armedIdx = -1;
    m_generation = m_peeking ? m_engine.cancel() : m_engine.submit(m_input);
    m_waiting = !m_peeking;
    if (m_peeking) {
        m_hasView = true;
        refreshNotices();
    }
}

void App::refreshNotices() {
    if (m_input[0] != 0 || m_waiting) return;
    // Keep notices after the app rows, without resubmitting the query or
    // resetting an app selection when a notice arrives or is dismissed.
    std::erase_if(m_results, [](const MenuRow& row) { return row.kind == "Notice"; });
    m_notices.forEach([this](const Notice& notice) {
        MenuRow row;
        row.title = notice.title;
        row.subtitle = notice.body;
        row.kind = "Notice";
        row.actionLabel = "Dismiss";
        row.activate = [this, tick = notice.tick](bool) {
            m_notices.dismiss(tick);
            refreshNotices();
        };
        m_results.push_back(std::move(row));
    });
    m_selected = std::clamp(m_selected, 0, std::max(0, (int)m_results.size() - 1));
    m_armedIdx = -1;
    ++m_rowsRevision;
}

void App::refreshUpdateRows() {
    if (m_waiting) return;
    // Update status is shared across queries; repaint its action in place.
    // Never submit input, move selection or reveal the window here.
    m_notices.forEach([this](const Notice& notice) {
        if (notice.key != "updates") return;
        for (auto& row : m_results) {
            if (!row.updateCheck) continue;
            row.title = notice.title;
            row.subtitle = notice.body;
            row.kind = "Result";
        }
    });
}

void App::takeEngineResults() {
    if (auto result = m_engine.takeResult(); result && result->generation == m_generation) {
        m_waiting = false;
        m_hasView = true;
        m_results.clear();
        m_selected = 0;
        m_animSnapList = true;
        m_spanText = result->view.text;
        m_slots = std::move(result->view.slots);
        m_spans = std::move(result->view.spans);
        for (size_t i = 0; i < result->view.rows.size(); ++i) {
            auto row = std::move(result->view.rows[i]);
            if (!row.actionLabel.empty()) {
                row.activate = [this, generation = m_generation, i, index = m_results.size(), title = row.title, kind = row.kind, preserveInput = row.preserveInput](bool stayOpen) {
                    if (!m_engine.execute(generation, i, true)) return;
                    m_pendingAction = PendingAction{generation, index, stayOpen || preserveInput, preserveInput};
                    m_results[index].title = title;
                    m_results[index].kind = kind;
                    m_results[index].subtitle = "Running";
                };
            }
            m_results.push_back(std::move(row));
        }
        ++m_rowsRevision;
        m_armedIdx = -1;
        refreshUpdateRows();
        refreshNotices();
    }
    if (auto done = m_engine.takeCompletion()) {
        auto pending = std::move(m_pendingAction); m_pendingAction.reset();
        if (!done->error.empty()) logf("command: %s", done->error.c_str());
        if (!pending || done->generation != m_generation || pending->generation != done->generation) return;
        if (done->error.empty()) {
            if (pending->preserveInput) {
                if (pending->row < m_results.size()) {
                    auto& row = m_results[pending->row];
                    row.title = row.actionLabel;
                    row.subtitle = "Done";
                    row.kind = "Result";
                    refreshUpdateRows();
                }
                return;
            }
            if (pending->stayOpen) {
                writeCompletion("");
                if (GetForegroundWindow() != m_hwnd) {
                    ImGui::GetIO().ClearInputKeys();
                    ImGui::GetIO().ClearInputMouse();
                    m_focusInput = false;
                }
            }
            else hide();
        } else if (pending->row < m_results.size()) {
            m_results[pending->row].title = m_results[pending->row].actionLabel + " failed";
            m_results[pending->row].subtitle = done->error;
            m_results[pending->row].kind = "Error";
            m_selected = (int)pending->row;
        }
    }
}

// Only the UI owns key handling and confirmation. The engine runs the action
// captured for this exact row and input generation.
void App::execute(int i, bool stayOpen, bool fromEnter) {
    if (m_waiting || m_pendingAction || m_spanText != m_input || i < 0 || i >= (int)m_results.size()) return;
    const MenuRow row = m_results[i]; // activation can replace the rows
    if (!row.activate) { autofill(i); return; }
    if (row.danger && !fromEnter) {
        m_selected = i;
        m_armedIdx = -1;
        return;
    }
    if (row.danger && !(m_armedIdx == i && m_armedRevision == m_rowsRevision)) {
        m_armedIdx = i;
        m_armedRevision = m_rowsRevision;
        return;
    }
    m_armedIdx = -1;
    row.activate(stayOpen);
}

bool App::canFill(int i) const {
    if (m_waiting || m_spanText != m_input) return false;
    if (i < 0 || i >= (int)m_results.size()) return false;
    const std::string& s = m_results[i].completion;
    return !s.empty() && s != m_input && s.size() < sizeof(m_input);
}

// Tab writes the selected completion and restores the caret at the end.
void App::autofill(int i) {
    if (!canFill(i)) return;
    writeCompletion(m_results[i].completion);
}

void App::writeCompletion(const std::string& s) {
    if (!replaceInput(s)) return;
    m_focusInput = true;
    m_caretToEnd = true;
    m_redrawInput = true;
    refreshRows();
}

void App::onNotice(Notice n) {
    logf("notice: %s | %s", n.title.c_str(), n.body.c_str());
    m_notices.push(std::move(n));
    if (!m_visible || m_fading) peek();
    else if (m_peeking) {
        m_peekUntil = GetTickCount64() + PEEK_MS;
        refreshRows();
    } else refreshNotices();
}

// ---------------------------------------------------------------- ui ---

// Layout (logical px, all scaled):
//   query row   INPUT_H   prompt ">" in the icon column, then the input; the
//                         engine's spans are painted over the text (accent noun,
//                         underlined args, heavy verb) and ghost slots follow the
//                         caret naming what is still to type
//   result row  ROW_H     [icon, glyph or digit 18][8][title][8][subtitle] ... [16][context noun]
//   detail row  ROW_H_DETAIL  title above a smaller subtitle; same gutter and right context
//   footer      FOOT_H    "n/N" ... "Enter <verb>" [16] "Tab Fill" [16] "Esc Close"
static int caretCallback(ImGuiInputTextCallbackData* d) {
    if (auto* toEnd = (bool*)d->UserData; *toEnd) {
        d->CursorPos = d->SelectionStart = d->SelectionEnd = d->BufTextLen;
        *toEnd = false;
    }
    return 0;
}

void App::drawUi() {
    const float S = m_scale;
    const float W = m_config.width * S;
    const int MAX_ROWS = m_config.maxRows;
    const float inputH = theme::INPUT_H * S;
    const float rowH = theme::ROW_H * S;
    const float footH = theme::FOOT_H * S;
    const float padX = theme::PAD_X * S;
    const float gapS = theme::GAP_S * S;
    const float gapM = theme::GAP_M * S;
    const float iconSz = theme::ICON_SZ * S;
    const float textX = padX + iconSz + gapS;
    const float H = inputH + 1.0f + (MAX_ROWS + 1) * theme::ROW_H_DETAIL * S + footH; // oversized; the HWND clips

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::Begin("##relay", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float fsSm = theme::FONT_SIZE_SM * S;
    const ImGuiIO& io = ImGui::GetIO();
    auto textY = [&](float top, float h, float size) { return top + (h - size) * 0.5f; };

    // --- query row: prompt, input with painted spans, caret hint
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, inputH), theme::rgb(theme::BG_INPUT));
    {
        const ImVec2 sz = font->CalcTextSizeA(fs, FLT_MAX, 0, ">");
        dl->AddText(ImVec2(padX + (iconSz - sz.x) * 0.5f, textY(0, inputH, fs)), theme::rgb(theme::ACCENT_TEXT), ">");
    }
    const float inputX = textX;
    ImGui::SetCursorPos(ImVec2(inputX, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (inputH - fs) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
    ImGui::SetNextItemWidth(W - padX - inputX);
    if (m_focusInput && !m_peeking) { ImGui::SetKeyboardFocusHere(); m_focusInput = false; }
    ImGui::PushID((int)m_inputRevision);
    ImGui::InputText("##q", m_input, sizeof(m_input), ImGuiInputTextFlags_CallbackAlways, caretCallback, &m_caretToEnd);
    // Enter and clicks outside InputText deactivate it. Relay has one editor:
    // reactivate its retained state without resetting the caret, selection or
    // undo history. Never take focus from another app or an active widget.
    const ImGuiID inputId = ImGui::GetItemID();
    if (!m_peeking && GetForegroundWindow() == m_hwnd && ImGui::GetActiveID() == 0 && ImGui::GetInputTextState(inputId))
        ImGui::SetActiveID(inputId, ImGui::GetCurrentWindow());
    ImGui::PopID();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Handle edits before painting the overlays, so highlighting always belongs
    // to the text in this frame. Older worker results cannot overwrite an edit.
    if (!m_peeking && m_lastInput != m_input) refreshRows();
    takeEngineResults();
    if (!m_visible || m_redrawInput) { ImGui::End(); return; }

    // The sentence is paint over the editable text: the noun re-drawn in the
    // accent, arguments underlined (the one being filled in the accent), verbs
    // heavier, errors in danger with a wavy line. Ghost slots after the caret
    // name what is still to type. Only while results match the text.
    const bool fresh = !m_waiting && m_spanText == m_input;
    const float ty0 = textY(0, inputH, fs);
    const float lineY = ty0 + fs + 1.0f * S;
    auto spanX = [&](size_t at) { return inputX + font->CalcTextSizeA(fs, FLT_MAX, 0, m_input, m_input + at).x; };
    auto heavy = [&](ImVec2 at, unsigned col, const char* b, const char* e = nullptr) {
        dl->AddText(at, theme::rgb(col), b, e);
        dl->AddText(ImVec2(at.x + 0.8f * S, at.y), theme::rgb(col), b, e);
    };
    auto wavy = [&](float x0, float x1, unsigned col) {
        const float amp = 1.0f * S, step = 2.0f * S;
        float x = x0; bool up = false;
        while (x < x1) {
            const float nx = std::min(x + step, x1);
            dl->AddLine(ImVec2(x, lineY + (up ? -amp : amp)), ImVec2(nx, lineY + (up ? amp : -amp)), theme::rgb(col), 1.0f * S);
            x = nx; up = !up;
        }
    };
    if (fresh) for (const TextSpan& sp : m_spans) {
        const float x0 = spanX(sp.begin), x1 = spanX(sp.end);
        const char* b = m_input + sp.begin; const char* e = m_input + sp.end;
        switch (sp.kind) {
        case TextSpan::Noun: dl->AddText(ImVec2(x0, ty0), theme::rgb(theme::ACCENT_TEXT), b, e); break;
        case TextSpan::Argument: dl->AddLine(ImVec2(x0, lineY), ImVec2(x1, lineY), theme::rgb(theme::TEXT_MUTED, 0.5f), 1.0f * S); break;
        case TextSpan::Partial: dl->AddLine(ImVec2(x0, lineY), ImVec2(x1, lineY), theme::rgb(theme::ACCENT), 1.0f * S); break;
        case TextSpan::Verb: heavy(ImVec2(x0, ty0), theme::TEXT, b, e); break;
        case TextSpan::Error: dl->AddText(ImVec2(x0, ty0), theme::rgb(theme::DANGER), b, e); wavy(x0, x1, theme::DANGER); break;
        }
    }
    if (fresh && !m_slots.empty()) {
        const size_t len = strlen(m_input);
        const float space = font->CalcTextSizeA(fs, FLT_MAX, 0, " ").x;
        float x = spanX(len) + (len && m_input[len - 1] != ' ' ? space : 0);
        auto put = [&](const std::string& s, unsigned col, bool bold = false) {
            if (bold) heavy(ImVec2(x, ty0), col, s.c_str());
            else dl->AddText(ImVec2(x, ty0), theme::rgb(col), s.c_str());
            x += font->CalcTextSizeA(fs, FLT_MAX, 0, s.c_str()).x;
        };
        bool next = true; // the first argument slot is the one Tab or typing fills
        for (const Slot& slot : m_slots) {
            if (slot.kind == Slot::Argument) {
                const unsigned col = next ? theme::TEXT_2 : theme::TEXT_MUTED;
                put("⟨" + slot.label, col);
                if (!slot.value.empty()) put(": " + slot.value, theme::TEXT_MUTED);
                put("⟩", col);
                next = false;
            } else put(slot.label, theme::TEXT_MUTED, slot.kind == Slot::Verb);
            x += space;
        }
    } else if (m_input[0] == 0 && !m_peeking) {
        dl->AddText(ImVec2(inputX, ty0), theme::rgb(theme::TEXT_MUTED), "Search apps or type / for commands");
    }

    const int n = (int)m_results.size();
    const bool searching = m_waiting && !m_hasView && m_input[0] != 0;
    const bool noMatch = n == 0 && (searching || (!m_spanText.empty() && m_slots.empty()));
    const int listRows = noMatch ? 1 : std::min(n, MAX_ROWS);
    const MenuLayout layout(m_results, rowH, theme::ROW_H_DETAIL * S);
    // Read Alt once for both activation and presentation. Opening Alt must be
    // released before shortcuts become available, even while the list is empty.
    const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (!altDown) m_altReleased = true;
    const bool altActive = !m_peeking && altDown && m_altReleased;

    // --- keys. Enter runs, Tab fills; Alt+digit runs, Alt+Shift+digit fills.
    if (!m_peeking) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { hide(); ImGui::End(); return; }
        if (n && !m_waiting) {
            const bool next = ImGui::IsKeyPressed(ImGuiKey_DownArrow) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N));
            const bool prev = ImGui::IsKeyPressed(ImGuiKey_UpArrow) || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P));
            if (next || prev) { m_armedIdx = -1; }
            if (next) m_selected = (m_selected + 1) % n;
            if (prev) m_selected = (m_selected + n - 1) % n;
            if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && canFill(m_selected)) { autofill(m_selected); ImGui::End(); return; }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
                execute(m_selected, io.KeyShift, true);
                ImGui::End();
                return;
            }
        }
    }

    // Navigation defines the viewport now; animation only positions its rows.
    // Shortcut labels and execution use this same target, never rounded motion.
    const int firstTarget = std::clamp(m_selected - MAX_ROWS + 1, 0, std::max(0, n - MAX_ROWS));
    const float listHeight = noMatch ? rowH : layout.position(float(firstTarget + listRows)) - layout.position(float(firstTarget));
    const float targetH = inputH + (listRows ? 1.0f + listHeight + footH : 0);
    const int shortcutCount = std::min({9, MAX_ROWS, n - firstTarget});
    if (altActive && !m_waiting) {
        for (int d = 1; d <= shortcutCount; ++d) {
            if (!ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + d - 1), false)) continue;
            const int idx = firstTarget + d - 1;
            if (m_results[idx].danger) continue;
            if (io.KeyShift) autofill(idx);
            else execute(idx, false);
            ImGui::End();
            return;
        }
    }

    // --- motion
    {
        const float dt = std::min(io.DeltaTime, 0.05f);
        auto ease = [dt](float& v, float target, float tau) {
            v += (target - v) * (1.0f - std::exp(-dt / tau));
            if (std::abs(target - v) < 0.002f) v = target;
        };
        m_altBlend = std::clamp(m_altBlend + (altActive ? dt : -dt) / theme::ALT_FADE_S, 0.0f, 1.0f);
        if (m_animSnapList) { m_animScroll = (float)firstTarget; m_animSel = (float)m_selected; m_animSnapList = false; }
        else { ease(m_animScroll, (float)firstTarget, theme::TAU_SELECT); ease(m_animSel, (float)m_selected, theme::TAU_SELECT); }
        // The first frame is prepared while hidden. Do not spend the idle-time
        // delta on the opening animation before the window is even shown.
        if (m_revealFrame) m_revealFrame = false;
        else ease(m_animH, targetH, theme::TAU_HEIGHT);
        if (std::abs(targetH - m_animH) < 0.5f) m_animH = targetH;
    }

    if (m_animH <= inputH + 0.5f) { ImGui::End(); return; }

    dl->AddLine(ImVec2(0, inputH), ImVec2(W, inputH), theme::rgb(theme::BORDER));
    const float listTop = inputH + 1.0f;
    const float footY = std::max(listTop, m_animH - footH);
    const float kindRight = W - padX;
    const bool mouseMoved = io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    const float altAlpha = m_altBlend * m_altBlend * (3.0f - 2.0f * m_altBlend);

    dl->PushClipRect(ImVec2(0, listTop), ImVec2(W, footY), true);

    if (noMatch) {
        dl->AddText(ImVec2(textX, textY(listTop, rowH, fs)), theme::rgb(theme::TEXT_2), searching ? "Searching" : "No matches; edit the text or use / to list commands");
    }

    if (n) {
        const float sy = listTop + layout.position(m_animSel) - layout.position(m_animScroll);
        const float bottom = listTop + layout.position(m_animSel + 1) - layout.position(m_animScroll);
        dl->AddRectFilled(ImVec2(0, sy), ImVec2(W, bottom), theme::rgb(theme::BG_SELECTED));
        dl->AddRectFilled(ImVec2(0, sy), ImVec2(2.0f * S, bottom),
            theme::rgb(m_results[m_selected].danger ? theme::DANGER : theme::ACCENT));
    }

    const int firstIdx = std::max(0, (int)std::floor(m_animScroll));
    for (int idx = firstIdx; idx < n; ++idx) {
        const MenuRow& r = m_results[idx];
        const std::string& title = r.title;
        const std::string& kind = r.kind;
        const float y0 = listTop + layout.position(float(idx)) - layout.position(m_animScroll);
        const float y1 = listTop + layout.position(float(idx + 1)) - layout.position(m_animScroll);
        const float rowHeight = y1 - y0;
        const float titleY = r.stacked ? y0 + gapS : textY(y0, rowHeight, fs);
        if (y0 >= footY) break;
        const bool danger = r.danger;
        // Gutter: the app icon when the row has one, else one glyph from the
        // grammar. "/" opens a command, "›" is the next word (a choice or
        // verb), "=" a result, "!" an error or destructive verb, "•" a notice.
        const char* glyph = nullptr; unsigned glyphCol = theme::TEXT_MUTED;
        if (r.iconKey.empty()) {
            if (kind == "Command") { glyph = "/"; glyphCol = theme::TEXT; }
            else if (kind == "Result") glyph = "=";
            else if (kind == "Error" || danger) { glyph = "!"; glyphCol = theme::DANGER; }
            else if (kind == "Notice") { glyph = "•"; glyphCol = theme::ACCENT_TEXT; }
            else glyph = "›";
        }
        const bool hovered = !m_peeking && !m_waiting && io.MousePos.x >= 0 && io.MousePos.x < W &&
            io.MousePos.y >= std::max(y0, listTop) && io.MousePos.y < y1 && io.MousePos.y < footY;
        if (hovered && mouseMoved && m_selected != idx) { m_selected = idx; m_armedIdx = -1; }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { dl->PopClipRect(); execute(idx, false); ImGui::End(); return; }
        const int digit = idx - firstTarget + 1;
        const float digitAlpha = digit >= 1 && digit <= shortcutCount && !danger ? altAlpha : 0.0f;

        // Keep icon loading/reveal advancing under the shortcut crossfade.
        if (auto* tex = IconCache::instance().get(r.iconKey)) {
            float a = 1.0f, lift = 0.0f;
            if (m_iconPending.erase(r.iconKey)) m_iconReveal[r.iconKey] = (float)ImGui::GetTime();
            if (auto it = m_iconReveal.find(r.iconKey); it != m_iconReveal.end()) {
                const float t = ((float)ImGui::GetTime() - it->second) / theme::ICON_REVEAL_S;
                if (t >= 1.0f) m_iconReveal.erase(it);
                else { a = t; lift = (1.0f - t) * 2.0f * S; }
            }
            const float iy = (r.stacked ? titleY + (fs - iconSz) * 0.5f : y0 + (rowHeight - iconSz) * 0.5f) + lift;
            dl->AddImage((ImTextureID)(intptr_t)tex, ImVec2(padX, iy), ImVec2(padX + iconSz, iy + iconSz),
                         ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, (int)(a * (1.0f - altAlpha) * 255)));
        } else if (!r.iconKey.empty()) {
            m_iconPending.insert(r.iconKey);
        } else if (glyph && altAlpha < 1.0f) {
            const ImVec2 gsz = font->CalcTextSizeA(fs, FLT_MAX, 0, glyph);
            dl->AddText(ImVec2(padX + (iconSz - gsz.x) * 0.5f, titleY), theme::rgb(glyphCol, 1.0f - altAlpha), glyph);
        }
        if (digitAlpha > 0.0f) {
            const char d[2] = { (char)('0' + digit), 0 };
            const ImVec2 dsz = font->CalcTextSizeA(fs, FLT_MAX, 0, d);
            dl->AddText(ImVec2(padX + (iconSz - dsz.x) * 0.5f, titleY), theme::rgb(theme::ACCENT_TEXT, digitAlpha), d);
        }

        // Right edge: the command noun for rows found outside their command.
        float textMaxX = kindRight;
        if (!r.context.empty()) {
            const ImVec2 ctxSz = font->CalcTextSizeA(fsSm, FLT_MAX, 0, r.context.c_str());
            const float ctxX = kindRight - ctxSz.x;
            const float contextY = r.stacked ? titleY + (fs - fsSm) * 0.5f : textY(y0, rowHeight, fsSm);
            dl->AddText(font, fsSm, ImVec2(ctxX, contextY), theme::rgb(theme::TEXT_MUTED), r.context.c_str());
            textMaxX = ctxX - gapM;
        }

        const float ty = titleY;
        const ImVec2 titleSz = font->CalcTextSizeA(fs, FLT_MAX, 0, title.c_str());
        const bool titleFits = textX + titleSz.x <= textMaxX;
        if (danger) ImGui::PushStyleColor(ImGuiCol_Text, theme::rgb(theme::DANGER));
        ImGui::RenderTextEllipsis(dl, ImVec2(textX, ty), ImVec2(textMaxX, y1), textMaxX, title.c_str(), nullptr, &titleSz);
        if (danger) ImGui::PopStyleColor();
        const std::string& subtitle = r.subtitle;
        if (r.stacked && !subtitle.empty()) {
            // PushFont takes a logical size and applies FontScaleDpi itself.
            ImGui::PushFont(font, theme::FONT_SIZE_SM);
            ImGui::PushStyleColor(ImGuiCol_Text, theme::rgb(theme::TEXT_2));
            ImGui::RenderTextEllipsis(dl, ImVec2(textX, ty + fs + gapS), ImVec2(kindRight, y1), kindRight,
                subtitle.c_str(), nullptr, nullptr);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        } else if (titleFits && !subtitle.empty()) {
            const float sx = textX + titleSz.x + gapS;
            if (sx + fs * 3 < textMaxX) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme::rgb(theme::TEXT_2));
                ImGui::RenderTextEllipsis(dl, ImVec2(sx, ty), ImVec2(textMaxX, y1), textMaxX, subtitle.c_str(), nullptr, nullptr);
                ImGui::PopStyleColor();
            }
        }
    }
    dl->PopClipRect();

    // --- footer: count, then what Enter / Tab do for the selected row, then Esc
    dl->PushClipRect(ImVec2(0, listTop), ImVec2(W, m_animH), true);
    dl->AddRectFilled(ImVec2(0, footY), ImVec2(W, m_animH), theme::rgb(theme::BG));
    dl->AddLine(ImVec2(0, footY), ImVec2(W, footY), theme::rgb(theme::BORDER));
    const float fty = textY(footY, footH, fsSm);
    if (n) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d/%d", m_selected + 1, n);
        dl->AddText(font, fsSm, ImVec2(padX, fty), theme::rgb(theme::TEXT_MUTED), buf);
    }
    std::vector<std::string> hints;
    if (m_pendingAction) hints.push_back("Running");
    if (n && m_selected < n) {
        const MenuRow& sel = m_results[m_selected];
        const bool armed = m_armedIdx == m_selected && m_armedRevision == m_rowsRevision;
        if (!m_pendingAction) {
            if (sel.activate) hints.push_back((sel.danger && armed ? "Enter Confirm " : "Enter ") + sel.actionLabel);
            else if (!sel.completion.empty()) hints.push_back("Enter Fill");
        }
        if (!sel.completion.empty() && sel.completion != m_spanText && sel.completion.size() < sizeof(m_input)) hints.push_back("Tab Fill");
    }
    hints.push_back("Esc Close");
    float hx = kindRight;
    for (int i = (int)hints.size() - 1; i >= 0; --i) {
        const ImVec2 hs = font->CalcTextSizeA(fsSm, FLT_MAX, 0, hints[i].c_str());
        hx -= hs.x;
        if (hx < padX + 60.0f * S) break; // out of room: drop the leftmost hints
        dl->AddText(font, fsSm, ImVec2(hx, fty), theme::rgb(theme::TEXT_MUTED), hints[i].c_str());
        hx -= gapM;
    }
    dl->PopClipRect();

    ImGui::End();
}

void App::frame() {
    if (m_peeking && GetTickCount64() >= m_peekUntil) { hide(); return; }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    drawUi();
    ImGui::Render();
    if (m_redrawInput) { m_redrawInput = false; return; }
    if (!m_visible) return; // drawUi may have hidden us

    const int h = std::max(1, (int)std::lround(m_animH)); // swapchain dimensions must stay positive
    const int w = (int)(m_config.width * m_scale);
    if (h != m_winH || w != m_winW) resizeTo(w, h);

    const float bg[4] = { 0x0b / 255.0f, 0x0c / 255.0f, 0x0e / 255.0f, 1.0f };
    m_ctx->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_ctx->ClearRenderTargetView(m_rtv.Get(), bg);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = m_swap->Present(1, 0);
    if (FAILED(hr)) logf("Present failed 0x%08x", (unsigned)hr);
    const float t = (GetTickCount64() - m_showTick) / 1000.0f;
    setOpacity(std::min(1.0f, t / theme::FADE_IN_S));
    m_dcomp->Commit();
}

int App::run() {
    MSG msg{};
    while (m_running) {
        if (!m_visible && m_fading) {
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 8, QS_ALLINPUT);
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) { m_running = false; break; }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!m_running) break;
            if (m_fading && !m_visible) {
                const float t = (GetTickCount64() - m_hideTick) / 1000.0f;
                if (t >= theme::FADE_OUT_S) finishHide();
                else { setOpacity(m_fadeFrom * (1.0f - t / theme::FADE_OUT_S)); m_dcomp->Commit(); }
            }
            continue;
        }
        if (!m_visible) {
            // Idle: block on the queue. Zero CPU, zero GPU.
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { m_running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!m_running) break;
        if (m_visible) frame();
    }
    return 0;
}
