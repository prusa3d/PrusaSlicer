#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {

class LambdaItem : public Item {
public:
    LambdaItem(RenderPosFn render_fn);

    void render(const Vec2f& pos, const Vec2f& size) override;

private:
    RenderPosFn m_render_fn;
};

}
