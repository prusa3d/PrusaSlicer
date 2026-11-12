#pragma once

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include <Slic3r/Biz/ConfigItemContext.hpp>

#include "Slic3r/App/Config/ConfigSubcategoryItem.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"

namespace Slic3r::Biz {
class ConfigBoxInteractor;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Rectangle;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

using ConfigSubcategoryListViewFactory = Yoga::ViewFactory<
    ConfigSubcategoryItem,
    Biz::ConfigItemContext,
    Biz::IConfigBoxSetter&,
    Biz::ConfigBoxInteractor&,
    size_t>;

class ConfigSubcategoryListView :
    public Yoga::ListView<
        ConfigSubcategoryItem,
        Biz::ConfigItemContext,
        ConfigSubcategoryListViewFactory,
        Yoga::ScrollArea>,
    public Biz::DataObserver<Biz::ConfigItemContext>,
    public IConfigNavigable
{
public:
    explicit ConfigSubcategoryListView(
        size_t index,
        const Biz::ConfigItemContext& data,
        Biz::IConfigBoxSetter& cbi_container,
        Biz::ConfigBoxInteractor& cbi,
        size_t cbi_index
    );

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

protected:
    void on_data_update() override;

private:
    Biz::IConfigBoxSetter& m_cbi_container;
    Biz::ConfigBoxInteractor& m_cbi;
    size_t m_cbi_index{0};

    Biz::UnsharedPointer<
        Biz::ObservableListSortFilter<Biz::ConfigItemContext, Domain::ConfigItemDef::OptionGroup>>
        m_category_filter;
    Yoga::Rectangle* m_background{nullptr};
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};
};

} // namespace Slic3r::App
