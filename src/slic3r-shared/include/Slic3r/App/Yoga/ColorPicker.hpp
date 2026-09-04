#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {

class ColorPicker : public Item
{
public:
    explicit ColorPicker(const std::string& name = {});

    struct Callbacks
    {
        std::function<void(const ImColor& color)> color_edited{nullptr};
    };

    void render(const Vec2f& pos, const Vec2f& size) override;

    Callbacks& callbacks();

    void set_color(const ImColor& color);

    ImGuiColorEditFlags flags() const;

    void set_flags(ImGuiColorEditFlags flags);

protected:
    Vec2f get_item_size() override;

private:
    Callbacks m_callbacks;

    float m_data[4]             = {0, 0, 0, 1};
    ImGuiColorEditFlags m_flags = ImGuiColorEditFlags_NoAlpha
        | ImGuiColorEditFlags_NoLabel
        | ImGuiColorEditFlags_PickerHueWheel
        | ImGuiColorEditFlags_NoSidePreview
        | ImGuiColorEditFlags_NoSmallPreview
        | ImGuiColorEditFlags_DisplayRGB
        | ImGuiColorEditFlags_DisplayHex;
};

} // namespace Slic3r::App::Yoga
