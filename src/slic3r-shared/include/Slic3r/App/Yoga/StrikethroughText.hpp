#pragma once

#include "Slic3r/App/Yoga/Text.hpp"

namespace Slic3r::App::Yoga {

class StrikethroughText : public Text
{
public:
    explicit StrikethroughText(
        const std::string& text,
        Render::ImguiFontType font_type = Render::ImguiFontType::Regular
    );

    void render(const Vec2f& pos, const Vec2f& size) override;

    bool strikethrough() const;
    void set_strikethrough(bool strikethrough);

private:
    bool m_strikethrough{false};
};

} // namespace Slic3r::App::Yoga
