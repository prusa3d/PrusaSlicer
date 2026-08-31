#include "Slic3r/App/ColorMix/ExtruderFilterButton.hpp"

#include "Slic3r/App/Imgui/ImguiExtension.hpp"
#include "Slic3r/App/Render/ImguiTypes.hpp"
#include "Slic3r/App/Yoga/LambdaItem.hpp"
#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>

#include <string>

using Slic3r::App::Platform::Color;
using Slic3r::App::Render::ImguiFontType;

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App::ColorMix {

constexpr Unit BUTTON_PADDING         = 1_fpx;
constexpr Unit SWATCH_PADDING         = 2_fpx;
const constexpr float SWATCH_ROUNDING = 4.f;
const constexpr float BORDER_WIDTH    = 1.f;
const constexpr float DASH_LENGTH     = 6.f;
const constexpr float DASH_GAP        = 4.f;

ExtruderFilterButton::ExtruderFilterButton(
    const size_t index,
    const ExtruderFilterData& data,
    FnIndexClicked on_clicked
) :
    RectangleButton(),
    DataObserver<ExtruderFilterData>(index, data),
    m_on_clicked{std::move(on_clicked)}
{
    this->set_object_name("ExtruderFilterButton");
    this->set_width(34_fpx);
    this->set_height(34_fpx);
    this->set_flex_shrink(0);
    this->set_checkable(false);
    this->set_cursor(ImGuiMouseCursor_Hand);

    const ImColor transparent_color = m_theme->color_imgui(Color::Transparent);
    this->set_padding({BUTTON_PADDING});
    this->set_background_color(transparent_color, transparent_color);
    this->set_background_color_checked(transparent_color, transparent_color);
    this->set_background_color_border(
        transparent_color,
        m_theme->color_imgui(Color::AccentPrimary)
    );
    this->set_background_border_width(BORDER_WIDTH);
    this->set_rounding(SWATCH_ROUNDING);
    this->set_content_padding({SWATCH_PADDING});

    m_swatch = this->emplace_back<Rectangle>();
    m_swatch->set_flex_grow(1.f);
    m_swatch->set_rounding(SWATCH_ROUNDING);
    m_swatch->set_border_width(BORDER_WIDTH);
    m_swatch->set_border_color(m_theme->color_imgui(Color::Text));

    m_label = m_swatch->emplace_back<Text>(std::string{}, ImguiFontType::Bold);
    m_label->set_flex_grow(1.f);
    m_label->set_align({AlignH::Center, AlignV::Center});

    m_cross = m_swatch->emplace_back<LambdaItem>(
        [this](const Vec2f& pos, const Vec2f& size) -> void
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ASSERT(draw_list);

            draw_list->AddLine(
                to_im(pos),
                to_im(pos + size),
                Imgui::contrast_color(m_swatch->fill()),
                2.f
            );
        }
    );
    m_cross->set_position_type(YGPositionTypeAbsolute);
    m_cross->set_left(3_fpx);
    m_cross->set_top(3_fpx);
    m_cross->set_right(3_fpx);
    m_cross->set_bottom(3_fpx);

    this->callbacks().action = [this]() -> void { m_on_clicked(m_index); };

    this->ExtruderFilterButton::on_data_update();
}

void ExtruderFilterButton::on_data_update()
{
    const ImColor& color = m_state->color;
    const ImColor fill   = m_state->enabled ?
          color :
          ImColor(
            (color.Value.x + 2.f * 0.75f) / 3.f,
            (color.Value.y + 2.f * 0.75f) / 3.f,
            (color.Value.z + 2.f * 0.75f) / 3.f
        );

    this->set_checked(m_state->enabled);
    this->set_tooltip(
        fmt::format(
            fmt::runtime(_u8L("Toggle extruder {} for preset generation")),
            m_state->extruder_id_1based
        )
    );

    m_swatch->set_fill(fill);
    m_swatch->set_border_dash(m_state->enabled ? 0.f : DASH_LENGTH, DASH_GAP);
    m_label->set_text(std::to_string(m_state->extruder_id_1based));
    m_label->set_text_color(Imgui::contrast_color(fill));
    m_cross->set_visible(!m_state->enabled);
}

} // namespace Slic3r::App::ColorMix
