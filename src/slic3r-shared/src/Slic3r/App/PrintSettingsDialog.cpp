#include "Slic3r/App/PrintSettingsDialog.hpp"
#include "Slic3r/App/Yoga/AbstractSettingsDialog.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"

#include "Slic3r/App/Config/CategoryUtils.hpp"
#include "Slic3r/App/Navigator.hpp"
#include <Slic3r/App/AppServices.hpp>
#include "Slic3r/App/IDialogManager.hpp"
#include "Slic3r/App/Yoga/StackLayout.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/Yoga/Menu.hpp"
#include "Slic3r/App/Yoga/MenuItem.hpp"

#include <fmt/format.h>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::App::Render;
using namespace Slic3r::Biz;

namespace Slic3r::App {

PrintSettingsDialog::PrintSettingsDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Dialog("PrintSettingsDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_tool_print_categorizer(std::make_shared<ToolPrintCategorizer>()),
    m_dirty_tool_print_categorizer(std::make_shared<DirtyToolPrintCategorizer>()),
    m_tool_print_transformer(std::make_shared<ToolPrintMenuTransformer>()),
    m_extruder_menu_transformer(std::make_shared<ExtruderMenuTransformer>()),
    m_preset_changed_listener_scope(project_interactor.preset_interactor(), *this)
{
    content_item()->set_width(700);
    content_item()->set_height(700);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(0);
    content()->set_flex_grow(1);
    // Only preserve 1px bottom padding
    content()->set_padding(Paddings(0, 0, 0, 1));

    append_tab(Biz::_u8L("Print"));

    Item* center_row = content()->emplace_back<Item>();
    center_row->set_orientation(Orientation::Horizontal);
    center_row->set_flex_grow(1);

    auto group_by_fn = [](const Biz::PrintToolItem& item,
                          std::unordered_set<Domain::ConfigItemDef::Category>& seen_keys) -> bool
    {
        DEBUG_ASSERT(
            item.print_item->def().category != Domain::ConfigItemDef::Category::Unknown,
            "ConfigItemDef cannot have unknown category, please fill it."
        );

        if (seen_keys.contains(item.print_item->def().category)) {
            return true;
        } else {
            seen_keys.insert(item.print_item->def().category);
            return false;
        }
    };

    m_tool_print_categorizer->set_filter_fn(
        [](const Biz::PrintToolItem& item)
        { return item.print_item->def().category != Domain::ConfigItemDef::Category::Hidden; }
    );
    m_tool_print_categorizer->set_group_by_fn(group_by_fn);
    m_tool_print_categorizer->set_sort_fn(
        [](const Biz::PrintToolItem& lhs, const Biz::PrintToolItem& rhs) -> int
        {
            return static_cast<uint8_t>(lhs.print_item->def().category)
                < static_cast<uint8_t>(rhs.print_item->def().category);
        }
    );

    m_dirty_tool_print_categorizer->set_filter_fn([](const Biz::PrintToolItem& item)
                                                  { return item.is_dirty(); });
    m_dirty_tool_print_categorizer->set_group_by_fn(group_by_fn);
    m_dirty_tool_print_categorizer->set_category_getter_fn(
        [](const Biz::PrintToolItem& item) -> Domain::ConfigItemDef::Category
        { return item.print_item->def().category; }
    );
    m_dirty_tool_print_categorizer->set_source_model(
        m_project_interactor.preset_interactor().print_tool_cbi().observable_list()
    );

    m_tool_print_transformer->set_transform_fn(
        [this](const Biz::PrintToolItem& data, size_t index) -> PageEntry
        {
            const Domain::ConfigItemDef::Category category = data.print_item->def().category;

            Domain::PrinterTechnology pt =
                m_project_interactor.selected_config_container().print_technology();
            Render::Icon icon = CategoryUtils::category_render_icon(category, pt);
            return PageEntry{
                Biz::_u8(Domain::ConfigItemDef::translate_category(category, pt)),
                icon,
                m_dirty_tool_print_categorizer->contains(category)
            };
        }
    );
    m_tool_print_transformer->set_source_model(m_tool_print_categorizer.get());

    // Create the ViewFactory explicitly:
    auto factory_category =
        Yoga::ViewFactory<PageEntryButton, PageEntry, PageEntryButton::FnIndexClicked>(
            [this](size_t index) { select_page_entry(index, true); }
        );
    auto factory_extruder =
        Yoga::ViewFactory<PageEntryButton, PageEntry, PageEntryButton::FnIndexClicked>(
            [this](size_t index) { select_page_entry(index, false); }
        );
    ScrollArea* left_column = center_row->emplace_back<ScrollArea>();
    left_column->set_padding(Paddings(0, 0, 12, 0));
    left_column->set_orientation(Orientation::Vertical);
    left_column->set_gap(5);

    m_category_page_list_view =
        left_column->emplace_back<PageListView>(std::move(factory_category));
    m_category_page_list_view->set_object_name("CategoryPageListView");
    m_category_page_list_view->set_orientation(Orientation::Vertical);
    m_category_page_list_view->set_min_width(125);
    m_category_page_list_view->set_flex_shrink(0);
    m_category_page_list_view->set_source_list(m_tool_print_transformer.get());
    m_category_page_list_view->set_flex_grow(1);

    m_tool_print_categorizer->set_source_model(
        m_project_interactor.preset_interactor().print_tool_cbi().observable_list()
    );

    m_extruder_menu_transformer->set_transform_fn(
        [](const Biz::Preset::ToolConfigItemObservableList& data, size_t index) -> PageEntry
        {
            return PageEntry{
                Biz::_u8L("Extruder") + " " + std::to_string(index + 1),
                Render::Icon::Funnel
            };
        }
    );
    m_extruder_menu_transformer->set_source_model(
        &m_project_interactor.preset_interactor().tool_items()
    );

    Text* extruders_label =
        left_column->emplace_back<Text>(Biz::_u8L("Extruders"), Render::ImguiFontType::Bold);
    extruders_label->set_margin({5, 0});
    extruders_label->set_flex_shrink(0);

    m_extruder_page_list_view =
        left_column->emplace_back<PageListView>(std::move(factory_extruder));
    m_extruder_page_list_view->set_object_name("ExtruderPageListView");
    m_extruder_page_list_view->set_orientation(Orientation::Vertical);
    m_extruder_page_list_view->set_min_width(125);
    m_extruder_page_list_view->set_flex_shrink(0);
    m_extruder_page_list_view->set_flex_grow(1);
    m_extruder_page_list_view->set_source_list(m_extruder_menu_transformer.get());

    center_row->emplace_back<Separator>(Orientation::Vertical);

    m_content_stack_layout = center_row->emplace_back<StackLayout>();
    m_content_stack_layout->set_orientation(Orientation::Vertical);
    m_content_stack_layout->set_flex_grow(1);

    m_category_stack_list_view =
        m_content_stack_layout->emplace_back<ToolPrintCategoryListView>(ToolPrintCategoryFactory{
            m_project_interactor.preset_interactor().print_tool_cbi(),
            m_project_interactor.preset_interactor(),
            m_project_interactor
        });
    m_category_stack_list_view->set_orientation(Orientation::Vertical);
    m_category_stack_list_view->set_flex_grow(1);
    m_category_stack_list_view->set_source_list(m_tool_print_categorizer.get());

    m_metadata_stack_list_view = m_content_stack_layout->emplace_back<PrintMetadataListView>(
        m_project_interactor.preset_interactor()
    );
    m_metadata_stack_list_view->set_orientation(Orientation::Vertical);
    m_metadata_stack_list_view->set_flex_grow(1);
    m_metadata_stack_list_view->set_source_list(
        &m_project_interactor.preset_interactor().tool_items()
    );

    add_separator();

    m_footer = content()->emplace_back<Item>();
    m_footer->set_padding(10);
    m_footer->set_align_items(YGAlignCenter);
    m_footer->set_flex_shrink(0);
    m_footer->set_gap(5);

    m_bed_name = m_footer->emplace_back<Text>(std::string{});
    m_bed_name->set_flex_shrink(0);

    m_footer->set_gap(5.f);
    Item* spacer = m_footer->emplace_back<Item>();
    spacer->set_flex_grow(1);

    m_revert_button = AbstractSettingsDialog::add_footer_button(
        m_footer,
        _u8L("Revert changes"),
        Render::Icon::UndoGizmo
    );
    m_revert_button->set_icon_tint(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->set_label_color(m_theme->color_imgui(Platform::Color::AccentTertiary));
    m_revert_button->callbacks().action = [this]
    {
        m_project_interactor.preset_interactor().discard_selected_print_preset_changes();
        size_t tool_index = m_project_interactor.preset_interactor().tool_items().size();
        while (tool_index > 0) {
            m_project_interactor.preset_interactor().discard_selected_tool_print_preset_changes(
                --tool_index
            );
        }
    };
    m_revert_button->set_visible(false);

    LayoutButton* compare_button =
        AbstractSettingsDialog::add_footer_button(m_footer, _u8L("Compare"), Render::Icon::Compare);
    compare_button->callbacks().action = [this]
    {
        auto& dlg_manager = App::AppServices::instance().dialog_manager();
        dlg_manager.show_diff_dialog(
            m_project_interactor.preset_interactor(),
            Domain::Preset::PresetKind::FdmPrint
        );
    };
    compare_button->set_flex_shrink(0);

    m_save_button = AbstractSettingsDialog::add_footer_button(m_footer, _u8L("Save preset"));
    m_save_button->callbacks().action = [this]
    {
        m_project_interactor.preset_interactor().save_user_tool_print_presets();
    };

    if (m_category_page_list_view->list_item_count()) {
        m_category_page_list_view->item_at(0)->set_checked(true);
    }
}

void PrintSettingsDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

void PrintSettingsDialog::select_page_entry(size_t index, bool category)
{
    for (size_t button_index = 0; button_index < m_category_page_list_view->object_count();
         ++button_index)
    {
        PageEntryButton* button =
            dynamic_cast<PageEntryButton*>(m_category_page_list_view->get_item(button_index));
        ASSERT(button);
        button->set_checked(category && index == button_index);
    }

    for (size_t button_index = 0; button_index < m_extruder_page_list_view->object_count();
         ++button_index)
    {
        PageEntryButton* button =
            dynamic_cast<PageEntryButton*>(m_extruder_page_list_view->get_item(button_index));
        ASSERT(button);
        button->set_checked(!category && index == button_index);
    }

    category ? m_category_stack_list_view->set_current_index(index) :
               m_metadata_stack_list_view->set_current_index(index);
    m_content_stack_layout->set_current_index(category ? 0 : 1);
}

void PrintSettingsDialog::on_about_to_close()
{
    clear_navigation();
}

void PrintSettingsDialog::update_dirty_state()
{
    m_revert_button->set_visible(m_project_interactor.preset_interactor()
                                     .print_tool_cbi()
                                     .observable_list()
                                     .lock()
                                     ->is_dirty());

    m_dirty_tool_print_categorizer->invalidate();
}

void PrintSettingsDialog::navigate_to_item(const Domain::ConfigItem* config_item)
{
    clear_navigation();

    const Domain::ConfigItemDef::Category category = config_item->def().category;
    for (size_t category_index = 0; category_index < m_tool_print_categorizer->size();
         ++category_index)
    {
        if (m_tool_print_categorizer->at(category_index).print_item->def().category == category) {
            m_content_stack_layout->set_current_index(0);
            m_category_page_list_view->item_at(category_index)->callbacks().action();
            dynamic_cast<PrintToolSubcategoryListView*>(
                m_category_stack_list_view->get_item(category_index)
            )
                ->navigate_to_item(config_item);
            break;
        }
    }
}

void PrintSettingsDialog::clear_navigation()
{
    for (size_t index = 0; index < m_category_stack_list_view->object_count(); ++index) {
        dynamic_cast<PrintToolSubcategoryListView*>(m_category_stack_list_view->get_item(index))
            ->clear_navigation();
    }
}

void PrintSettingsDialog::on_preset_selection_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    Biz::Preset::PresetItemType type
)
{
    if (type == Biz::Preset::PresetItemType::PrintPreset
        || type == Biz::Preset::PresetItemType::ToolPrintPreset)
    {
        update_dirty_state();
    }
}

void PrintSettingsDialog::on_preset_value_changed(
    Domain::SelectionId project_id,
    Domain::SelectionId config_container_id,
    const Domain::ConfigItem& item
)
{
    if (std::holds_alternative<Domain::FDMConfigLocation>(item.location())) {
        const auto location{std::get<Domain::FDMConfigLocation>(item.location())};
        if (location != Domain::FDMConfigLocation::Print
            && location != Domain::FDMConfigLocation::Tool)
        {
            return;
        }
    } else if (std::holds_alternative<Domain::SLAConfigLocation>(item.location())) {
        const auto location{std::get<Domain::SLAConfigLocation>(item.location())};
        if (location != Domain::SLAConfigLocation::Print) {
            return;
        }
    }

    update_dirty_state();
    Domain::PrinterTechnology pt =
        m_project_interactor.selected_config_container().print_technology();
    for (size_t index = 0; index < m_tool_print_transformer->size(); index++) {
        const auto& data = m_tool_print_transformer->at(index);
        if (data.name
            == Biz::_u8(Domain::ConfigItemDef::translate_category(item.def().category, pt)))
        {
            m_tool_print_transformer->on_updated(index);
            return;
        }
    }
}

void PrintSettingsDialog::on_preset_bundles_loaded()
{
    update_dirty_state();
    ASSERT(m_tool_print_transformer->size() > 1);
    m_tool_print_transformer->on_updated({0, m_tool_print_transformer->size()-1});
}

} // namespace Slic3r::App
