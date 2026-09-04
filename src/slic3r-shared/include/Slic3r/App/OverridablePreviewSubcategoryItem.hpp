#pragma once
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/OverrideItemPreviewRow.hpp"

#include "Slic3r/Domain/ConfigDef.hpp"

#include "Slic3r/Biz/OverrideItem.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

namespace Slic3r::App {

class OverridablePreviewSubcategoryItem :
    public Biz::DataObserver<Biz::OverrideItem>,
    public Yoga::Item
{
    using ConfigPreviewRowListViewFactory = Yoga::
        ViewFactory<OverrideItemPreviewRow, Biz::OverrideItem, Biz::Preset::PresetInteractor&>;

    using ConfigPreviewRowListView = Yoga::ListView<
        OverrideItemPreviewRow,
        Biz::OverrideItem,
        ConfigPreviewRowListViewFactory,
        Yoga::Item>;

public:
    OverridablePreviewSubcategoryItem(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::Preset::PresetInteractor& preset_interactor
    );

private:
    void on_data_update() override;

private:
    ConfigPreviewRowListView* m_rows_list_view{nullptr};
    Biz::UnsharedPointer<Biz::ObservableListSortFilter<Biz::OverrideItem>> m_rows_filter_list;
    Yoga::Text* m_label{nullptr};
    Domain::ConfigItemDef::OptionGroup m_option_group{Domain::ConfigItemDef::OptionGroup::Unknown};
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};
};

} // namespace Slic3r::App
