#include "Slic3r/App/ProjectButton.hpp"

#include "Slic3r/App/Yoga/Rectangle.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Tooltip.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/ProjectSaver.hpp"
#include "Slic3r/App/IDialogManager.hpp"

#include "Slic3r/Biz/I18N/I18N.hpp"

#include <fmt/format.h>
#include <imgui/imgui_internal.h>

using namespace Slic3r::Biz;
using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ProjectButton::ProjectButton(
    size_t index,
    const Domain::SelectionId& data,
    Biz::ProjectInteractor& project_interactor,
    ProjectSaver& project_saver
) :
    Biz::DataObserver<Domain::SelectionId>(index, data),
    m_project_interactor(project_interactor),
    m_project_saver(project_saver)
{
    m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);

    set_allow_overlap(true);
    set_flex_shrink(0);

    m_background = emplace_back<Rectangle>();
    m_background->set_margin(Margins(0, 5, 0, 0));
    m_background->set_padding(Paddings(30.f, 8.f, 20.f, 12.f));
    m_background->set_rounding(5.f);
    m_background->set_gap(7.f);
    m_background->set_flags(ImDrawFlags_RoundCornersTop);
    update_bg_color();

    set_tooltip_position(Position::Bottom);

    m_label = m_background->emplace_back<Text>("");
    m_label->set_self_align(YGAlignCenter);

    m_cross = m_background->emplace_back<LayoutButton>("", Render::Icon::TopBarCross);
    m_cross->set_width(22);
    m_cross->set_height(22);
    m_cross->set_self_align(YGAlignCenter);
    m_cross->set_flex_shrink(0);
    m_cross->set_background_color(IM_COL32_BLACK_TRANS);

    m_separator_wrap = emplace_back<Item>();
    m_separator_wrap->set_padding({0.f, 10.f});
    m_separator_wrap->emplace_back<Separator>(Orientation::Vertical)
        ->set_fill(m_theme->color_imgui(Platform::Color::SceneBgTop));

    on_data_update();
}

Domain::SelectionId ProjectButton::project_id() const
{
    return *m_state;
}

bool ProjectButton::is_cross_hovered() const
{
    return m_cross->hovered();
}

void ProjectButton::on_selected_project_changed(size_t index)
{
    set_checked(*m_state == index);
}

void ProjectButton::hovered_updated_internal()
{
    update_bg_color();
}

void ProjectButton::checked_updated_internal()
{
    update_bg_color();
    m_label->set_text_color(m_theme->color_imgui(
        m_project_interactor.backup_store().is_project_unsaved(*m_state) ?
            Platform::Color::AccentTertiary :
            Platform::Color::Text,
        checked() ? Platform::ColorGroup::Default : Platform::ColorGroup::Disabled
    ));
    m_label->set_font_type(
        checked() ? Render::ImguiFontType::Bold : Render::ImguiFontType::Regular
    );
}

void ProjectButton::on_data_update()
{
    const boost::filesystem::path proj_path(m_project_interactor.get_project_name(*m_state));
    std::string btn_label;
    std::string btn_tooltip;
    if (proj_path.empty()) {
        std::string new_project = _u8L("New Project");
        if (*m_state) {
            new_project += fmt::format(" ({})", *m_state);
        }
        btn_label   = new_project;
        btn_tooltip = new_project;
    } else {
        btn_label   = proj_path.filename().string();
        btn_tooltip = proj_path.string();
    }

    m_label->set_text_color(m_theme->color_imgui(
        m_project_interactor.backup_store().is_project_unsaved(*m_state) ?
            Platform::Color::AccentTertiary :
            Platform::Color::Text
    ));

    set_tooltip(btn_tooltip);
    m_label->set_text(btn_label);

    m_cross->callbacks().action = [this]()
    {
        if (m_project_interactor.backup_store().is_project_unsaved(*m_state)) {
            AppServices::instance().dialog_manager().show_yesnocancel_dialog(
                Biz::_u8L("Close project"),
                Biz::_u8L("There are unsaved changes."),
                {Biz::_u8L("Save"),
                 [this]
                 {
                     if (m_project_saver.save_project(*m_state)) {
                         m_project_interactor.remove_project(*m_state);
                     }
                 }},
                {Biz::_u8L("Discard"), [this] { m_project_interactor.remove_project(*m_state); }},
                {Biz::_u8L("Cancel"),
                 []
                 {
                     // do nothing
                 }}
            );
        } else {
            m_project_interactor.remove_project(*m_state);
        }
    };

    callbacks().action = [this]()
    {
        // Ignore action, if cross button was clicked or if button is already selected
        if (m_cross->hovered() || checked()) {
            return;
        }
        // select related project
        m_project_interactor.select_project(*m_state);
    };

    set_checked(m_project_interactor.selected_project_id() == *m_state);
}

void ProjectButton::update_bg_color()
{
    ImColor bg_color;
    if (checked()) {
        bg_color = m_theme->color_imgui(Platform::Color::SceneBgTop);
    } else if (hovered()) {
        bg_color = m_theme->color_imgui(Platform::Color::Button, Platform::ColorGroup::Hovered);
    } else {
        bg_color = m_theme->color_imgui(Platform::Color::Transparent);
    }
    m_background->set_fill(bg_color);
}

void ProjectButton::on_view_will_be_removed()
{
    m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
}

void ProjectButton::set_separator_visible(bool separator_visible)
{
    m_separator_wrap->set_visible(separator_visible);
}

} // namespace Slic3r::App
