#pragma once
#include <imgui.h>

// Palette. Primitives are OKLCH-derived (neutral hue 250, chroma 0.006;
// accent hue 250, matching the neutral). Every rendered pair measured, see theme.cpp header comment.
namespace theme {

inline ImU32 rgb(unsigned hex, float a = 1.0f) {
    return IM_COL32((hex >> 16) & 0xff, (hex >> 8) & 0xff, hex & 0xff, (int)(a * 255));
}

// Semantic tokens.
constexpr unsigned BG            = 0x0b0c0e; // window background
constexpr unsigned BG_INPUT      = 0x121416; // query field surface
constexpr unsigned BG_SELECTED   = 0x1e2123; // selected row
constexpr unsigned BORDER        = 0x2c2e31; // separators, window edge
constexpr unsigned TEXT          = 0xdee2e5; // primary text        (15.0:1 on BG)
constexpr unsigned TEXT_2        = 0x9c9fa2; // secondary text      (7.4:1 on BG)
constexpr unsigned TEXT_MUTED    = 0x898c90; // hints, kind tags    (5.8:1 on BG, 4.8:1 on BG_SELECTED)
constexpr unsigned ACCENT        = 0x7fa7cf; // solid selection marker and underlines
constexpr unsigned ACCENT_TEXT   = 0x94bce4; // lighter for antialiased text and glyphs to retain the accent's brightness
constexpr unsigned DANGER        = 0xed756e; // destructive verbs (6.9:1 on BG, 5.7:1 on BG_SELECTED)

// Layout metrics (logical px, scaled by DPI at runtime). Width and row count come from config.
constexpr float INPUT_H       = 40.0f;
constexpr float ROW_H         = 30.0f;
constexpr float FOOT_H        = 24.0f;
constexpr float PAD_X         = 12.0f;
constexpr float GAP_S         = 8.0f;   // within a group: icon|title, title|subtitle
constexpr float GAP_M         = 16.0f;  // between groups: text|kind tag, footer items
constexpr float ICON_SZ       = 18.0f;
constexpr float FONT_SIZE     = 14.0f;  // titles, query
constexpr float FONT_SIZE_SM  = 12.0f;  // kind tags, footer
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
