#include "Slic3r/App/SidebarObject.hpp"

#include "Slic3r/App/ColorDropdown.hpp"
#include "Slic3r/App/Plater/ScaleDialog.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/App/OverrideSettingsDialog.hpp"
#include "Slic3r/App/WipeTowerSettings.hpp"
#include "Slic3r/App/Plater/ScaleWidget.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

#include <fmt/format.h>

using Slic3r::Domain::SelectionId;

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

static std::string volume_type_name(Domain::ModelVolumeType type)
{
    switch (type) {
    case Domain::ModelVolumeType::MODEL_PART:
        return Biz::_u8L("Solid part");
    case Domain::ModelVolumeType::NEGATIVE_VOLUME:
        return Biz::_u8L("Negative volume");
    case Domain::ModelVolumeType::PARAMETER_MODIFIER:
        return Biz::_u8L("Modifier");
    case Domain::ModelVolumeType::SUPPORT_BLOCKER:
        return Biz::_u8L("Support blocker");
    case Domain::ModelVolumeType::SUPPORT_ENFORCER:
        return Biz::_u8L("Support modifier");
    default:
        return "";
    }
}

static void add_separator(Item* item, const Unit& padding)
{
    auto* separator = item->emplace_back<Separator>(Orientation::Horizontal);
    separator->set_margin(Margins(-padding, 0.f, -padding, 0.f));
}

class ExtruderDropdown :
    public Yoga::ColorDropdown,
    public Biz::Scene::ISceneSelectionChangedListener,
    public Biz::ISelectedProjectChangedListener
{
public:
    ExtruderDropdown(Biz::ProjectInteractor& project_interactor) :
        Yoga::ColorDropdown{project_interactor, true, true},
        m_project_interactor{project_interactor}
    {
        m_project_interactor.preset_interactor().add_listener<Biz::Preset::IPresetChangedListener>(
            this
        );
        m_project_interactor.scene_interactor()
            .add_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
        m_project_interactor.add_listener<Biz::ISelectedProjectChangedListener>(this);

        on_color_selected = [this](std::size_t index)
        {
            // Translate the item position to the extruder ID.
            const int extruder_id = this->extruder_id_at(index);

            const Biz::Scene::ObjectSelection& selection{
                m_project_interactor.scene_interactor().object_selection()
            };

            Domain::Project& project{m_project_interactor.selected_project()};
            for (const Domain::ElementRef& element : selection.elements) {
                const Domain::ModelObject* model_object{
                    project.find_object_by_id(element.object_id)
                };
                if (!model_object) {
                    continue;
                }

                if (selection.mode == Biz::Scene::SelectionMode::Instance) {
                    const Domain::ConfigItem& item{
                        model_object->object_settings.items.opt("extruder")
                    };
                    m_project_interactor.preset_interactor().set_item_value(
                        item,
                        Domain::ConfigValue{extruder_id}
                    );
                } else if (selection.mode == Biz::Scene::SelectionMode::Volume) {
                    const Domain::ModelVolume* model_volume{
                        project.find_volume_by_id(element.object_id, element.volume_id)
                    };
                    if (!model_volume) {
                        continue;
                    }
                    const Domain::ConfigItem& item{
                        *model_volume->volume_settings.overrides.find("extruder")
                    };
                    m_project_interactor.preset_interactor().set_item_value(
                        item,
                        Domain::ConfigValue{extruder_id}
                    );
                    m_project_interactor.preset_interactor().set_item_override(
                        item,
                        extruder_id != 0
                    );
                }
            }
        };
    }

    ~ExtruderDropdown()
    {
        m_project_interactor.preset_interactor()
            .remove_listener<Biz::Preset::IPresetChangedListener>(this);
        m_project_interactor.scene_interactor()
            .remove_listener<Biz::Scene::ISceneSelectionChangedListener>(this);
        m_project_interactor.remove_listener<Biz::ISelectedProjectChangedListener>(this);
    }

    void on_preset_value_changed(
        Domain::SelectionId project_id,
        Domain::SelectionId config_container_id,
        const Domain::ConfigItem& item
    ) override
    {
        if (!std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
            return;
        }
        const auto location{std::get<Domain::FDMConfigLocation>(item.location())};
        if (location != Domain::FDMConfigLocation::Object
            && location != Domain::FDMConfigLocation::Volume)
        {
            return;
        }

        if (item.name() == "perimeter_extruder"
            || item.name() == "infill_extruder"
            || item.name() == "solid_infill_extruder")
        {
            ColorDropdown::reload();
        }

        if (item.name() == "extruder") {
            reload(project_id);
        }
    }

    void on_scene_selection_changed(
        Domain::SelectionId project_id,
        const Biz::Scene::ObjectSelection& selection
    ) override
    {
        if (project_id != m_project_interactor.selected_project_id()) {
            return;
        }
        reload(project_id);
        ColorDropdown::reload();
    }

    void on_selected_project_changed(size_t project_id) override
    {
        if (project_id != m_project_interactor.selected_project_id()) {
            return;
        }
        reload(project_id);
    }

    void on_config_container_selection_changed(
        SelectionId project_id,
        SelectionId config_container_id
    ) override
    {
        ColorDropdown::on_config_container_selection_changed(project_id, config_container_id);

        if (project_id == m_project_interactor.selected_project_id()) {
            this->reload(project_id);
        }
    }

    void reload(std::size_t project_id)
    {
        const Domain::Project& project{m_project_interactor.workbench().project(project_id)};
        const Biz::Scene::ObjectSelection& selection{
            m_project_interactor.scene_interactor().object_selection(project_id)
        };
        std::set<int> extruder_ids;
        for (const Domain::ElementRef& element : selection.elements) {
            const Domain::ModelObject* object{project.find_object_by_id(element.object_id)};
            if (!object) {
                continue;
            }
            if (selection.mode == Biz::Scene::SelectionMode::Instance) {
                extruder_ids.insert(object->object_settings.items.opt("extruder").get<int>());
            } else if (selection.mode == Biz::Scene::SelectionMode::Volume) {
                const Domain::ModelVolume* model_volume{
                    project.find_volume_by_id(element.object_id, element.volume_id)};
                if (!model_volume) {
                    continue;
                }
                auto extruder{model_volume->volume_settings.overrides.get("extruder")};
                const int extruder_id{extruder ? extruder->get<int>() : 0};
                extruder_ids.insert(extruder_id);
            }
        }

        if (extruder_ids.size() == 1) {
            const int extruder_id = *extruder_ids.begin();

            set_current_index(
                this->index_of_extruder_id(extruder_id)
                    .value_or(static_cast<std::size_t>(std::max(extruder_id, 0)))
            );
        } else {
            set_current_index(std::nullopt);
        }
    }

private:
    Biz::ProjectInteractor& m_project_interactor;
};

