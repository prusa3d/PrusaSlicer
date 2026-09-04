#pragma once

#include "Slic3r/Domain/Types.hpp"

#include <cstddef>

namespace Slic3r::App::Render {


/**
 * Screen resolution info including conversion between logical (scaled) and physical (real) pixels.
 */
class ScreenInfo {
public:
    /**
     * Creates screen info
     * @param width Width in physical pixels (without applied scale)
     * @param height Height in physical pixels (without applied scale)
     * @param scale The UI scale which applied to logical pixel size gains physical one.
     * @param dpi Actual DPI as reported by the platform (0 = unknown, caller will fall back to 96*scale)
     * @param root_font_size base font size in pixels
     */
    ScreenInfo(
        size_t width,
        size_t height,
        float scale,
        int dpi                 = 0,
        float root_font_size    = 0,
        float root_font_size_pt = 0
    ) :
        m_width(width),
        m_height(height),
        m_scale(scale),
        m_dpi(dpi),
        m_root_font_size{root_font_size},
        m_root_font_size_pt{root_font_size_pt}
    {}

    ScreenInfo(const ScreenInfo& other) = default;
    ScreenInfo(ScreenInfo&& other) = default;

    ScreenInfo& operator=(const ScreenInfo& other) = default;
    ScreenInfo& operator=(ScreenInfo&& other) = default;

    size_t physical_width() const { return m_width; }
    size_t physical_height() const { return m_height; }
    float logical_width() const { return m_width / m_scale; }
    float logical_height() const { return m_height / m_scale; }
    float scale() const { return m_scale; }
    int dpi() const { return m_dpi; }
    float root_font_size() const { return m_root_font_size; }
    float root_font_size_pt() const { return m_root_font_size_pt; }

    float logical_to_physical(float logical_coord) const { return logical_coord * m_scale; }
    float physical_to_logical(float physical_coord) const { return physical_coord / m_scale; }

    float physical_to_imgui_x(float physical_x) const { return physical_to_logical(physical_x); }
    float physical_to_imgui_y(float physical_y) const { return physical_to_logical(physical_height() - physical_y - 1); }

    float mouse_to_screen(float mouse_coord) const { return logical_to_physical(mouse_coord); }

    bool operator==(const ScreenInfo& rhs) const
    {
        return m_width == rhs.m_width
            && m_height == rhs.m_height
            && Domain::fuzzy_compare(m_scale, rhs.m_scale)
            && m_dpi == rhs.m_dpi
            && Domain::fuzzy_compare(m_root_font_size, rhs.m_root_font_size)
            && Domain::fuzzy_compare(m_root_font_size_pt, rhs.m_root_font_size_pt);
    }

    bool operator!=(const ScreenInfo& rhs) const
    {
        return !(*this == rhs);
    }

private:
    size_t m_width;
    size_t m_height;
    float m_scale;
    int m_dpi{0};
    float m_root_font_size{0};
    float m_root_font_size_pt{0};
};

}
