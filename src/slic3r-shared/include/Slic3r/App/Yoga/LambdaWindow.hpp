#pragma once

#include "Slic3r/App/Yoga/Window.hpp"

namespace Slic3r::App::Yoga {

class LambdaItem;

/**
 * @deprecated use Window instead
 */
class LambdaWindow : public Window
{
public:
    explicit LambdaWindow(RenderPosFn render_fn, const std::string& prefix);
    ~LambdaWindow();

    void render_body(const Vec2f& pos, const Vec2f& size) override;

private:
    LambdaItem* m_lambda_item = nullptr;
};

}
