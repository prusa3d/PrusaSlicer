#pragma once

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"

#include "Slic3r/App/Config/PrintToolSubcategoryItem.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/ScrollArea.hpp"
#include "Slic3r/App/IConfigNavigable.hpp"

namespace Slic3r::Biz {
class PrintToolConfigBoxInteractor;
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Rectangle;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

using PrintToolSubcategoryListViewFactory = Yoga::ViewFactory<
    PrintToolSubcategoryItem,
    Biz::PrintToolItem,
    Biz::PrintToolConfigBoxInteractor&,
    Biz::IConfigBoxSetter&,
    Biz::ProjectInteractor&>;

class PrintToolSubcategoryListView :
    public Yoga::ListView<
        PrintToolSubcategoryItem,
        Biz::PrintToolItem,
        PrintToolSubcategoryListViewFactory,
        Yoga::ScrollArea>,
    public Biz::DataObserver<Biz::PrintToolItem>,
    public IConfigNavigable
{
public:
    explicit PrintToolSubcategoryListView(
        size_t index,
        const Biz::PrintToolItem& data,
        Biz::PrintToolConfigBoxInteractor& cbi,
        Biz::IConfigBoxSetter& cbi_setter,
        Biz::ProjectInteractor& project_interactor
    );

    void navigate_to_item(const Domain::ConfigItem* config_item) override;
    void clear_navigation() override;

protected:
    void on_data_update() override;

private:
    Biz::PrintToolConfigBoxInteractor& m_cbi;
    Biz::IConfigBoxSetter& m_cbi_setter;

    Biz::UnsharedPointer<
        Biz::ObservableListSortFilter<Biz::PrintToolItem, Domain::ConfigItemDef::OptionGroup>>
        m_category_filter;
    Yoga::Rectangle* m_background{nullptr};
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};
};

} // namespace Slic3r::App
