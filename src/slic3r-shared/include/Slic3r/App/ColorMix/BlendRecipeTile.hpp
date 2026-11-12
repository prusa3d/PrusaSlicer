#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/Biz/Algorithms/VirtualExtruderPresets.hpp"
#include "Slic3r/Biz/DataObserver.hpp"

#include <functional>
#include <optional>
#include <string>

namespace Slic3r::App::Yoga {
class Rectangle;
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::ColorMix {

struct BlendTileData
{
    Biz::Algorithms::VirtualExtruderPresets::BlendPreset preset;
    ImColor color{0x80, 0x80, 0x80};
    std::optional<Biz::Algorithms::VirtualExtruderPresets::PerceptualColor> perceptual_color;
    std::string label;
    std::string tooltip;
    size_t candidate_index{0};
    bool already_used{false};
};

/**
 * @brief Clickable tile of one offered blend: its mixed color and a short "1+2" label.
 *
 * Blends that are already in the list get a dashed accent ring.
 */
class BlendRecipeTile : public Yoga::RectangleButton, public Biz::DataObserver<BlendTileData>
{
public:
    using FnIndexClicked = std::function<void(size_t)>;

    BlendRecipeTile(size_t index, const BlendTileData& data, FnIndexClicked on_clicked);

protected:
    void on_data_update() override;

private:
    Yoga::Rectangle* m_tile{nullptr};
    Yoga::Text* m_label{nullptr};

    FnIndexClicked m_on_clicked;
};

} // namespace Slic3r::App::ColorMix
