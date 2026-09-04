#pragma once

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Config/PrintToolRowItem.hpp"

namespace Slic3r::Biz {
class PrintToolConfigBoxInteractor;
class IConfigBoxSetter;
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Text;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class PrintToolFavoritesOptionGroup :
    public Biz::DataObserver<Biz::PrintToolItem>,
    public Yoga::Item
{
public:
    explicit PrintToolFavoritesOptionGroup(
        size_t index,
        const Biz::PrintToolItem& data,
        Biz::ProjectInteractor& project_interactor
    );

protected:
    void on_data_update() override;

private:
    using PrintToolRowFilter = Biz::ObservableListSortFilter<Biz::PrintToolItem>;

    using PrintToolRowListViewFactory = Yoga::ViewFactory<
        PrintToolRowItem,
        Biz::PrintToolItem,
        Biz::PrintToolConfigBoxInteractor&,
        Biz::IConfigBoxSetter&,
        Biz::ProjectInteractor&,
        PrintToolRowItemDisplayOptions>;
    using PrintToolRowListView =
        Yoga::ListView<PrintToolRowItem, Biz::PrintToolItem, PrintToolRowListViewFactory>;

    Biz::UnsharedPointer<PrintToolRowFilter> m_favorites_filter;

    Biz::ProjectInteractor& m_project_interactor;
    Yoga::Text* m_text_group_name{nullptr};

    PrintToolRowListView* m_favorites_list_view{nullptr};
    Domain::ConfigItemDef::Category m_category{Domain::ConfigItemDef::Category::Unknown};
};

} // namespace Slic3r::App
