#include "Slic3r/App/MaterialSelectionDialog.hpp"

#include "Slic3r/App/Yoga/Icon.hpp"
#include "Slic3r/App/Yoga/InputText.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"
#include "Slic3r/App/Yoga/LayoutButton.hpp"
#include "Slic3r/App/AppConfig.hpp"
#include "Slic3r/App/AppServices.hpp"
#include "Slic3r/App/Search.hpp"
#include "Slic3r/App/Navigator.hpp"
#include "Slic3r/App/MaterialSettingsDialog.hpp"
#include "Slic3r/App/AppConfigInteractor.hpp"
#include "Slic3r/App/PrinterSearchFunction.hpp"

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include <boost/locale.hpp>

using namespace Slic3r::App::Yoga;
using namespace Slic3r::Biz;

namespace Slic3r::App {

static bool is_favorite(const Biz::Preset::PresetItem& preset_item)
{
    return AppServices::instance()
        .app_config()
        .app_settings_advanced()
        .contains_material_favorite_preset(preset_item.id);
}

MaterialSelectionDialog::ProjectContext& MaterialSelectionDialog::context()
{
    return m_project_contexts->selected();
}

const MaterialSelectionDialog::ProjectContext& MaterialSelectionDialog::context() const
{
    return m_project_contexts->selected();
}

void MaterialSelectionDialog::update_current_context()
{
    if (m_current_context != context()) {
        m_current_context = context();
        if (LayoutButton* checked_type_button =
                dynamic_cast<LayoutButton*>(m_material_type_button_group.checked_button());
            m_current_context.type_filter != checked_type_button->label())
        {
            m_type_filter_buttons
                .at(m_current_context.type_filter.empty() ? Biz::_u8L("All") :
                                                            m_current_context.type_filter)
                ->set_checked(true);
        }
        m_material_filter->invalidate();
    }
}

MaterialSelectionDialog::MaterialSelectionDialog(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator
) :
    Yoga::Dialog({"Material"}, "MaterialSelectionDialog"),
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_material_presets(m_project_interactor.preset_interactor().material_presets()),
    m_material_filter(std::make_shared<Biz::ObservableListSortFilter<Biz::Preset::PresetItem>>()),
    m_material_searcher(
        std::make_shared<Biz::ObservableListSearcher<Biz::Preset::PresetItem>>(score_preset_item)
    )
{
    m_material_searcher->set_max_found_items(64);
    m_project_contexts = std::make_unique<Biz::ProjectScoped<ProjectContext>>(project_interactor);

    m_material_settings_dialog =
        content_item()->emplace_back<MaterialSettingsDialog>(project_interactor, m_navigator, this);
    m_material_presets.add_listener<Biz::IListObserver<Biz::Preset::PresetItemObservableList>>(
        this
    );

    content_item()->set_width(350);

    content()->set_orientation(Orientation::Vertical);
    content()->set_gap(5);

    Item* filters_wrap = content()->emplace_back<Item>();
    filters_wrap->set_orientation(Orientation::Horizontal);
    filters_wrap->set_gap(5.f);
    filters_wrap->set_flex_grow(1.f);

    for (const std::string& filter_btn_name :
         std::initializer_list<std::string>{_u8L("All"), "PLA", "PETG", "ASA"})
    {
        LayoutButton* btn = filters_wrap->emplace_back<LayoutButton>(filter_btn_name);
        btn->set_checkable(true);
        btn->set_flex_grow(1.f);
        m_type_filter_buttons[filter_btn_name] = btn;
        m_material_type_button_group.insert_button(btn);
    }
    m_material_type_button_group.callbacks().checked_changed =
        [this](AbstractButton* current_checked, AbstractButton*)
    {
        context().type_filter = dynamic_cast<LayoutButton*>(current_checked)->label();
        update_current_context();
        update_preset_list();
    };

    m_material_filter->set_filter_fn(
        [this](const Biz::Preset::PresetItem& data) -> bool
        {
            const std::string& type_filter =
                m_current_context.type_filter.empty() ? _u8L("All") : m_current_context.type_filter;
            if (type_filter != _u8L("All")) {
                const auto& selected_preset =
                    m_project_interactor.preset_interactor().selected_printer_preset();
                const auto& config = m_project_interactor.preset_interactor()
                                         .get_material_preset(
                                             selected_preset.hw_config.id,
                                             selected_preset.printer.id,
                                             selected_preset.print.id,
                                             m_material_index,
                                             data.id
                                         )
                                         .first.get()
                                         .config_box();
                if (config.items.opt("filament_type").get<std::string>() != type_filter) {
                    return false;
                }
            }
            if (m_only_favorites_button->checked() && !is_favorite(data)) {
                return false;
            }

            return true;
        }
    );

    content()->emplace_back<Separator>(Orientation::Horizontal);

    Item* search_row = content()->emplace_back<Item>();
    search_row->set_gap(5);
    Icon* icon = search_row->emplace_back<Icon>(Render::Icon::Search);
    icon->set_width(16);
    icon->set_fill_mode(Icon::FillMode::PreservedAspectCentered);
    m_input_text_search = search_row->emplace_back<InputText>();
    m_input_text_search->set_hint(_u8L("Search..."));
    m_input_text_search->set_flex_grow(1);
    m_input_text_search->callbacks().text_changed = [this]()
    { m_material_searcher->set_search_text(m_input_text_search->text()); };

    m_only_favorites_button = search_row->emplace_back<LayoutButton>(
        std::string(),
        Render::Icon::Star,
        Biz::_u8L("Only favorites")
    );
    m_only_favorites_button->set_width(24);
    m_only_favorites_button->set_height(24);
    m_only_favorites_button->set_checkable(true);
    m_only_favorites_button->set_self_align(YGAlignCenter);
    m_only_favorites_button->callbacks().checked_changed = [this](bool checked)
    {
        m_only_favorites_button->set_icon(checked ? Render::Icon::StarSolid : Render::Icon::Star);
        m_only_favorites_button->set_tooltip(
            checked ? Biz::_u8L("Show all items") : Biz::_u8L("Filter only favorited items")
        );
        m_material_filter->invalidate();
        AppServices::instance().app_config_interactor().set_item_value(
            "materials_only_favorites",
            Domain::ConfigValue{checked}
        );
        update_preset_list();
    };
    on_app_config_changed("materials_only_favorites");

    content()->emplace_back<Separator>(Orientation::Horizontal);

    Item* scroll_area = content()->emplace_back<Item>();
    scroll_area->set_orientation(Orientation::Vertical);
    scroll_area->set_min_height(200);
    scroll_area->set_max_height(300);

    m_selection_row_list_view =
        scroll_area->emplace_back<SelectionRowListView>(SelectionRowListViewFactory{
            [this]() { m_navigator.set_opened_dialog(nullptr); },
            [this](size_t index)
            {
                const Biz::Preset::PresetItem& preset_item =
                    *m_selection_row_list_view->item_at(index)->state();
                AppServices::instance()
                    .app_config()
                    .app_settings_advanced()
                    .toggle_material_favorite_preset(preset_item.id);
                m_material_filter->invalidate();
                if (!is_favorite(preset_item) && m_only_favorites_button->checked()) {
                    on_list_selection_changed(m_preset_list->selected_index());
                }
            },
            [this](size_t index)
            {
                return index < m_selection_row_list_view->list_item_count()
                    && is_favorite(*m_selection_row_list_view->item_at(index)->state());
            },
            [this]()
            {
                if (!m_material_settings_dialog->opened()) {
                    m_navigator.set_opened_dialog(m_material_settings_dialog);
                }
            },
            m_material_index,
            m_project_interactor.preset_interactor()
        });
    m_selection_row_list_view->set_orientation(Orientation::Vertical);
    m_selection_row_list_view->set_gap(5);
    m_selection_row_list_view->set_margin({0.f, 0.f, -10.f, 0.f});
    m_selection_row_list_view->set_padding({0.f, 0.f, 15.f, 0.f});
    m_selection_row_list_view->set_source_list(m_material_searcher.get());

    m_material_searcher->set_source_model(m_material_filter.get());

    // Material Settings Dialog setup
    m_material_settings_dialog->attach_to_item(content_item(), Position::Left);

    m_material_settings_dialog->dialog_callbacks().tab_selected = [this](size_t current_index)
    {
        if (m_callbacks.advanced_settings_tab_opened) {
            m_callbacks.advanced_settings_tab_opened(current_index);
        }
    };

    set_material_index(0);
}

MaterialSelectionDialog::~MaterialSelectionDialog()
{
    m_material_presets.remove_listener<Biz::IListObserver<Biz::Preset::PresetItemObservableList>>(
        this
    );
}

MaterialSelectionDialog::Callbacks& MaterialSelectionDialog::material_selection_callbacks()
{
    return m_callbacks;
}

void MaterialSelectionDialog::set_material_index(size_t material_index)
{
    if (m_material_index == material_index) {
        return;
    }

    m_material_index = material_index;

    update_preset_list();
}

void MaterialSelectionDialog::on_list_selection_changed(Domain::SelectionId new_selection)
{
    const Biz::Preset::PresetItem& selected_preset_item = m_preset_list->items().at(new_selection);

    for (size_t index = 0; index < m_selection_row_list_view->list_item_count(); ++index) {
        AbstractButton* button =
            dynamic_cast<AbstractButton*>(m_selection_row_list_view->item_at(index));
        ASSERT(button);
        if (m_material_searcher->at(index).id == selected_preset_item.id) {
            button->set_checked(true);
            m_selection_row_list_view->scroll_at_item(m_selection_row_list_view->item_at(index));
        } else {
            button->set_checked(false);
        }
    }
}

void MaterialSelectionDialog::on_will_be_reset(std::optional<size_t> new_size)
{
    if (m_preset_list) {
        m_preset_list->remove_listener<Biz::IListSelectionChangedListener>(this);
        m_preset_list = nullptr;
    }

    m_material_filter->set_source_model(nullptr);
}

void MaterialSelectionDialog::on_reset()
{
    m_material_index = m_material_presets.size() ?
        std::clamp(m_material_index, std::size_t{0}, m_material_presets.size() - 1) :
        Domain::INVALID_ID;

    update_current_context();
    update_preset_list();
}

void MaterialSelectionDialog::close_action()
{
    m_navigator.set_opened_dialog(nullptr);
}

void MaterialSelectionDialog::update_preset_list()
{
    if (m_preset_list) {
        m_preset_list->remove_listener<Biz::IListSelectionChangedListener>(this);
        m_preset_list = nullptr;
    }

    if (m_material_index == Domain::INVALID_ID) {
        return;
    }

    m_preset_list = &m_material_presets.at(m_material_index);
    m_preset_list->add_listener<Biz::IListSelectionChangedListener>(this);

    m_material_filter->set_source_model(&m_preset_list->items());
    on_list_selection_changed(m_preset_list->selected_index());
}

MaterialSettingsDialog& MaterialSelectionDialog::material_settings_dialog()
{
    return *m_material_settings_dialog;
}

void MaterialSelectionDialog::on_app_config_changed(const std::string& key)
{
    if (key == "materials_only_favorites") {
        m_only_favorites_button->set_checked(
            AppServices::instance()
                .app_config()
                .get_config_box()
                .items.find("materials_only_favorites")
                ->value()
                .get<bool>()
        );
    }
}

} // namespace Slic3r::App
