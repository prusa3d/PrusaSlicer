#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"

namespace Slic3r::App::Yoga {

class Icon;

/**
 * @brief A small checkable circular button that shows its checked state via a border
 * instead of a solid fill, optionally hosting a single centered icon.
 */
class CircleBorderedButton : public RectangleButton
{
public:
    explicit CircleBorderedButton();

    void set_icon(Render::Icon icon);
    void set_icon_tint(const ImColor& tint);

protected:
    void checked_updated_internal() override;

private:
    Icon* m_icon{nullptr};
};

} // namespace Slic3r::App::Yoga
