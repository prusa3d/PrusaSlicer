#include "Slic3r/App/Config/PrintToolSubcategoryItem.hpp"

#include <Slic3r/Domain/Config.hpp>

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Imgui/ImguiExtension.hpp"

#include "Slic3r/Biz/PrintToolConfigBoxInteractor.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"
#include <Slic3r/Biz/I18N/I18N.hpp>

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrintToolSubcategoryItem::PrintToolSubcategoryItem(
    size_t index,
    const Biz::PrintToolItem& data,
    Biz::PrintToolConfigBoxInteractor& cbi,
    Biz::IConfigBoxSetter& cbi_setter,
    Biz::ProjectInteractor& project_interactor
) :
    Biz::DataObserver<Biz::PrintToolItem>(index, data),
    m_cbi(cbi),
    m_cbi_setter(cbi_setter),
    m_rows_filter_list(std::make_shared<Biz::ObservableListSortFilter<Biz::PrintToolItem>>())
{
    set_object_name("PrintToolSubcategoryItem");
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);
    set_flags(ImDrawFlags_None);
    set_rounding(0);
    set_gap(0);
    set_padding(Paddings(20.f, 20.f, 20.f, 0.f));

    m_label = emplace_back<Text>(
        Biz::_u8(
            Domain::ConfigItemDef::translate_option_group(m_state->print_item->def().option_group)
        ),
        Render::ImguiFontType::Bold
    );

    m_rows_filter_list->set_filter_fn(
        [this](const Biz::PrintToolItem& item) -> bool
        {
            return item.print_item->def().option_group == m_option_group
                && item.print_item->def().category == m_category;
        }
    );
    // also group by row_group
    m_rows_filter_list->set_group_by_fn(
        [](const Biz::PrintToolItem& item, std::unordered_set<std::string>& seen_keys) -> bool
        {
            const std::string& row_group = item.print_item->def().row_group;
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
        [](const Biz::PrintToolItem& lhs, const Biz::PrintToolItem& rhs)
        { return lhs.print_item->def().order < rhs.print_item->def().order; }
    );

    m_rows_filter_list->set_source_model(m_cbi.observable_list());

    m_rows_list_view = emplace_back<PrintToolRowListView>(
        PrintToolRowListViewFactory{m_cbi, m_cbi_setter, project_interactor}
    );
    m_rows_list_view->set_object_name("PrintToolRowListView");
    m_rows_list_view->set_orientation(Orientation::Vertical);
    m_rows_list_view->set_gap(5);
    m_rows_list_view->set_padding(20);

    on_index_update();
    on_data_update();

    m_rows_list_view->set_source_list(m_rows_filter_list.get());
}

void PrintToolSubcategoryItem::navigate_to_item(const Domain::ConfigItem* config_item)
{
    const std::string& name = config_item->name();
    for (size_t row_index = 0; row_index < m_rows_filter_list->size(); ++row_index) {
        if (m_rows_filter_list->at(row_index).name == name) {
            m_rows_list_view->item_at(row_index)->navigate_to_item(config_item);
            break;
        }
    }
}

void PrintToolSubcategoryItem::clear_navigation()
{
    for (size_t row_index = 0; row_index < m_rows_list_view->object_count(); ++row_index) {
        m_rows_list_view->item_at(row_index)->clear_navigation();
    }
}

void PrintToolSubcategoryItem::on_data_update()
{
    m_label->set_text(
        Biz::_u8(
            Domain::ConfigItemDef::translate_option_group(m_state->print_item->def().option_group)
        )
    );

    const Domain::ConfigItemDef::OptionGroup option_group = m_state->print_item->def().option_group;
    const Domain::ConfigItemDef::Category category        = m_state->print_item->def().category;

    if (option_group != m_option_group || m_category != category) {
        m_option_group = option_group;
        m_category     = category;
        m_rows_filter_list->invalidate();
    }
}

void PrintToolSubcategoryItem::on_index_update()
{
    ImColor color = m_theme->color_imgui(Platform::Color::WindowBg);
    set_fill(m_index % 2 == 0 ? color : Imgui::adjust_brightness(color, 0.9f));
}

} // namespace Slic3r::App
