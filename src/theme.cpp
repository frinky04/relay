// Contrast (WCAG, measured):
//   TEXT       #dee2e5  on BG 15.02  on BG_INPUT 14.17  on BG_SELECTED 12.43
//   TEXT_2     #9c9fa2  on BG  7.36  on BG_INPUT  6.94  on BG_SELECTED  6.09
//   TEXT_MUTED #898c90  on BG  5.79  on BG_INPUT  5.47  on BG_SELECTED  4.79
//   ACCENT     #7fa7cf  on BG  7.76  on BG_INPUT  7.32  on BG_SELECTED  6.42
//   ACCENT_TEXT #94bce4 on BG  9.85  on BG_INPUT  9.30  on BG_SELECTED  8.15
//   DANGER     #ed756e  on BG  6.87  on BG_INPUT  6.48  on BG_SELECTED  5.68
//   BORDER vs BG 1.44, BG_SELECTED vs BG 1.21
// Detail rows: primary title uses TEXT on BG (15.02) / BG_SELECTED (12.43);
// secondary line uses TEXT_2 on BG (7.36) / BG_SELECTED (6.09), at FONT_SIZE_SM.
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
