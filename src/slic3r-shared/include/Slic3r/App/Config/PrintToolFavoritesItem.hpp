#pragma once

#include <Slic3r/Domain/ConfigDef.hpp>

#include "Slic3r/Biz/ObservableListSortFilter.hpp"
#include "Slic3r/Biz/PrintToolItem.hpp"
#include "Slic3r/App/PrintToolFavoritesOptionGroup.hpp"

#include "Slic3r/App/Config/PrintToolRowItem.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::Biz {
class PrintToolConfigBoxInteractor;
class IConfigBoxSetter;
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class PrintToolFavoritesItem : public Yoga::Item
{
    using ObservableFavoritesCategorizer =
        Biz::ObservableListSortFilter<Biz::PrintToolItem, Domain::ConfigItemDef::Category>;

    using PrintToolFavoritesOptionGroupListViewFactory = Yoga::
        ViewFactory<PrintToolFavoritesOptionGroup, Biz::PrintToolItem, Biz::ProjectInteractor&>;
    using PrintToolFavoritesOptionGroupListView = Yoga::ListView<
        PrintToolFavoritesOptionGroup,
        Biz::PrintToolItem,
        PrintToolFavoritesOptionGroupListViewFactory>;

public:
    PrintToolFavoritesItem(Biz::ProjectInteractor& project_interactor);

private:
    Biz::UnsharedPointer<ObservableFavoritesCategorizer> m_favorites_categorizer;

    PrintToolFavoritesOptionGroupListView* m_categories_view{nullptr};
};

} // namespace Slic3r::App