SidebarObject::SidebarObject(Biz::ProjectInteractor& project_interactor) :
    Window("SidebarObject"),
    m_project_interactor(project_interactor),
    m_scene_selection_changed_listener_scope(project_interactor.scene_interactor(), *this),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_osi_observer_scope(
        *project_interactor.preset_interactor()
             .object_settings_interactor()
             .object_observable_list()
             .lock(),
        *this
    )
{
    m_override_settings_dialog = emplace_back<OverrideSettingsDialog>(m_project_interactor);

    set_orientation(Orientation::Vertical);
    set_min_height(60);
    set_flex_grow(1);
    set_padding(0);

    const Unit gap{0.8_rem};

    auto title{emplace_back<Item>()};
    title->set_flex_shrink(0);
    title->set_padding({1.25_rem, gap});
    m_text_object_name = title->emplace_back<Text>("Unkown");
    m_text_object_name->set_font_type(Render::ImguiFontType::Bold);
    m_text_object_name->set_font_size(15_fpx);
    m_text_object_name->set_flex_grow(1);

    LayoutButton* close_button =
        title->emplace_back<LayoutButton>(std::string{}, Render::Icon::PrintIdle);
    close_button->set_background_color(Platform::Color::ButtonTransparent);
    close_button->set_width(20);
    close_button->set_height(20);
    close_button->callbacks().action = [this]
    { m_project_interactor.scene_interactor().clear_object_selection(); };

    add_separator(this, {});

    m_scroll_area = emplace_back<ScrollArea>("ScrollPanels");
    m_scroll_area->set_orientation(Orientation::Vertical);
    m_scroll_area->set_flex_grow(1);
    const Unit padding{1.25_rem};
    m_scroll_area->set_padding(padding);
    m_scroll_area->set_gap(gap);

    m_wipe_tower_settings = m_scroll_area->emplace_back<WipeTowerSettings>(m_project_interactor);
    m_wipe_tower_settings->set_flex_shrink(0);

    m_config_item_filter = std::make_shared<ConfigItemFilter>();
    m_config_item_filter->set_filter_fn(
        [this](const Biz::OverrideItem& item) -> bool
        {
            return !item.is_override()
                && (m_project_interactor.preset_interactor()
                            .current_printer_config()
                            .material_slot_count()
                        > 1
                    || item.config_item->def().gui_type
                        == Domain::ConfigItemDef::GUIType::extruder_selection);
        }
    );

    m_extruder_picker = m_scroll_area->emplace_back<Item>();
    m_extruder_picker->set_flex_shrink(0);
    m_extruder_picker->set_orientation(Orientation::Vertical);
    m_extruder_picker->set_gap(gap);
    auto picker_label{m_extruder_picker->emplace_back<Text>(Biz::_u8L("Extruder"))};
    picker_label->set_font_type(Render::ImguiFontType::Bold);
    m_extruder_picker->emplace_back<ExtruderDropdown>(m_project_interactor);
    add_separator(m_extruder_picker, padding);

    m_scale_section = m_scroll_area->emplace_back<Item>();
    m_scale_section->set_flex_shrink(0);
    m_scale_section->set_orientation(Orientation::Vertical);
    m_scale_section->set_gap(gap);

    auto reference_frame_picker{std::make_unique<Plater::ReferenceFramePicker>(
        project_interactor,
        Biz::Scene::SelectionReferenceFrame::Volume
    )};
    m_scale_widget = m_scale_section->emplace_back<Plater::ScaleWidget>(
        m_project_interactor,
        nullptr,
        reference_frame_picker.get()
    );
    m_scale_widget->on_activated(m_project_interactor.selected_project_id());
    m_scale_widget->set_flex_shrink(0);

    add_separator(m_scale_section, padding);

    m_scale_section->append(std::move(reference_frame_picker));
    add_separator(m_scale_section, padding);

    add_volume_type_selector();

    std::weak_ptr<Biz::ObjectSettingsObservableList> object_settings_observable_list =
        m_project_interactor.preset_interactor()
            .object_settings_interactor()
            .object_observable_list();
    m_config_item_filter->set_source_model(object_settings_observable_list);

    m_add_settings_button = m_scroll_area->emplace_back<LayoutButton>(Biz::_u8L("More settings"));
    m_add_settings_button->set_self_align(YGAlignCenter);
    m_add_settings_button->callbacks().action = [this]
    {
        if (m_override_settings_dialog->opened()) {
            m_override_settings_dialog->close();
        } else {
            m_override_settings_dialog->open();
        }
    };
    m_add_settings_button->set_flex_shrink(0);
    m_add_settings_button->set_content_padding({1.25_rem, 0.25_rem});

    m_no_overrides_label =
        m_scroll_area->emplace_back<Text>(Biz::_u8L("No settings can be added for this selection"));
    m_no_overrides_label->set_wrap_mode(Text::WrapMode::Wrap);

    m_override_group_filter = std::make_shared<ObservableOverrideCategorizer>();
    m_override_group_filter->set_allow_disabled(false);
    m_override_group_filter->set_default_categories(
        {Domain::ConfigItemDef::Category::Print_Infill,
         Domain::ConfigItemDef::Category::Print_LayersSurfaces,
         Domain::ConfigItemDef::Category::Print_Supports,
         Domain::ConfigItemDef::Category::Print_WallsPerimeters,
         Domain::ConfigItemDef::Category::Print_BedAdhesion}
    );
    m_override_group_filter->set_ignored_categories(
        {Domain::ConfigItemDef::Category::Object_Extruders}
    );

    m_open_override_settings_dialog_for_category = [this](Domain::ConfigItemDef::Category category)
    { m_override_settings_dialog->open_for_category(category); };

    m_override_group_list_view =
        m_scroll_area->emplace_back<OverrideGroupListView>(OverrideGroupListViewFactory{
            m_project_interactor,
            m_open_override_settings_dialog_for_category
        });
    m_override_group_list_view->set_orientation(Orientation::Vertical);
    m_override_group_list_view->set_gap(5_fpx);
    m_override_group_list_view->set_flex_shrink(0);
    m_override_group_list_view->set_source_list(m_override_group_filter.get());

    m_override_group_filter->set_source_model(object_settings_observable_list);

    m_override_settings_dialog->attach_to_item(this, Position::Left);
    m_override_settings_dialog->callbacks().opened = [this]
    { m_add_settings_button->set_checked(true); };
    m_override_settings_dialog->callbacks().closed = [this]
    { m_add_settings_button->set_checked(false); };
}

