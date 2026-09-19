// Contrast (WCAG, measured):
//   TEXT       #e2e2e2  on BG 15.10  on BG_INPUT 14.22  on BG_SELECTED 11.98
//   TEXT_2     #a0a0a0  on BG 7.48  on BG_INPUT 7.04  on BG_SELECTED 5.94
//   TEXT_MUTED #909090  on BG 6.13  on BG_INPUT 5.77  on BG_SELECTED 4.86
//   ACCENT     #b0b0b0  on BG 9.02  on BG_INPUT 8.49  on BG_SELECTED 7.16
//   ACCENT_TEXT#d0d0d0  on BG 12.68  on BG_INPUT 11.94  on BG_SELECTED 10.06
//   DANGER     #f0f0f0  on BG 17.17  on BG_INPUT 16.17  on BG_SELECTED 13.62
//   BORDER vs BG 1.48, BG_SELECTED vs BG 1.26
//   DIVIDER vs BG 1.26, vs BG_INPUT 1.19, vs BG_SELECTED 1.00 (no text).
// Footer: keys use TEXT on BG_INPUT (14.22); count/actions use TEXT_MUTED (5.77).
// Detail rows: primary title uses TEXT on BG (15.10) / BG_SELECTED (11.98);
// secondary line uses TEXT_2 on BG (7.48) / BG_SELECTED (5.94), at FONT_SIZE_SM.
// Errors/destructive actions use DANGER; existing ! glyphs, error underlines and
// confirmation hints retain their meaning without hue.
// Color swatches occupy the icon gutter and carry no text. Their checker uses
// BG_INPUT/BORDER and their outline uses TEXT_MUTED; all text/background pairs stay as above.
#include "theme.h"

namespace theme {

static ImVec4 v4(unsigned hex, float a = 1.0f) {
    return ImVec4(((hex >> 16) & 0xff) / 255.0f, ((hex >> 8) & 0xff) / 255.0f, (hex & 0xff) / 255.0f, a);
}

void apply(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle(); // reset before scaling
    s.FontSizeBase      = FONT_SIZE;
    s.FontScaleDpi      = scale;
    s.WindowPadding     = ImVec2(0, 0);
    s.WindowBorderSize  = 0.0f; // DWM draws the 1px border (DWMWA_BORDER_COLOR); no second one
    s.WindowRounding    = 0.0f;
    s.FramePadding      = ImVec2(PAD_X, 0);
    s.FrameRounding     = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = ImVec2(0, 0);
    s.ScrollbarSize     = 4.0f;
    s.ScrollbarRounding = 0.0f;
    s.ScaleAllSizes(scale);

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]        = v4(BG);
    c[ImGuiCol_Border]          = v4(BORDER);
    c[ImGuiCol_FrameBg]         = v4(BG_INPUT);
    c[ImGuiCol_FrameBgHovered]  = v4(BG_INPUT);
    c[ImGuiCol_FrameBgActive]   = v4(BG_INPUT);
    c[ImGuiCol_Text]            = v4(TEXT);
    c[ImGuiCol_TextDisabled]    = v4(TEXT_MUTED);
    c[ImGuiCol_TextSelectedBg]  = v4(ACCENT, 0.30f);
    c[ImGuiCol_ScrollbarBg]     = v4(BG);
    c[ImGuiCol_ScrollbarGrab]   = v4(BORDER);
    c[ImGuiCol_ScrollbarGrabHovered] = v4(TEXT_MUTED);
    c[ImGuiCol_ScrollbarGrabActive]  = v4(TEXT_MUTED);
    c[ImGuiCol_NavCursor]       = v4(0, 0.0f);
}

} // namespace theme
