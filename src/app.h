#pragma once
#include "menu_view.h"
#include "config.h"
#include "watcher.h"
#include "notices.h"
#include "engine.h"
#include "updates.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>


class App {
public:
    bool init(HINSTANCE inst);
    int run();
    void shutdown();

    void show();      // activate + focus
    void peek();      // show without taking focus (notifications)
    void hide();
    void toggle();
    void quit();

private:
    // --- window / gpu
    HWND m_hwnd = nullptr;
    bool m_visible = false;
    bool m_peeking = false;
    bool m_running = true;
    float m_scale = 1.0f;
    int m_winW = 0, m_winH = 0;
    int m_swapW = 0, m_swapH = 0; // allocated back buffer size; see resizeTo

    Microsoft::WRL::ComPtr<ID3D11Device> m_dev;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_ctx;
    Microsoft::WRL::ComPtr<IDXGISwapChain> m_swap;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    Microsoft::WRL::ComPtr<IDCompositionDesktopDevice> m_dcomp;
    Microsoft::WRL::ComPtr<IDCompositionTarget> m_dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual2> m_visual;
    float m_opacity = -1.0f;
    unsigned long long m_showTick = 0;
    bool m_fading = false;            // fade-out in progress (window still shown, click-through)
    float m_fadeFrom = 1.0f;
    unsigned long long m_hideTick = 0;
    unsigned long long m_peekUntil = 0;

    // --- UI state
    Config m_config;
    Hotkey m_hotkey;
    DirWatcher m_watcher;
    using Notice = NoticeStore::Notice;
    NoticeStore m_notices;
    Engine m_engine;
    Updates m_updates;
    uint64_t m_generation = 0;
    bool m_waiting = false;
    bool m_hasView = false; // retain even an empty completed view during queries
    struct PendingAction { uint64_t generation; size_t row; bool stayOpen; bool preserveInput; };
    std::optional<PendingAction> m_pendingAction;

    char m_input[512] = {};
    unsigned m_inputRevision = 0; // programmatic replacements start a new editor state
    std::string m_lastInput;
    std::vector<MenuRow> m_results;
    uint64_t m_rowsRevision = 0;
    int m_selected = 0;
    int m_firstVisible = 0; // logical viewport, independent of hover and animation
    float m_wheelRemainder = 0.0f;
    bool m_manualScroll = false;
    bool m_focusInput = false;
    bool m_caretToEnd = false;
    bool m_redrawInput = false;
    std::string m_spanText;
    std::vector<TextSpan> m_spans;
    std::vector<Slot> m_slots;
    int m_armedIdx = -1;
    uint64_t m_armedRevision = 0;
    bool m_altReleased = false;
    float m_altBlend = 0.0f;

    // --- motion (see drawUi)
    float m_animH = 0.0f;
    float m_animSel = 0.0f;
    float m_animScroll = 0.0f;
    bool m_revealFrame = false; // prime the hidden frame at zero height
    bool m_animSnapList = true;
    std::unordered_set<std::string> m_iconPending;
    std::unordered_map<std::string, float> m_iconReveal;

    bool createDevice();
    void destroyDevice();
    void createRtv();
    int maxHeight() const;
    void ensureBuffers(int w, int h);
    void resizeTo(int w, int h);
    void placeWindow();
    void setScale(float s);
    void setOpacity(float a);
    void finishHide();
    void registerHotkey();
    void startWatcher();
    void reveal(bool takeFocus);

    bool replaceInput(const std::string& text);
    void refreshRows();
    void refreshNotices();
    void refreshUpdateRows();
    void takeEngineResults();
    void writeCompletion(const std::string& text);
    void execute(int index, bool stayOpen, bool fromEnter = false);
    bool canFill(int index) const;
    void autofill(int index);
    void onNotice(Notice n);

    void frame();
    void drawUi();

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT, WPARAM, LPARAM);
};
