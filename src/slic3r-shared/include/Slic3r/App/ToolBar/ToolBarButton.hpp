#pragma once

#include "Slic3r/App/Yoga/LayoutButton.hpp"

namespace Slic3r::App::Yoga {
class ContextPopup;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {
class Dialog;

class ToolBarButton : public Yoga::LayoutButton
{
public:
    ToolBarButton(Render::Icon icon, const std::string& tooltip = {});

    void render(const Yoga::Vec2f& pos, const Yoga::Vec2f& size) override;

    void style_node() override;

    Yoga::ContextPopup* get_subtoolbar() const;
    Yoga::ContextPopup* get_or_create_subtoolbar();
    Dialog* dialog() const;

private:
    Yoga::ContextPopup* m_subtoolbar = nullptr;
};

} // namespace Slic3r::App
