#pragma once

#include "Slic3r/App/Yoga/RectangleButton.hpp"

namespace Slic3r::App::Plater {

class HeightRangeButton : public Yoga::RectangleButton
{
public:
    HeightRangeButton();

    void set_range_label(const std::string& range);
    void set_height_label(const std::string& height);
    void set_highlighted(bool highlighted);

private:
    void update_colors();

private:
    Yoga::Text* m_range_label{nullptr};
    Yoga::Text* m_height_label{nullptr};

    bool m_highlighted{false};
};

} // namespace Slic3r::App::Plater
