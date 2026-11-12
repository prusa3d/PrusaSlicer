#pragma once

#include "Slic3r/Biz/ObservableListSortFilter.hpp"

#include "Slic3r/App/Config/OverridableSubcategoryItem.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"

namespace Slic3r::Biz {
class OverridableConfigBoxInteractor;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Rectangle;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

using OverridableSubcategoryListViewFactory = Yoga::ViewFactory<
    OverridableSubcategoryItem,
    Biz::OverrideItem,
    Biz::IConfigBoxSetter&,
    Biz::OverridableConfigBoxInteractor&,
    size_t>;

class OverridableSubcategoryListView :
    public Yoga::ListView<
        OverridableSubcategoryItem,
        Biz::OverrideItem,
        OverridableSubcategoryListViewFactory,
        Yoga::ScrollArea>,
    public Biz::DataObserver<Biz::OverrideItem>,
    public IConfigNavigable
{
public:
    explicit OverridableSubcategoryListView(
        size_t index,
        const Biz::OverrideItem& data,
        Biz::IConfigBoxSetter& cbi_container,
        Biz::OverridableConfigBoxInteractor& cbi,
        size_t cbi_index
    );

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

protected:
    void on_data_update() override;

private:
    Biz::IConfigBoxSetter& m_cbi_container;
    Biz::OverridableConfigBoxInteractor& m_cbi;
    size_t m_cbi_index{0};

    Biz::UnsharedPointer<
        Biz::ObservableListSortFilter<Biz::OverrideItem, Domain::ConfigItemDef::OptionGroup>>
        m_category_filter;
    Yoga::Rectangle* m_background{nullptr};
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};
};

} // namespace Slic3r::App
