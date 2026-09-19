#pragma once
#include <imgui.h>

// Monochrome palette. Every rendered pair measured, see theme.cpp header comment.
namespace theme {

inline ImU32 rgb(unsigned hex, float a = 1.0f) {
    return IM_COL32((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff, (int)(a * 255));
}

// Semantic tokens.
constexpr unsigned BG            = 0x0c0c0c; // window background
constexpr unsigned BG_INPUT      = 0x141414; // elevated query field and footer surface
constexpr unsigned BG_SELECTED   = 0x242424; // selected row
constexpr unsigned BORDER        = 0x303030; // window edge, scrollbar, swatch checker
constexpr unsigned DIVIDER       = 0x242424; // subtle internal surface separators
constexpr unsigned TEXT          = 0xe2e2e2; // primary text        (15.1:1 on BG)
constexpr unsigned TEXT_2        = 0xa0a0a0; // secondary text      (7.5:1 on BG)
constexpr unsigned TEXT_MUTED    = 0x909090; // hints, kind tags    (6.1:1 on BG, 4.9:1 on BG_SELECTED)
constexpr unsigned ACCENT        = 0xb0b0b0; // partial-match underlines and text selection
constexpr unsigned ACCENT_TEXT   = 0xd0d0d0; // lighter for antialiased text and glyphs to retain the accent's brightness
constexpr unsigned DANGER        = 0xf0f0f0; // destructive verbs (17.2:1 on BG, 13.6:1 on BG_SELECTED)

// Layout metrics (logical px, scaled by DPI at runtime). Width and row count come from config.
constexpr float INPUT_H       = 44.0f;
constexpr float ROW_H         = 34.0f;
constexpr float FOOT_H        = 27.0f;
constexpr float PAD_X         = 12.0f;
constexpr float GAP_S         = 8.0f;   // within a group: icon|title, title|subtitle
constexpr float GAP_M         = 16.0f;  // between groups: text|kind tag, footer items
constexpr float ICON_SZ       = 18.0f;
constexpr float FONT_SIZE     = 18.0f;  // titles, query
constexpr float FONT_SIZE_SM  = 15.0f;  // kind tags, footer
constexpr float ROW_H_DETAIL  = FONT_SIZE + FONT_SIZE_SM + 3 * GAP_S; // two lines, gap and vertical padding

// Motion. Time constants (seconds) for exponential smoothing. 0.035 reads as
// instant-but-alive; above 0.08 starts to read as lag.
constexpr float TAU_SELECT    = 0.035f; // selection highlight + list scroll
constexpr float TAU_HEIGHT    = 0.040f; // window height following content
constexpr float FADE_IN_S     = 0.100f; // show fade, compositor-side opacity ramp
constexpr float FADE_OUT_S    = 0.100f; // hide fade, compositor-side opacity ramp
constexpr float ICON_REVEAL_S = 0.080f; // late-arriving icon: opacity 0..1 + 2px lift
constexpr float ALT_FADE_S    = 0.050f; // icon/shortcut crossfade; input remains immediate

void apply(float scale);

} // namespace theme
