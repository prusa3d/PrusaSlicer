#include "Slic3r/App/MaterialSettingsDialog.hpp"

#include "Slic3r/Biz/OverridableConfigBoxInteractor.hpp"
#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Config/OverridableSubcategoryListView.hpp"
#include "Slic3r/App/Config/CategoryUtils.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/CurrentPresetLabel.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/MaterialSelectionDialog.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/IDialogManager.hpp"

#include "Slic3r/App/Yoga/LayoutButton.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::App::Render;
using namespace Slic3r::Biz;

namespace Slic3r::App {

MaterialSettingsDialog::MaterialSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    MaterialSelectionDialog* material_selection_dialog
) :
    AbstractSettingsDialog({}, "MaterialSettingsDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_material_selection_dialog(material_selection_dialog),
    m_material_cbi_list(m_project_interactor.preset_interactor().material_cbi_list()),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this)
{
    m_material_cbi_list.add_listener<Biz::IListObserver<Biz::OverridableConfigBoxInteractor>>(this);

    m_footer->set_gap(5.f);

    m_current_preset_label = m_footer->emplace_back<CurrentPresetLabel>(
        m_project_interactor.preset_interactor().material_presets()
    );
    m_current_preset_label->set_align(Align{AlignH::Center, AlignV::Center});

    m_footer->emplace_back<Separator>(Orientation::Vertical);

    m_revert_button = add_footer_button(_u8L("Revert changes"), Render::Icon::UndoGizmo);
    m_revert_button->set_icon_tint(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->set_label_color(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->callbacks().action = [this]
    {
        m_project_interactor.preset_interactor().discard_selected_tool_material_preset_changes(
            current_tab_index()
        );
    };

    add_footer_button(_u8L("Compare"), Render::Icon::Compare)->callbacks().action = [this]
    {
        auto& dlg_manager = App::AppServices::instance().dialog_manager();
        dlg_manager.show_diff_dialog(
            m_project_interactor.preset_interactor(),
            Domain::Preset::PresetKind::FdmMaterial
        );
    };

    add_footer_button(_u8L("Save preset"))->callbacks().action = [&]
    {
        m_project_interactor.preset_interactor().save_user_preset(
            Domain::Preset::PresetKind::FdmMaterial,
            current_tab_index()
        );
    };

    on_reset();
}

MaterialSettingsDialog::~MaterialSettingsDialog()
{
    m_material_cbi_list.remove_listener<Biz::IListObserver<Biz::OverridableConfigBoxInteractor>>(
        this
    );
}

void MaterialSettingsDialog::navigate_to_item(const Domain::ConfigItem* config_item)
{
    m_config_tabs.front()->navigate_to_item(config_item);
}

void MaterialSettingsDialog::clear_navigation()
{
    m_config_tabs.front()->clear_navigation();
}

void MaterialSettingsDialog::on_reset()
{
    Tabs::const_iterator current_tab_it = std::find_if(
        m_tabs.cbegin(),
        m_tabs.cend(),
        [this](const std::unique_ptr<Tab>& tab) { return tab.get() == m_current_tab; }
    );

    std::optional<size_t> current_index;
    if (current_tab_it != m_tabs.cend()) {
        current_index = std::distance(m_tabs.cbegin(), current_tab_it);
    }

    while (!m_config_tabs.empty()) {
        remove_tab(0);
    }
    m_revert_button->set_visible(false);

    for (size_t material_cbi_index = 0; material_cbi_index < m_material_cbi_list.size();
         ++material_cbi_index)
    {
        Biz::OverridableConfigBoxInteractor& cbi = const_cast<Biz::OverridableConfigBoxInteractor&>(
            m_material_cbi_list.at(material_cbi_index)
        );

        std::string tab_name = m_project_interactor.selected_config_container().print_technology()
                == Domain::PrinterTechnology::FFF ?
            _u8L("Filament") :
            _u8L("Material");

        Tab* tab = append_tab(fmt::format("{} {}", tab_name, material_cbi_index + 1));
        m_config_tabs.emplace_back(
            std::make_unique<ConfigTab>(cbi, *tab, m_project_interactor, material_cbi_index)
        );
    }

    if (current_index.has_value()) {
        set_current_tab(std::min(current_index.value(), m_tabs.size() - 1));
    }
}

void MaterialSettingsDialog::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (type == Biz::Preset::PresetItemType::MaterialPreset) {
        update_ui_state();
    }
}

void MaterialSettingsDialog::close_action()
{
    m_navigator.set_opened_dialog(m_material_selection_dialog);
}

void MaterialSettingsDialog::remove_tab(size_t index)
{
    AbstractSettingsDialog::remove_tab(index);
    m_config_tabs.erase(m_config_tabs.begin() + index);
}

void MaterialSettingsDialog::on_tab_selected(int current_index)
{
    AbstractSettingsDialog::on_tab_selected(current_index);
    if (m_remove_in_progress)
        return;

    if (m_current_tab && current_index < static_cast<int>(m_material_cbi_list.size())) {
        m_current_preset_label->set_current_list(current_index);
        if (m_tabs.size() == m_material_cbi_list.size() && m_tabs.size() == m_config_tabs.size()) {
            // Update UI only if m_config_tabs is completed
            update_ui_state();
        }
    }
}

void MaterialSettingsDialog::on_about_to_close()
{
    clear_navigation();
}

void MaterialSettingsDialog::update_ui_state(const Domain::ConfigItem* changed_item)
{
    Biz::OverridableConfigBoxInteractor& cbi = const_cast<Biz::OverridableConfigBoxInteractor&>(
        m_material_cbi_list.at(current_tab_index())
    );
    m_revert_button->set_visible(cbi.config_box_overridable_list().lock()->is_dirty());

    m_config_tabs.at(current_tab_index())->dirty_categorizer->invalidate();
    if (changed_item) {
        auto& category_page_transformer =
            m_config_tabs.at(current_tab_index())->category_page_transformer;
        Domain::PrinterTechnology pt =
            m_project_interactor.selected_config_container().print_technology();
        for (size_t index = 0; index < category_page_transformer->size(); index++) {
            const Domain::ConfigItemDef::Category category =
                changed_item->def().location != changed_item->location() ?
                Domain::ConfigItemDef::Category::Filament_Overrides :
                changed_item->def().category;

            const auto& data = category_page_transformer->at(index);
            if (data.name == Biz::_u8(Domain::ConfigItemDef::translate_category(category, pt))) {
                category_page_transformer->on_updated(index);
            }
        }
    } else {
        auto& category_page_transformer =
            m_config_tabs.at(current_tab_index())->category_page_transformer;
        ASSERT(category_page_transformer->size() > 1);
        category_page_transformer->on_updated({0, category_page_transformer->size() - 1});
    }
}

MaterialSettingsDialog::ConfigTab::ConfigTab(
    Biz::OverridableConfigBoxInteractor& cbi,
    Tab& tab,
    Biz::ProjectInteractor& project_interactor,
    size_t cbi_index
) :
    cbi(cbi),
    tab(tab),
    project_interactor(project_interactor),
    cbi_index(cbi_index),
    categorizer(std::make_shared<Categorizer>()),
    dirty_categorizer(std::make_shared<DirtyCategorizer>()),
    category_page_transformer(std::make_shared<OverridableCategoryPageTransformer>())
{
    auto group_by_fn = [](const Biz::OverrideItem& item,
                          std::unordered_set<Domain::ConfigItemDef::Category>& seen_keys) -> bool
    {
        const Domain::ConfigItemDef::Category category = item.is_override() ?
            Domain::ConfigItemDef::Category::Filament_Overrides :
            item.config_item->def().category;
        DEBUG_ASSERT(
            category != Domain::ConfigItemDef::Category::Unknown,
            "ConfigItemDef cannot have unknown category, please fill it."
        );

        if (seen_keys.contains(category)) {
            return true;
        } else {
            seen_keys.insert(category);
            return false;
        }
    };

    categorizer->set_filter_fn(
        [](const Biz::OverrideItem& item)
        { return item.config_item->def().category != Domain::ConfigItemDef::Category::Hidden; }
    );
    categorizer->set_group_by_fn(group_by_fn);
    categorizer->set_sort_fn(
        [](const Biz::OverrideItem& lhs, const Biz::OverrideItem& rhs)
        { return lhs.config_item->def().category < rhs.config_item->def().category; }
    );
    categorizer->set_source_model(cbi.config_box_overridable_list());

    dirty_categorizer->set_filter_fn([](const Biz::OverrideItem& item) { return item.is_dirty(); });
    dirty_categorizer->set_group_by_fn(group_by_fn);
    dirty_categorizer->set_category_getter_fn(
        [](const Biz::OverrideItem& data)
        {
            return data.is_override() ? Domain::ConfigItemDef::Category::Filament_Overrides :
                                        data.config_item->def().category;
        }
    );
    dirty_categorizer->set_source_model(cbi.config_box_overridable_list());

    category_page_transformer->set_transform_fn(
        [this](const Biz::OverrideItem& data, size_t index)
        {
            const Domain::ConfigItemDef::Category category = data.is_override() ?
                Domain::ConfigItemDef::Category::Filament_Overrides :
                data.config_item->def().category;

            Domain::PrinterTechnology pt =
                this->project_interactor.selected_config_container().print_technology();
            Render::Icon icon = CategoryUtils::category_render_icon(category, pt);

            return PageEntry{
                Biz::_u8(Domain::ConfigItemDef::translate_category(category, pt)),
                icon,
                dirty_categorizer->contains(category)
            };
        }
    );

    category_page_transformer->set_source_model(categorizer.get());

    using OverridableCategoryListViewFactory = ViewFactory<
        OverridableSubcategoryListView,
        Biz::OverrideItem,
        Biz::IConfigBoxSetter&,
        Biz::OverridableConfigBoxInteractor&,
        size_t>;
    using OverridableCategoryListView = ListView<
        OverridableSubcategoryListView,
        Biz::OverrideItem,
        OverridableCategoryListViewFactory,
        StackLayout>;

    std::unique_ptr<OverridableCategoryListView> category_list_view =
        std::make_unique<OverridableCategoryListView>(OverridableCategoryListViewFactory{
            project_interactor.preset_interactor(),
            cbi,
            cbi_index
        });

    category_list_view->set_source_list(categorizer.get());

    tab.replace_stack_layout(std::move(category_list_view));

    tab.page_list_view->set_source_list(category_page_transformer.get());

    if (tab.page_list_view->list_item_count()) {
        tab.page_list_view->item_at(0)->set_checked(true);
    }
}

void MaterialSettingsDialog::ConfigTab::navigate_to_item(const Domain::ConfigItem* config_item)
{
    clear_navigation();

    const bool is_override = config_item->def().location != config_item->location();

    const Domain::ConfigItemDef::Category category = is_override ?
        Domain::ConfigItemDef::Category::Filament_Overrides :
        config_item->def().category;
    for (size_t category_index = 0; category_index < categorizer->size(); ++category_index) {
        if (categorizer->at(category_index).config_item->def().category == category) {
            tab.page_list_view->item_at(category_index)->callbacks().action();
            dynamic_cast<OverridableSubcategoryListView*>(
                tab.pages_stack_layout->get_item(category_index)
            )
                ->navigate_to_item(config_item);
            break;
        }
    }
}

void MaterialSettingsDialog::ConfigTab::clear_navigation()
{
    for (size_t index = 0; index < tab.pages_stack_layout->object_count(); ++index) {
        dynamic_cast<OverridableSubcategoryListView*>(tab.pages_stack_layout->get_item(index))
            ->clear_navigation();
    }
}

void MaterialSettingsDialog::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
        const auto location{std::get<Domain::FDMConfigLocation>(item.location())};
        if (location != Domain::FDMConfigLocation::Filament) {
            return;
        }
    } else if (std::holds_alternative<Domain::SLAConfigLocation>(item.location())) {
        const auto location{std::get<Domain::SLAConfigLocation>(item.location())};
        if (location != Domain::SLAConfigLocation::Material) {
            return;
        }
    }

    update_ui_state(&item);
}

} // namespace Slic3r::App
