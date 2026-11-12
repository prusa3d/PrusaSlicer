#include "Slic3r/App/Config/ConfigSubcategoryListView.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/ConfigBoxInteractor.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

ConfigSubcategoryListView::ConfigSubcategoryListView(
    size_t index,
    const Biz::ConfigItemContext& data,
    Biz::IConfigBoxSetter& cbi_container,
    Biz::ConfigBoxInteractor& cbi,
    size_t cbi_index
) :
    ListView<
        ConfigSubcategoryItem,
        Biz::ConfigItemContext,
        ConfigSubcategoryListViewFactory,
        ScrollArea>(ConfigSubcategoryListViewFactory{cbi_container, cbi, cbi_index}),
    Biz::DataObserver<Biz::ConfigItemContext>(index, data),
    m_cbi_container(cbi_container),
    m_cbi(cbi),
    m_cbi_index(cbi_index),
    m_category_filter(
        std::make_shared<Biz::ObservableListSortFilter<
            Biz::ConfigItemContext,
            Domain::ConfigItemDef::OptionGroup>>()
    )
{
    set_object_name("ConfigSubcategoryListView");
    set_orientation(Orientation::Vertical);
    set_flex_grow(1);
    set_gap(0);
    set_flex_grow(1);
    set_min_height(100);

    m_category_filter->set_filter_fn([this](const Biz::ConfigItemContext& data)
                                     { return data.config_item->def().category == m_category; });
    m_category_filter->set_group_by_fn(
        [](const Biz::ConfigItemContext& data,
           std::unordered_set<Domain::ConfigItemDef::OptionGroup>& seen_keys)
        {
            if (seen_keys.contains(data.config_item->def().option_group)) {
                return true;
            } else {
                seen_keys.insert(data.config_item->def().option_group);
                return false;
            }
        }
    );

    m_category_filter->set_sort_fn(
        [](const Biz::ConfigItemContext& lhs, const Biz::ConfigItemContext& rhs)
        { return lhs.config_item->def().option_group < rhs.config_item->def().option_group; }
    );

    m_category_filter->set_source_model(m_cbi.config_box_list());

    set_source_list(m_category_filter.get());

    on_data_update();
}

void ConfigSubcategoryListView::navigate_to_item(const Domain::ConfigItem* config_item)
{
    const Domain::ConfigItemDef::OptionGroup option_group = config_item->def().option_group;
    for (size_t subcategory_index = 0; subcategory_index < m_category_filter->size();
         ++subcategory_index)
    {
        if (m_category_filter->at(subcategory_index).config_item->def().option_group
            == option_group)
        {
            ConfigSubcategoryItem* found_item = item_at(subcategory_index);
            found_item->navigate_to_item(config_item);
            scroll_at_item(found_item);
            break;
        }
    }
}

void ConfigSubcategoryListView::clear_navigation()
{
    for (size_t subcategory_index = 0; subcategory_index < m_category_filter->size();
         ++subcategory_index)
    {
        item_at(subcategory_index)->clear_navigation();
    }
}

void ConfigSubcategoryListView::on_data_update()
{
    Domain::ConfigItemDef::Category category = m_state->config_item->def().category;
    if (m_category != category) {
        m_category = category;
        m_category_filter->invalidate();
    }
}

} // namespace Slic3r::App
