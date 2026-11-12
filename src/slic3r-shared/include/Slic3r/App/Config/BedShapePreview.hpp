#pragma once

#include "Slic3r/Domain/Types.hpp"

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App {

class BedShapePreview : public Yoga::Item
{
public:
    explicit BedShapePreview();

    void set_shape(
        const std::vector<Domain::Vec2d>& points,
        const std::vector<Domain::Vec2d>& triangles,
        const Domain::Vec2d& orig_pos
    );

    const ImColor& shape_fill() const;
    void set_shape_fill(const ImColor& fill);

private:
    void render(const Domain::Vec2f& pos, const Domain::Vec2f& size) override;

private:
    ImColor m_shape_fill           = IM_COL32_WHITE;
    ImColor m_fill                 = IM_COL32_WHITE;
    ImColor m_border_fill          = IM_COL32_WHITE;
    ImColor m_disabled_fill        = IM_COL32_WHITE;
    ImColor m_disabled_border_fill = IM_COL32_WHITE;

    std::vector<Domain::Vec2d> m_points;
    std::vector<Domain::Vec2d> m_triangles;

    Domain::Vec2d m_orig_pos{Domain::Vec2d::Zero()};
};

} // namespace Slic3r::App
