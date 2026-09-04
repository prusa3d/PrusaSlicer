#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

#include <string>

namespace Slic3r::App::ColorMix {

/**
 * @brief Clickable tile of one offered blend: its mixed color and a short "1+2" label.
 *
 * Blends that are already in the list get a dashed accent ring.
 */
class BlendRecipeTile : public Yoga::AbstractButton
{
public:
    BlendRecipeTile(const ImColor& blend_color, const std::string& label, bool already_used);

    bool already_used() const;

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

private:
    void size_info_changed(const Yoga::SizeInfo& info_size) override;

    ImColor m_blend_color{0x80, 0x80, 0x80};
    std::string m_label;
    bool m_already_used{false};

    Yoga::EvaluatedUnit m_tile_inset;
    Yoga::EvaluatedUnit m_tile_rounding;
    Yoga::EvaluatedUnit m_ring_rounding;
    Yoga::EvaluatedUnit m_ring_thickness;
    Yoga::EvaluatedUnit m_border_thickness;
    Yoga::EvaluatedUnit m_dash_length;
    Yoga::EvaluatedUnit m_dash_gap;
    Yoga::EvaluatedUnit m_text_pad;
};

} // namespace Slic3r::App::ColorMix