void SidebarObject::on_scene_selection_changed(
    Domain::SelectionId project_id,
    const Biz::Scene::ObjectSelection& selection
)
{
    if (m_project_interactor.selected_project_id() != project_id) {
        return;
    }

    m_selection = selection;
    update_object_name();
    update_volume_type_selector();
    update_enable_modifiers();
}

void SidebarObject::on_reset()
{
    // Some of the volumes may have been recreated
    m_selection = m_project_interactor.scene_interactor().object_selection();
    update_volume_type_selector();
    update_enable_modifiers();
}

void SidebarObject::active_tool_changed(Scene::IToolGizmo* active_tool)
{
    if (active_tool == nullptr) {
        m_scale_widget->on_activated(m_project_interactor.selected_project_id());
    } else {
        m_scale_widget->on_deactivated();
    }
}

void SidebarObject::visible_updated_internal()
{
    if (!is_visible()) {
        m_override_settings_dialog->close();
    }
}

void SidebarObject::add_volume_type_selector()
{
    m_volume_type_selector =
        m_scroll_area->emplace_back<ComboBox>(std::initializer_list<std::string>{
            volume_type_name(Domain::ModelVolumeType::MODEL_PART),
            volume_type_name(Domain::ModelVolumeType::NEGATIVE_VOLUME),
            volume_type_name(Domain::ModelVolumeType::PARAMETER_MODIFIER),
            volume_type_name(Domain::ModelVolumeType::SUPPORT_BLOCKER),
            volume_type_name(Domain::ModelVolumeType::SUPPORT_ENFORCER)
        });
    m_volume_type_selector->callbacks().selection_changed = [this](int index)
    {
        m_project_interactor.scene_interactor().set_selected_volume_type(
            static_cast<Domain::ModelVolumeType>(index)
        );
        m_project_interactor.undo_provider().take_snapshot(Biz::UndoSnapshotType::ChangeVolumeType);
        m_volume_type_selector->set_override_label(std::string());

        update_enable_modifiers();
    };

    m_volume_type_selector_warning = m_scroll_area->emplace_back<Text>(
        Biz::_u8L("You can't change a type of the last solid part of the object.")
    );
    m_volume_type_selector_warning->set_text_color(m_theme->color_imgui(Platform::Color::Warning));
    m_volume_type_selector_warning->set_wrap_mode(Text::WrapMode::Wrap);
}

