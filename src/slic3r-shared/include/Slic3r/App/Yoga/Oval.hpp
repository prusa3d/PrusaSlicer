#pragma once

#include "Slic3r/App/Yoga/Rectangle.hpp"

namespace Slic3r::App::Yoga {

class Oval : public Rectangle {
public:
    Oval();

    void render(const Vec2f& pos, const Vec2f& size) override;
};

} // namespace Slic3r::App::Yoga
