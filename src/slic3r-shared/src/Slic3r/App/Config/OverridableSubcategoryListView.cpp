#include "Slic3r/App/Config/OverridableSubcategoryListView.hpp"

#include "Slic3r/Domain/Config.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/OverridableConfigBoxInteractor.hpp"
#include "Slic3r/Biz/OverridableConfigBoxObservableList.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

OverridableSubcategoryListView::OverridableSubcategoryListView(
    size_t index,
    const Biz::OverrideItem& data,
    Biz::IConfigBoxSetter& cbi_container,
    Biz::OverridableConfigBoxInteractor& cbi,
    size_t cbi_index
) :
    ListView<
        OverridableSubcategoryItem,
        Biz::OverrideItem,
        OverridableSubcategoryListViewFactory,
        ScrollArea>(OverridableSubcategoryListViewFactory{cbi_container, cbi, cbi_index}),
    Biz::DataObserver<Biz::OverrideItem>(index, data),
    m_cbi_container(cbi_container),
    m_cbi(cbi),
    m_cbi_index(cbi_index),
    m_category_filter(
        std::make_shared<
            Biz::ObservableListSortFilter<Biz::OverrideItem, Domain::ConfigItemDef::OptionGroup>>()
    )
{
    set_object_name("OverridableSubcategoryListView");
    set_orientation(Orientation::Vertical);
    set_flex_grow(1);
    set_gap(5);
    set_flex_grow(1);
    set_min_height(100);

    m_category_filter->set_filter_fn(
        [this](const Biz::OverrideItem& override_item)
        {
            return override_item.config_item->def().category == m_category
                || (override_item.is_override()
                    && m_category == Domain::ConfigItemDef::Category::Filament_Overrides);
        }
    );
    m_category_filter->set_group_by_fn(
        [](const Biz::OverrideItem& override_item,
           std::unordered_set<Domain::ConfigItemDef::OptionGroup>& seen_keys)
        {
            if (seen_keys.contains(override_item.config_item->def().option_group)) {
                return true;
            } else {
                seen_keys.insert(override_item.config_item->def().option_group);
                return false;
            }
        }
    );

    m_category_filter->set_sort_fn(
        [](const Biz::OverrideItem& lhs, const Biz::OverrideItem& rhs)
        { return lhs.config_item->def().option_group < rhs.config_item->def().option_group; }
    );

    m_category_filter->set_source_model(m_cbi.config_box_overridable_list());

    set_source_list(m_category_filter.get());

    on_data_update();
}

void OverridableSubcategoryListView::navigate_to_item(const Domain::ConfigItem* config_item)
{
    const Domain::ConfigItemDef::OptionGroup option_group = config_item->def().option_group;
    for (size_t subcategory_index = 0; subcategory_index < m_category_filter->size();
         ++subcategory_index)
    {
        if (m_category_filter->at(subcategory_index).config_item->def().option_group
            == option_group)
        {
            OverridableSubcategoryItem* found_item = item_at(subcategory_index);
            found_item->navigate_to_item(config_item);
            scroll_at_item(found_item);
            break;
        }
    }
}

void OverridableSubcategoryListView::clear_navigation()
{
    for (size_t subcategory_index = 0; subcategory_index < m_category_filter->size();
         ++subcategory_index)
    {
        item_at(subcategory_index)->clear_navigation();
    }
}

void OverridableSubcategoryListView::on_data_update()
{
    Domain::ConfigItemDef::Category category = m_state->config_item->def().category;
    if (m_category != category) {
        m_category = category;
        m_category_filter->invalidate();
    }
}

} // namespace Slic3r::App
