#include "Slic3r/App/Config/ExtruderIdButtonGroup.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"

namespace Slic3r::App {

using namespace Yoga;

namespace {
Unit button_width = 30_fpx;
Unit buttons_gap  = 10_fpx;
} // namespace

TransparentLabeledButton::TransparentLabeledButton(const std::string& label) :
    LayoutButton(label),
    m_color(m_theme->color_imgui(Platform::Color::AccentTertiary))
{
    set_object_name("TransparentLabeledButton");
    set_checkable(true);
    set_background_color(Platform::Color::ButtonTransparent);
    set_background_color_checked(
        m_theme->color_imgui(Platform::Color::ButtonTransparent),
        m_theme->color_imgui(Platform::Color::ButtonTransparent)
    );
    set_width(button_width);
    set_label_font_type(Render::ImguiFontType::Bold);
}

void TransparentLabeledButton::checked_updated_internal()
{
    LayoutButton::checked_updated_internal();
    update_label_color();
}

void TransparentLabeledButton::hovered_updated_internal()
{
    LayoutButton::hovered_updated_internal();
    update_label_color();
}

void TransparentLabeledButton::update_label_color()
{
    set_label_color(
        checked() ?
            m_color :
            hovered() ?
            m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled) :
            m_theme->color_imgui(Platform::Color::ButtonTransparent)
    );
}

ExtruderIdButton::ExtruderIdButton(
    size_t index,
    const Biz::Preset::PresetItemObservableList& data,
    std::weak_ptr<Yoga::ButtonGroup> button_group,
    Biz::ProjectInteractor& project_interactor
) :
    Biz::DataObserver<Biz::Preset::PresetItemObservableList>(index, data),
    TransparentLabeledButton(std::to_string(index + 1)),
    m_button_group(button_group),
    m_project_interactor(project_interactor),
    m_colors_changed_listener_scope(project_interactor.project_settings_interactor(), *this)
{
    update_color_from_slots(project_interactor.project_settings_interactor().get_colors(
        project_interactor.selected_project_id(),
        project_interactor.selected_config_container_id()
    ));

    if (!m_button_group.expired()) {
        m_button_group.lock()->insert_button(this);
    }
}

ExtruderIdButton::~ExtruderIdButton()
{
    if (!m_button_group.expired()) {
        m_button_group.lock()->remove_button(this);
    }
}

void ExtruderIdButton::on_colors_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const std::vector<Domain::ColorRGB>& colors
)
{
    if (project_id != m_project_interactor.selected_project_id()
        || config_container_id != m_project_interactor.selected_config_container_id())
    {
        return;
    }

    update_color_from_slots(colors);
}

void ExtruderIdButton::update_color_from_slots(const std::vector<Domain::ColorRGB>& colors)
{
    if (m_index < colors.size()) {
        const Domain::ColorRGB& color = colors[m_index];
        m_color                       = {color.r(), color.g(), color.b()};
    } else {
        // No slot color available for this index (e.g. colors not yet resolved for the
        // current tool count) - fall back to a neutral color instead of keeping whatever
        // was set previously.
        m_color = m_theme->color_imgui(Platform::Color::Text, Platform::ColorGroup::Disabled);
    }
    update_label_color();
}

ExtruderIdButtonGroup::
    ExtruderIdButtonGroup(Biz::ProjectInteractor& project_interactor, bool auto_enabled) :
    m_auto_enabled(auto_enabled),
    m_button_group(std::make_shared<Yoga::ButtonGroup>())
{
    set_object_name("ExtruderIdButtonGroup");
    set_orientation(Orientation::Horizontal);
    set_gap(5_fpx);

    if (m_auto_enabled) {
        m_auto_button = emplace_back<TransparentLabeledButton>("A");
    } else {
        m_auto_button = emplace_back<TransparentLabeledButton>("");
        m_auto_button->set_icon(Render::Icon::Primary);
        m_auto_button->set_enabled(false);
    }
    m_button_group->set_always_checked(false);
    m_button_group->insert_button(m_auto_button);

    m_list_view = emplace_back<ExtruderButtonListView>(ExtruderButtonListViewFactory{
        std::weak_ptr<Yoga::ButtonGroup>(m_button_group),
        project_interactor
    });
    m_list_view->set_orientation(Orientation::Horizontal);
    m_list_view->set_gap(buttons_gap);
    m_list_view->set_source_list(&project_interactor.preset_interactor().material_presets());
}

void ExtruderIdButtonGroup::set_selected_extruder(int extruder_id)
{
    m_selected_extruder_out_of_range =
        extruder_id > static_cast<int>(m_list_view->list_item_count());
    if (extruder_id == 0) {
        if (m_auto_enabled) {
            m_auto_button->set_checked(true);
        }
        return;
    }

    if (m_selected_extruder_out_of_range) {
        if (AbstractButton* checked_button = m_button_group->checked_button()) {
            checked_button->set_checked(false);
        }
        return;
    }

    for (size_t i = 0; i < m_list_view->list_item_count(); ++i) {
        ExtruderIdButton* button = m_list_view->item_at(i);
        if (button->extruder_id() == extruder_id) {
            button->set_checked(true);
            return;
        }
    }
}

std::optional<int> ExtruderIdButtonGroup::selected_extruder_id() const
{
    AbstractButton* checked = m_button_group->checked_button();
    if (!checked) {
        return std::nullopt;
    }

    if (checked == m_auto_button) {
        return 0;
    }
    return static_cast<ExtruderIdButton*>(checked)->extruder_id();
}

bool ExtruderIdButtonGroup::is_selected_extruder_out_of_range() const
{
    return m_selected_extruder_out_of_range;
}

Yoga::ButtonGroup::Callbacks& ExtruderIdButtonGroup::callbacks()
{
    return m_button_group->callbacks();
}

} // namespace Slic3r::App
