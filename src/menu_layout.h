#pragma once
#include "menu_view.h"
#include <algorithm>
#include <span>
#include <vector>

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

private:
    std::vector<float> m_offsets;
};
