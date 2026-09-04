#pragma once

#include "Slic3r/App/Yoga/Rectangle.hpp"

namespace Slic3r::App::Yoga {
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App::Plater {

class KeyIcon : public Yoga::Rectangle
{
public:
    explicit KeyIcon(const std::string& label);

    void set_tint(const ImColor& color);

private:
    Yoga::Text* m_text{nullptr};
};

} // namespace Slic3r::App::Plater
