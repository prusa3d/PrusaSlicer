#include "Slic3r/App/Config/PrintToolSubcategoryListView.hpp"

#include <Slic3r/Domain/Config.hpp>

#include "Slic3r/Biz/PrintToolConfigBoxInteractor.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrintToolSubcategoryListView::PrintToolSubcategoryListView(
    size_t index,
    const Biz::PrintToolItem& data,
    Biz::PrintToolConfigBoxInteractor& cbi,
    Biz::IConfigBoxSetter& cbi_setter,
    Biz::ProjectInteractor& project_interactor
) :
    ListView<
        PrintToolSubcategoryItem,
        Biz::PrintToolItem,
        PrintToolSubcategoryListViewFactory,
        ScrollArea>(PrintToolSubcategoryListViewFactory{cbi, cbi_setter, project_interactor}),
    Biz::DataObserver<Biz::PrintToolItem>(index, data),
    m_cbi(cbi),
    m_cbi_setter(cbi_setter),
    m_category_filter(
        std::make_shared<
            Biz::ObservableListSortFilter<Biz::PrintToolItem, Domain::ConfigItemDef::OptionGroup>>()
    )
{
    set_object_name("PrintToolSubcategoryListView");
    set_orientation(Orientation::Vertical);
    set_flex_grow(1);
    set_gap(0);
    set_flex_grow(1);
    set_min_height(100);

    m_category_filter->set_filter_fn(
        [this](const Biz::PrintToolItem& tool_print_item)
        { return tool_print_item.print_item->def().category == m_category; }
    );
    m_category_filter->set_group_by_fn(
        [](const Biz::PrintToolItem& tool_print_item,
           std::unordered_set<Domain::ConfigItemDef::OptionGroup>& seen_keys)
        {
            if (seen_keys.contains(tool_print_item.print_item->def().option_group)) {
                return true;
            } else {
                seen_keys.insert(tool_print_item.print_item->def().option_group);
                return false;
            }
        }
    );
    m_category_filter->set_sort_fn(
        [](const Biz::PrintToolItem& lhs, const Biz::PrintToolItem& rhs)
        { return lhs.print_item->def().option_group < rhs.print_item->def().option_group; }
    );

    m_category_filter->set_source_model(m_cbi.observable_list());

    set_source_list(m_category_filter.get());

    on_data_update();
}

void PrintToolSubcategoryListView::navigate_to_item(const Domain::ConfigItem* config_item)
{
    const Domain::ConfigItemDef::OptionGroup option_group = config_item->def().option_group;
    for (size_t row_index = 0; row_index < m_category_filter->size(); ++row_index) {
        if (m_category_filter->at(row_index).print_item->def().option_group == option_group) {
            PrintToolSubcategoryItem* found_item = item_at(row_index);
            found_item->navigate_to_item(config_item);
            scroll_at_item(found_item);
            break;
        }
    }
}

void PrintToolSubcategoryListView::clear_navigation()
{
    for (size_t subcategory_index = 0; subcategory_index < m_category_filter->size();
         ++subcategory_index)
    {
        item_at(subcategory_index)->clear_navigation();
    }
}

void PrintToolSubcategoryListView::on_data_update()
{
    Domain::ConfigItemDef::Category category = m_state->print_item->def().category;
    if (m_category != category) {
        m_category = category;
        m_category_filter->invalidate();
    }
}

} // namespace Slic3r::App
