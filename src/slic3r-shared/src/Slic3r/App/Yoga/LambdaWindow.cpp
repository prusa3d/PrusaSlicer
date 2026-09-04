#include "Slic3r/App/Yoga/LambdaWindow.hpp"

#include "Slic3r/App/Yoga/LambdaItem.hpp"

namespace Slic3r::App::Yoga {

LambdaWindow::LambdaWindow(RenderPosFn render_fn, const std::string& prefix)
    : Window(prefix)
{
    set_object_name("LambdaWindow");
    m_lambda_item = emplace_back<LambdaItem>(render_fn);
}

LambdaWindow::~LambdaWindow() { delete m_lambda_item; }

void LambdaWindow::render_body(const Vec2f& pos, const Vec2f& size) {}

} // namespace Slic3r::App::Yoga
