#include "Slic3r/App/ColorMix/BlendRecipeTile.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/Text.hpp"

using Slic3r::App::Platform::Color;
using Slic3r::App::Platform::ColorGroup;
using Slic3r::App::Render::ImguiFontType;

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::ColorMix {

constexpr Unit TILE_PADDING          = 3_fpx;
constexpr Unit LABEL_PADDING         = 5_fpx;
const constexpr float TILE_ROUNDING  = 6.f;
const constexpr float RING_ROUNDING  = 4.f;
const constexpr float RING_THICKNESS = 2.f;
const constexpr float BORDER_WIDTH   = 1.f;
const constexpr float DASH_LENGTH    = 6.f;
const constexpr float DASH_GAP       = 4.f;

BlendRecipeTile::
    BlendRecipeTile(const size_t index, const BlendTileData& data, FnIndexClicked on_clicked) :
    RectangleButton(),
    DataObserver<BlendTileData>(index, data),
    m_on_clicked{std::move(on_clicked)}
{
    this->set_object_name("BlendRecipeTile");
    this->set_width(54_fpx);
    this->set_height(54_fpx);
    this->set_flex_shrink(0);
    this->set_cursor(ImGuiMouseCursor_Hand);

    const ImColor transparent_color = m_theme->color_imgui(Color::Transparent);
    this->set_background_color(transparent_color, transparent_color);
    this->set_background_color_checked(transparent_color, transparent_color);
    this->set_background_border_width(RING_THICKNESS);
    this->set_rounding(RING_ROUNDING);
    this->set_content_padding({TILE_PADDING});

    m_tile = this->emplace_back<Rectangle>();
    m_tile->set_flex_grow(1.f);
    m_tile->set_rounding(TILE_ROUNDING);
    m_tile->set_border_width(BORDER_WIDTH);
    m_tile->set_border_color(m_theme->color_imgui(Color::Text, ColorGroup::Disabled));
    m_tile->set_padding({LABEL_PADDING});

    m_label = m_tile->emplace_back<Text>(std::string{}, ImguiFontType::Bold);
    m_label->set_font_size(0.8_rem);
    m_label->set_flex_grow(1.f);
    m_label->set_align({AlignH::Right, AlignV::Bottom});

    this->callbacks().action = [this]() -> void { m_on_clicked(m_index); };

    this->BlendRecipeTile::on_data_update();
}

void BlendRecipeTile::on_data_update()
{
    const ImColor accent_color      = m_theme->color_imgui(Color::AccentPrimary);
    const ImColor transparent_color = m_theme->color_imgui(Color::Transparent);
    if (m_state->already_used) {
        this->set_background_color_border(accent_color, accent_color);
        this->set_background_border_dash(DASH_LENGTH, DASH_GAP);
    } else {
        this->set_background_color_border(transparent_color, accent_color);
        this->set_background_border_dash(0.f, 0.f);
    }

    this->set_tooltip(m_state->tooltip);

    m_tile->set_fill(m_state->color);
    m_label->set_text(m_state->label);
    m_label->set_text_color(Imgui::contrast_color(m_state->color));
}

} // namespace Slic3r::App::ColorMix
