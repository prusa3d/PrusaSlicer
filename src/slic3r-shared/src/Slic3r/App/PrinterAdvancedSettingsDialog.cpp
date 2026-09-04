#include "Slic3r/App/PrinterAdvancedSettingsDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/LogicalPrinterSettingsDialog.hpp"
#include <Slic3r/App/AppServices.hpp>
#include "Slic3r/App/IDialogManager.hpp"

#include "Slic3r/App/Config/CategoryUtils.hpp"

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App {

PrinterAdvancedSettingsDialog::PrinterAdvancedSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    LogicalPrinterSettingsDialog* logical_printer_settings_dialog
) :
    ConfigSettingsDialog(project_interactor, navigator, "PrinterAdvancedSettingsDialog"),
    m_list_selection_changed_scope(project_interactor.preset_interactor().printer_presets(), *this),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this),
    m_dirty_categorizer(std::make_shared<DirtyCategorizer>()),
    m_logical_printer_settings_dialog(logical_printer_settings_dialog)
{
    Tab* tab = append_tab(_u8L("Printer"));
    m_config_tabs.emplace_back(
        std::make_unique<ConfigTab>(
            &project_interactor.preset_interactor().printer_cbi(),
            tab,
            project_interactor,
            0
        )
    );

    m_dirty_categorizer->set_filter_fn(
        [](const Biz::ConfigItemContext& data) { return data.is_dirty(); }
    );
    m_dirty_categorizer->set_group_by_fn(
        [](const Biz::ConfigItemContext& data,
           std::unordered_set<Domain::ConfigItemDef::Category>& seen_keys) -> bool
        {
            DEBUG_ASSERT(
                data.config_item->def().category != Domain::ConfigItemDef::Category::Unknown,
                "ConfigItemDef cannot have unknown category, please fill it."
            );

            if (seen_keys.contains(data.config_item->def().category)) {
                return true;
            } else {
                seen_keys.insert(data.config_item->def().category);
                return false;
            }
        }
    );
    m_dirty_categorizer->set_category_getter_fn(
        [](const Biz::ConfigItemContext& data) -> Domain::ConfigItemDef::Category
        { return data.config_item->def().category; }
    );
    m_dirty_categorizer->set_source_model(
        project_interactor.preset_interactor().printer_cbi().config_box_list()
    );

    m_config_tabs.front()->category_page_transformer->set_transform_fn(
        [this](const Biz::ConfigItemContext& data, size_t index)
        {
            const Domain::ConfigItemDef::Category category = data.config_item->def().category;

            Domain::PrinterTechnology pt =
                m_project_interactor->selected_config_container().print_technology();
            Render::Icon icon = CategoryUtils::category_render_icon(category, pt);

            return PageEntry{
                Biz::_u8(Domain::ConfigItemDef::translate_category(category, pt)),
                icon,
                m_dirty_categorizer->contains(category)
            };
        }
    );

    m_footer->set_gap(5.f);

    m_label_preset_name = m_footer->emplace_back<Text>(std::string{});
    m_label_preset_name->set_align({AlignH::Center, AlignV::Center});

    m_footer->emplace_back<Separator>(Orientation::Vertical);

    m_revert_button = add_footer_button(_u8L("Revert changes"), Render::Icon::UndoGizmo);
    m_revert_button->set_icon_tint(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->set_label_color(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->callbacks().action = [this]
    { m_project_interactor->preset_interactor().discard_selected_printer_preset_changes(); };
    m_revert_button->set_visible(false);

    add_footer_button(_u8L("Compare"), Render::Icon::Compare)->callbacks().action = [this]
    {
        auto& dlg_manager = App::AppServices::instance().dialog_manager();
        dlg_manager.show_diff_dialog(
            m_project_interactor->preset_interactor(),
            Domain::Preset::PresetKind::FdmPrinter
        );
    };
    add_footer_button(_u8L("Save preset"))->callbacks().action = [&]
    {
        m_project_interactor->preset_interactor()
            .save_user_preset(Domain::Preset::PresetKind::FdmPrinter, 0);
    };
}

void PrinterAdvancedSettingsDialog::on_list_selection_changed(Domain::SelectionId new_selection)
{
    if (new_selection == Domain::INVALID_ID) {
        return;
    }

    const Biz::Preset::PresetItem& preset_item =
        m_project_interactor->preset_interactor().printer_presets().items().at(new_selection);

    m_label_preset_name->set_text(preset_item.ui_hw_config_name());

    update_ui_state();
}

void PrinterAdvancedSettingsDialog::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
        const auto location{std::get<Domain::FDMConfigLocation>(item.location())};
        if (location != Domain::FDMConfigLocation::Printer) {
            return;
        }
    } else if (std::holds_alternative<Domain::SLAConfigLocation>(item.location())) {
        const auto location{std::get<Domain::SLAConfigLocation>(item.location())};
        if (location != Domain::SLAConfigLocation::Printer) {
            return;
        }
    }

    update_ui_state(&item);
}

void PrinterAdvancedSettingsDialog::update_ui_state(const Domain::ConfigItem* changed_item)
{
    m_revert_button->set_visible(
        m_project_interactor->preset_interactor().printer_cbi().config_box_list().lock()->is_dirty()
    );

    auto& category_page_transformer = m_config_tabs.front()->category_page_transformer;
    Domain::PrinterTechnology pt =
        m_project_interactor->selected_config_container().print_technology();
    if (changed_item) {
        for (size_t index = 0; index < category_page_transformer->size(); index++) {
            const auto& data = category_page_transformer->at(index);
            if (data.name
                == Biz::_u8(
                    Domain::ConfigItemDef::translate_category(changed_item->def().category, pt)
                ))
            {
                category_page_transformer->on_updated(index);
                return;
            }
        }
    } else {
        ASSERT(category_page_transformer->size() > 1);
        category_page_transformer->on_updated({0, category_page_transformer->size() - 1});
    }
}

void PrinterAdvancedSettingsDialog::close_action()
{
    m_navigator.set_opened_dialog(m_logical_printer_settings_dialog);
}

} // namespace Slic3r::App