void SidebarObject::update_volume_type_selector()
{
    bool show_warning{false};
    if (m_selection.mode == Biz::Scene::SelectionMode::Volume && !m_selection.empty()) {
        Domain::ModelObject* object = m_project_interactor.selected_project().find_object_by_id(
            m_selection.elements.front().object_id
        );
        ASSERT(object);
        const size_t solid_parts_count = object->parts_count();
        size_t selected_solid_parts_cnt{0};

        std::optional<Domain::ModelVolumeType> sel_type = std::nullopt;
        for (const Domain::ElementRef& el : m_selection.elements) {
            Domain::ModelVolume* volume = m_project_interactor.selected_project().find_volume_by_id(
                el.object_id,
                el.volume_id
            );
            ASSERT(volume);
            if (volume->is_model_part()) {
                selected_solid_parts_cnt++;
            }

            if (!sel_type) {
                sel_type = volume->type();
            } else if (
                sel_type.value() != Domain::ModelVolumeType::INVALID
                && sel_type.value() != volume->type()
            )
            {
                sel_type = Domain::ModelVolumeType::INVALID;
            }
        }

        if (selected_solid_parts_cnt == solid_parts_count) {
            show_warning = true;
        }
        ASSERT(sel_type);

        if (sel_type.value() != Domain::ModelVolumeType::INVALID) {
            m_volume_type_selector->set_current_index(static_cast<int>(sel_type.value()));
        }
        m_volume_type_selector->set_override_label(
            sel_type.value() == Domain::ModelVolumeType::INVALID ? Biz::_u8L("Mixed") :
                                                                   std::string()
        );
    }

    m_volume_type_selector->set_visible(m_selection.mode == Biz::Scene::SelectionMode::Volume);
    m_volume_type_selector->set_enabled(!show_warning);
    m_volume_type_selector_warning->set_visible(show_warning);
}

