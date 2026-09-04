#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

#include <vector>

namespace Slic3r::App::ColorMix {

/**
 * @brief Preview of the repeating layer cycle of a virtual extruder recipe.
 *
 * One colored cell per printed layer, the cycle repeated until the bar is filled.
 */
class LayerSequenceBar : public Yoga::AbstractButton
{
public:
    LayerSequenceBar();

    /**
     * @brief One cell per printed layer. The cycle repeats until the bar is filled.
     */
    void set_cycle(std::vector<ImColor> cycle);

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    void size_info_changed(const Yoga::SizeInfo& info_size) override;

    std::vector<ImColor> m_cycle;

    Yoga::EvaluatedUnit m_cell_gap;
    Yoga::EvaluatedUnit m_bar_rounding;
    Yoga::EvaluatedUnit m_border_thickness;
};

} // namespace Slic3r::App::ColorMix
