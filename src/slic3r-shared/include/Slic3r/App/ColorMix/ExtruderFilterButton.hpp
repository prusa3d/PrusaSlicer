#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::ColorMix {

/**
 * @brief Toggle button of one physical extruder that filters which blends are offered.
 */
class ExtruderFilterButton : public Yoga::AbstractButton
{
public:
    ExtruderFilterButton(unsigned int extruder_id_1based, const ImColor& color);

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    void size_info_changed(const Yoga::SizeInfo& info_size) override;

    unsigned int m_extruder_id_1based{1};
    ImColor m_color{0x80, 0x80, 0x80};

    Yoga::EvaluatedUnit m_swatch_inset;
    Yoga::EvaluatedUnit m_swatch_rounding;
    Yoga::EvaluatedUnit m_cross_inset;
    Yoga::EvaluatedUnit m_cross_thickness;
    Yoga::EvaluatedUnit m_border_thickness;
    Yoga::EvaluatedUnit m_dash_length;
    Yoga::EvaluatedUnit m_dash_gap;
    Yoga::EvaluatedUnit m_hover_ring_inset;
    Yoga::EvaluatedUnit m_hover_ring_thickness;
};

} // namespace Slic3r::App::ColorMix
