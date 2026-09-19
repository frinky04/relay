#pragma once
#include "menu_view.h"
#include <algorithm>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

// Keep the viewport stable unless keyboard navigation crosses one of its edges.
// Hover only changes selection; result/config changes still clamp the viewport.
inline int menuViewportStart(int first, int selected, int count, int visibleRows, bool revealSelection) {
    const int lastStart = std::max(0, count - visibleRows);
    first = std::clamp(first, 0, lastStart);
    if (revealSelection) {
        if (selected < first) first = selected;
        else if (selected >= first + visibleRows) first = selected - visibleRows + 1;
    }
    return std::clamp(first, 0, lastStart);
}

// Accumulate high-resolution wheel input without storing excess motion at an edge.
inline int menuWheelStart(int first, int count, int visibleRows, float deltaRows, float& remainder) {
    const int last = std::max(0, count - visibleRows);
    first = std::clamp(first, 0, last);
    if (deltaRows * remainder < 0) remainder = 0;
    const float target = std::clamp(float(first) + remainder + deltaRows, 0.0f, float(last));
    const int next = first + int(std::trunc(target - first));
    remainder = target - next;
    return next;
}

inline int menuNavigationSelection(int selected, int count, int visibleRows, int direction,
                                   bool page, bool reenter, int first, int last) {
    if (count == 0) return 0;
    if (reenter && (selected < first || selected > last)) return direction > 0 ? first : last;
    if (page) return std::clamp(selected + direction * std::max(1, visibleRows - 1), 0, count - 1);
    return (selected + direction + count) % count;
}

// Map logical row indices (including animated fractions) to vertical positions.
// Navigation and Alt shortcuts continue to count rows, independent of their height.
class MenuLayout {
public:
    MenuLayout(std::span<const MenuRow> rows, float compactHeight, float detailHeight) : m_offsets{0.0f} {
        m_offsets.reserve(rows.size() + 1);
        for (const auto& row : rows)
            m_offsets.push_back(m_offsets.back() + (row.stacked ? detailHeight : compactHeight));
    }

    float position(float row) const {
        row = std::clamp(row, 0.0f, float(m_offsets.size() - 1));
        const auto index = size_t(row);
        if (index + 1 == m_offsets.size()) return m_offsets.back();
        return m_offsets[index] + (row - float(index)) * (m_offsets[index + 1] - m_offsets[index]);
    }

    // Fully visible rows in the painted viewport, for re-entry after manual scrolling.
    std::pair<int, int> visibleRows(float scroll, float height) const {
        const int count = int(m_offsets.size()) - 1;
        if (!count) return {0, 0};
        const float top = position(scroll);
        const int first = std::min(count - 1, int(std::lower_bound(
            m_offsets.begin(), m_offsets.end(), top - 0.01f) - m_offsets.begin()));
        const int last = int(std::upper_bound(m_offsets.begin(), m_offsets.end(),
            top + std::max(0.0f, height) + 0.01f) - m_offsets.begin()) - 2;
        return {first, std::clamp(last, first, count - 1)};
    }

private:
    std::vector<float> m_offsets;
};
