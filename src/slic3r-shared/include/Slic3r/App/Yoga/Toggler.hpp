#pragma once

#include "Slic3r/App/Yoga/Oval.hpp"

namespace Slic3r::App::Yoga {

class Circle;

class Toggler : public Oval
{
public:
    Toggler();

    void set_checked(bool checked);

    bool third_state() const;
    void set_third_state(bool third_state);

    bool hovered() const;
    void set_hovered(bool hovered);

protected:
    void update_contents();
    void update_color();
    Platform::ColorGroup button_bg_color_group() const;
    Platform::ColorGroup button_color_group() const;

    void enabled_updated_internal() override;

private:
    bool m_third_state = false;
    bool m_checked     = false;
    bool m_hovered     = false;
    Circle* m_knob{nullptr};
    Oval* m_inner_oval{nullptr};
};

} // namespace Slic3r::App::Yoga