void SidebarObject::update_object_name()
{
    if (m_selection.empty()) {
        return;
    }

    std::string text;
    if (m_selection.elements.size() > 1) {
        text = fmt::format("{} Selected", m_selection.elements.size());
    } else if (m_selection.elements.front().is_wipe_tower()) {
        text = Biz::_u8L("Wipe tower");
    } else if (m_selection.mode == Biz::Scene::SelectionMode::Instance) {
        Domain::ModelObject* object = m_project_interactor.selected_project().find_object_by_id(
            m_selection.elements.front().object_id
        );
        ASSERT(object);
        text = object->name;
    } else if (m_selection.mode == Biz::Scene::SelectionMode::Volume) {
        Domain::ModelVolume* volume = m_project_interactor.selected_project().find_volume_by_id(
            m_selection.elements.front().object_id,
            m_selection.elements.front().volume_id
        );
        ASSERT(volume);
        text = volume->name;
    }

    m_text_object_name->set_text(text);
}

void SidebarObject::update_enable_modifiers()
{
    bool enable = true;

    if (m_selection.mode == Biz::Scene::SelectionMode::Volume) {
        // std::any_of(m_selection.elements.cbegin(), m_selection.elements.cend(), [this]())
        if (std::any_of(
                m_selection.elements.cbegin(),
                m_selection.elements.cend(),
                [this](const Domain::ElementRef& element)
                {
                    Domain::ModelVolume* volume =
                        m_project_interactor.selected_project().find_volume_by_id(
                            element.object_id,
                            element.volume_id
                        );
                    return volume->type() != Domain::ModelVolumeType::MODEL_PART
                        && volume->type() != Domain::ModelVolumeType::PARAMETER_MODIFIER;
                }
            ))
        {
            enable = false;
        }
    }

    const bool wipe_tower_selected{std::ranges::any_of(
        m_selection.elements,
        [](const Domain::ElementRef& element) { return element.is_wipe_tower(); }
    )};

    enable &= !wipe_tower_selected;

    const bool osi_has_items = m_project_interactor.preset_interactor()
                                   .object_settings_interactor()
                                   .object_observable_list()
                                   .lock()
                                   ->size();

    enable &= osi_has_items;

    m_wipe_tower_settings->set_visible(wipe_tower_selected && m_selection.elements.size() == 1);
    m_add_settings_button->set_visible(!wipe_tower_selected);
    m_add_settings_button->set_enabled(enable);
    m_add_settings_button->set_tooltip(
        m_selection.mode == Biz::Scene::SelectionMode::Instance ?
            Biz::_u8L("Add object settings") :
            Biz::_u8L("Add volume settings")
    );

    const bool show_extruder_picker{enable
                                    && m_project_interactor.selected_config_container()
                                            .selected_preset()
                                            .hw_config.material_slot_count()
                                        > 1};

    m_extruder_picker->set_visible(show_extruder_picker);
    m_override_group_list_view->set_visible(enable);
    m_no_overrides_label->set_visible(!wipe_tower_selected && !enable);
    m_scale_section->set_visible(!wipe_tower_selected);

    if (!enable) {
        m_override_settings_dialog->close();
    }
}

} // namespace Slic3r::App
