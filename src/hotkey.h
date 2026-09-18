#pragma once
#include <windows.h>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

struct HotkeyBinding {
    enum class Kind { Shortcut, WinTap };
    Kind kind = Kind::Shortcut;
    UINT modifiers = 0;
    UINT key = 0;
    bool operator==(const HotkeyBinding&) const = default;
    static std::optional<HotkeyBinding> parse(std::string_view text);
};

// Pure input state: a Win tap must start with no other keyboard key held.
// Repeats, chords, injected input and releases without a press never activate.
class WinKeyTap {
public:
    using Keys = std::array<bool, 256>;
    void reset(const Keys& held = {});
    bool key(UINT vk, bool down, bool injected = false);
private:
    Keys m_held{};
    UINT m_candidate = 0;
};

// Owns registration and the input thread. Both binding kinds post the same
// generation-tagged message; lParam is zero for activation, one for input failure.
class Hotkey {
public:
    ~Hotkey() { stop(); }
    std::string start(const HotkeyBinding& binding, HWND target, UINT message);
    void stop();
    bool accepts(WPARAM generation) const { return m_active && generation == m_generation; }
private:
    std::thread m_thread;
    DWORD m_threadId = 0;
    WPARAM m_generation = 0;
    bool m_active = false;
};
