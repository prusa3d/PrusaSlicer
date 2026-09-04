#pragma once

#include "Slic3r/App/Yoga/AbstractButton.hpp"

#include <imgui.h>

namespace Slic3r::App::Yoga {

class SpinButton : public AbstractButton
{
public:
    explicit SpinButton(ImGuiDir dir);

    void render(const Vec2f& pos, const Vec2f& size) override;
    Vec2f get_item_size() override;

private:
    ImGuiDir m_dir{ImGuiDir_None};
};

} // namespace Slic3r::App::Yoga
