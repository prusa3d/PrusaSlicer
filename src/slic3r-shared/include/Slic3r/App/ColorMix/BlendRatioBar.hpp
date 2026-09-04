#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

#include <functional>

namespace Slic3r::App::ColorMix {

/**
 * @brief Ratio bar of a two-component blend.
 *
 * A horizontal color transition between the two physical extruders with a draggable
 * handle. The ratio is snapped to five percent steps.
 */
class BlendRatioBar : public Yoga::AbstractButton
{
public:
    struct Callbacks
    {
        std::function<void(int ratio_a_percent)> ratio_changed{nullptr};
    };

    BlendRatioBar();

    Callbacks& callbacks();

    void set_colors(const ImColor& color_a, const ImColor& color_b);

    /**
     * @brief Ratio of the first component. Clamped and snapped to the offered steps.
     */
    void set_ratio(int ratio);

    [[nodiscard]] int ratio() const
    {
        return m_ratio;
    }

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    void size_info_changed(const Yoga::SizeInfo& info_size) override;

    [[nodiscard]] float marker_x_for_ratio(float width) const;
    bool update_from_x(float width, float local_x);

    ImColor m_color_a{0xC0, 0xC0, 0xC0};
    ImColor m_color_b{0x80, 0x80, 0x80};
    int m_ratio{50};
    bool m_dragging{false};

    Yoga::EvaluatedUnit m_handle_overhang;
    Yoga::EvaluatedUnit m_handle_width;
    Yoga::EvaluatedUnit m_handle_rounding;
    Yoga::EvaluatedUnit m_handle_shadow_offset;
    Yoga::EvaluatedUnit m_bar_rounding;
    Yoga::EvaluatedUnit m_border_thickness;

    Callbacks m_callbacks;
};

} // namespace Slic3r::App::ColorMix
