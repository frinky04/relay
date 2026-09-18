#include "hotkey.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <future>

std::optional<HotkeyBinding> HotkeyBinding::parse(std::string_view text) {
    std::string compact;
    for (unsigned char c : text)
        if (!std::isspace(c)) compact += static_cast<char>(std::tolower(c));
    if (compact == "win" || compact == "super") return HotkeyBinding{Kind::WinTap};

    HotkeyBinding result;
    std::string_view remaining = compact;
    while (!remaining.empty()) {
        const auto separator = remaining.find('+');
        const auto token = remaining.substr(0, separator);
        if (token.empty()) return {};
        UINT modifier = 0;
        if (token == "ctrl" || token == "control") modifier = MOD_CONTROL;
        else if (token == "alt") modifier = MOD_ALT;
        else if (token == "shift") modifier = MOD_SHIFT;
        else if (token == "win" || token == "super") modifier = MOD_WIN;
        if (modifier) {
            if (result.modifiers & modifier) return {};
            result.modifiers |= modifier;
        } else {
            if (result.key) return {};
            if (token == "space") result.key = VK_SPACE;
            else if (token == "tab") result.key = VK_TAB;
            else if (token == "enter" || token == "return") result.key = VK_RETURN;
            else if (token == "esc" || token == "escape") result.key = VK_ESCAPE;
            else if (token == "`" || token == "grave" || token == "backtick") result.key = VK_OEM_3;
            else if (token.size() == 1 && std::isalnum(static_cast<unsigned char>(token[0])))
                result.key = static_cast<UINT>(std::toupper(static_cast<unsigned char>(token[0])));
            else if (token.size() >= 2 && token[0] == 'f') {
                unsigned number = 0;
                const auto parsed = std::from_chars(token.data() + 1, token.data() + token.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || number < 1 || number > 24)
                    return {};
                result.key = VK_F1 + number - 1;
            } else return {};
        }
        if (separator == remaining.npos) break;
        remaining.remove_prefix(separator + 1);
        if (remaining.empty()) return {};
    }
    return result.key ? std::optional{result} : std::nullopt;
}

void WinKeyTap::reset(const Keys& held) {
    m_held = held;
    m_candidate = 0;
}

bool WinKeyTap::key(UINT vk, bool down, bool injected) {
    if (vk >= m_held.size()) return false;
    const bool win = vk == VK_LWIN || vk == VK_RWIN;
    const bool activate = win && !down && !injected && m_candidate == vk;
    if (injected || !win) m_candidate = 0;
    else if (down && !m_held[vk])
        m_candidate = std::none_of(m_held.begin(), m_held.end(), [](bool held) { return held; }) ? vk : 0;
    else if (!down) m_candidate = 0;
    m_held[vk] = down;
    return activate;
}

namespace {
constexpr int HOTKEY_ID = 1;
// A marker unique to this process; never consume another program's injection.
const char injectedMarker = 0;
ULONG_PTR injectionTag() { return reinterpret_cast<ULONG_PTR>(&injectedMarker); }

struct HookContext {
    WinKeyTap tap;
    HWND target;
    UINT message;
    WPARAM generation;
};
thread_local HookContext* context = nullptr;

WinKeyTap::Keys heldKeys() {
    WinKeyTap::Keys held{};
    // Hooks report left/right modifiers, not their aggregate virtual keys.
    // Mouse buttons are not keyboard chords.
    for (UINT key = VK_BACK; key < held.size(); ++key)
        if (key != VK_SHIFT && key != VK_CONTROL && key != VK_MENU)
            held[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
    return held;
}

LRESULT CALLBACK keyboardHook(int code, WPARAM message, LPARAM param) {
    if (code != HC_ACTION || !context) return CallNextHookEx(nullptr, code, message, param);
    const auto& event = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(param);
    if ((event.flags & LLKHF_INJECTED) && event.dwExtraInfo == injectionTag())
        return CallNextHookEx(nullptr, code, message, param);

    const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    const bool injected = (event.flags & LLKHF_INJECTED) != 0;
    const bool win = event.vkCode == VK_LWIN || event.vkCode == VK_RWIN;
    // Async state is still BEFORE this event. Refresh at each fresh Win press
    // to recover from releases missed on the lock screen or another desktop.
    if (win && down && !(GetAsyncKeyState(event.vkCode) & 0x8000))
        context->tap.reset(heldKeys());

    if (!context->tap.key(event.vkCode, down, injected))
        return CallNextHookEx(nullptr, code, message, param);

    // Windows has already seen Win-down, so chords work without replay. For a
    // standalone release, mask Start before releasing Win in one ordered batch.
    // 0xE8 is an unassigned virtual key, used solely to mask the shell's menu.
    INPUT input[3]{};
    for (auto& item : input) {
        item.type = INPUT_KEYBOARD;
        item.ki.dwExtraInfo = injectionTag();
    }
    input[0].ki.wVk = input[1].ki.wVk = 0xE8;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    input[2].ki.wVk = static_cast<WORD>(event.vkCode);
    input[2].ki.wScan = static_cast<WORD>(event.scanCode);
    input[2].ki.dwFlags = KEYEVENTF_KEYUP | ((event.flags & LLKHF_EXTENDED) ? KEYEVENTF_EXTENDEDKEY : 0);
    const UINT sent = SendInput(3, input, sizeof(INPUT));
    if (sent == 3) {
        PostMessageW(context->target, context->message, context->generation, 0);
        return 1;
    }
    // UIPI can reject injection over an elevated app. Never swallow the real
    // release on failure or leave a modifier down, and never open both launchers.
    if (sent == 1) SendInput(1, &input[1], sizeof(INPUT));
    PostMessageW(context->target, context->message, context->generation, 1);
    return CallNextHookEx(nullptr, code, message, param);
}
}

std::string Hotkey::start(const HotkeyBinding& binding, HWND target, UINT message) {
    stop();
    const WPARAM generation = m_generation;
    std::promise<DWORD> ready;
    auto result = ready.get_future();
    m_thread = std::thread([binding, target, message, generation, ready = std::move(ready)]() mutable {
        MSG msg{};
        PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE); // create queue before publishing thread id
        HookContext hookContext{{}, target, message, generation};
        context = &hookContext;
        HHOOK hook = nullptr;
        bool registered = false;
        if (binding.kind == HotkeyBinding::Kind::WinTap) {
            hookContext.tap.reset(heldKeys());
            hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardHook, GetModuleHandleW(nullptr), 0);
        } else {
            registered = RegisterHotKey(nullptr, HOTKEY_ID, binding.modifiers | MOD_NOREPEAT, binding.key) != FALSE;
        }
        ready.set_value((hook || registered) ? GetCurrentThreadId() : 0);
        if (hook || registered) {
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (registered && msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID)
                    PostMessageW(target, message, generation, 0);
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (hook) UnhookWindowsHookEx(hook);
        if (registered) UnregisterHotKey(nullptr, HOTKEY_ID);
        context = nullptr;
    });
    m_threadId = result.get();
    m_active = m_threadId != 0;
    if (m_active) return {};
    m_thread.join();
    return binding.kind == HotkeyBinding::Kind::WinTap
        ? "Unable to install the Windows key hook; restart Relay or set hotkey to alt+space and save init.lua"
        : "Unable to register the hotkey; close the app that uses it or choose another hotkey, then save init.lua";
}

void Hotkey::stop() {
    m_active = false;
    ++m_generation;
    if (m_thread.joinable()) {
        if (m_threadId) PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
        m_thread.join();
    }
    m_threadId = 0;
}
