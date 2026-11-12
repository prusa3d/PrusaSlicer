#pragma once

#include "Slic3r/App/Yoga/Rectangle.hpp"

namespace Slic3r::App::Yoga {

class Circle : public Yoga::Rectangle
{
public:
    Circle();

private:
    void render(const Vec2f& pos, const Vec2f& size) override;
};

} // namespace Slic3r::App::Yoga
