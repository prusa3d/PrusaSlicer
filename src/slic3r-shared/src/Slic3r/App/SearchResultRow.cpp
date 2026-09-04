#include "Slic3r/App/SearchResultRow.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/Config/CategoryUtils.hpp"
#include "Slic3r/App/Navigator.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

SearchResultRow::SearchResultRow(
    size_t index,
    const Domain::ConfigItem& data,
    Navigator& navigator,
    Biz::ProjectInteractor& project_interactor,
    std::weak_ptr<Yoga::ButtonGroup> button_group
) :
    Biz::DataObserver<Domain::ConfigItem>(index, data),
    m_navigator(navigator),
    m_project_interactor(project_interactor),
    m_button_group(button_group)
{
    set_gap(5);
    set_rounding(0);
    set_checkable(true);

    set_background_color(Platform::Color::WindowBg);
    set_background_color_checked(
        m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Active)
    );

    m_icon = emplace_back<Icon>(Render::Icon::None);
    m_icon->set_width(18);
    m_icon->set_flex_shrink(0);

    m_text_category = emplace_back<Text>(std::string());
    m_text_category->set_width(100);
    m_text_category->set_height(ImGui::GetTextLineHeight());
    m_text_category->set_flex_shrink(0);
    m_text_category->set_wrap_mode(Text::WrapMode::WrapElide);

    m_text_label = emplace_back<Text>(std::string());
    m_text_label->set_flex_grow(1);
    m_text_label->set_height(ImGui::GetTextLineHeight());
    m_text_label->set_flex_shrink(0);
    m_text_label->set_wrap_mode(Text::WrapMode::WrapElide);

    m_tooltip->content_item()->set_width(300);
    m_tooltip->set_text_wrap(true);

    m_button_group.lock()->insert_button(this);

    on_data_update();
}

SearchResultRow::~SearchResultRow()
{
    if (!m_button_group.expired()) {
        m_button_group.lock()->remove_button(this);
    }
}

void SearchResultRow::on_data_update()
{
    Domain::PrinterTechnology technology =
        m_project_interactor.selected_config_container().print_technology();

    const Domain::ConfigItemDef& def = m_state->def();

    m_icon->set_icon(CategoryUtils::category_render_icon(def.category, technology));

    const bool is_override = def.location != m_state->location();
    // Ugly hack, blame product
    Domain::ConfigItemDef::Category category =
        is_override ? Domain::ConfigItemDef::Category::Filament_Overrides : def.category;

    m_text_category->set_text(Biz::_u8(Domain::ConfigItemDef::translate_category(category, technology)));

    m_text_label->set_text(Biz::_u8(def.label));

    m_tooltip->set_text(fmt::format("{}\n{}\n{}", Biz::_u8(def.label), m_text_category->text(), Biz::_u8(def.tooltip)));
}

void SearchResultRow::hovered_updated_internal()
{
    RectangleButton::hovered_updated_internal();
    if (hovered()) {
        set_checked(true);
    }
}

void SearchResultRow::action_internal()
{
    m_navigator.navigate_to_item(m_state);
}

} // namespace Slic3r::App
