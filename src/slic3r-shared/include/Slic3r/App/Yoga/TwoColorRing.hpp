#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {
class TwoColorRing : public Item
{
public:
    void render(const Vec2f& pos, const Vec2f& size) override;

    std::optional<ImColor> primary_color{};
    std::optional<ImColor> secondary_color{};
};

} // namespace Slic3r::App::Yoga
