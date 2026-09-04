#include "Slic3r/App/Config/OverridableSubcategoryItem.hpp"

#include "Slic3r/Domain/Config.hpp"

#include "Slic3r/Biz/OverridableConfigBoxInteractor.hpp"
#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

OverridableSubcategoryItem::OverridableSubcategoryItem(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::IConfigBoxSetter& cbi_container,
    Biz::OverridableConfigBoxInteractor& cbi,
    size_t cbi_index
) :
    Biz::DataObserver<Biz::OverrideItem>(index, data),
    m_cbi(cbi),
    m_cbi_container(cbi_container),
    m_cbi_index(cbi_index),
    m_rows_filter_list(std::make_shared<Biz::ObservableListSortFilter<Biz::OverrideItem>>())
{
    set_object_name("OverridableSubcategoryItem");
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);
    set_flags(ImDrawFlags_None);
    set_rounding(0);
    set_gap(0);
    set_padding(20);

    m_label = emplace_back<Text>(
        Biz::_u8(Domain::ConfigItemDef::translate_option_group(m_state->config_item->def().option_group)),
        Render::ImguiFontType::Bold
    );

    m_rows_filter_list->set_filter_fn(
        [this](const Biz::OverrideItem& item) -> bool
        {
            return item.config_item->def().option_group == m_option_group
                && (item.config_item->def().category == m_category
                    || (item.is_override()
                        && m_category == Domain::ConfigItemDef::Category::Filament_Overrides));
        }
    );
    // also group by row_group
    m_rows_filter_list->set_group_by_fn(
        [](const Biz::OverrideItem& item, std::unordered_set<std::string>& seen_keys) -> bool
        {
            const std::string& row_group = item.config_item->def().row_group;
            if (row_group.empty()) {
                return false;
            } else {
                if (seen_keys.contains(row_group)) {
                    return true;
                } else {
                    seen_keys.insert(row_group);
                    return false;
                }
            }
        }
    );
    m_rows_filter_list->set_sort_fn(
        [](const Biz::OverrideItem& lhs, const Biz::OverrideItem& rhs)
        { return lhs.config_item->def().order < rhs.config_item->def().order; }
    );

    m_rows_filter_list->set_source_model(m_cbi.config_box_overridable_list());

    m_rows_list_view = emplace_back<ConfigRowListView>(
        ConfigRowListViewFactory{m_cbi_container, m_cbi, m_cbi_index}
    );
    m_rows_list_view->set_source_list(m_rows_filter_list.get());
    m_rows_list_view->set_orientation(Orientation::Vertical);
    m_rows_list_view->set_gap(0);
    m_rows_list_view->set_padding(Paddings(20.f, 20.f, 20.f, 0.f));

    on_index_update();
    on_data_update();
}

void OverridableSubcategoryItem::navigate_to_item(const Domain::ConfigItem* config_item)
{
    const std::string& name = config_item->name();
    for (size_t row_index = 0; row_index < m_rows_filter_list->size(); ++row_index) {
        if (m_rows_filter_list->at(row_index).name == name) {
            m_rows_list_view->item_at(row_index)->navigate_to_item(config_item);
            break;
        }
    }
}

void OverridableSubcategoryItem::clear_navigation()
{
    for (size_t row_index = 0; row_index < m_rows_list_view->object_count(); ++row_index) {
        m_rows_list_view->item_at(row_index)->clear_navigation();
    }
}

void OverridableSubcategoryItem::on_data_update()
{
    const Domain::ConfigItem* config_item = m_state->config_item;

    m_label->set_text(
        Biz::_u8(Domain::ConfigItemDef::translate_option_group(config_item->def().option_group))
    );

    const Domain::ConfigItemDef::OptionGroup option_group = config_item->def().option_group;
    const Domain::ConfigItemDef::Category category        = config_item->def().category;

    if (option_group != m_option_group || m_category != category) {
        m_option_group = option_group;
        m_category     = category;
        m_rows_filter_list->invalidate();
    }
}

void OverridableSubcategoryItem::on_index_update()
{
    ImColor color = m_theme->color_imgui(Platform::Color::WindowBg);
    set_fill(m_index % 2 == 0 ? color : Imgui::adjust_brightness(color, 0.9f));
}

} // namespace Slic3r::App
