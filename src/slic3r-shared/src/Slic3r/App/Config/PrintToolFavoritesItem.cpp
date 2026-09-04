#include "Slic3r/App/Config/PrintToolFavoritesItem.hpp"

#include <Slic3r/Domain/Config.hpp>

#include "Slic3r/Biz/PrintToolConfigBoxInteractor.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrintToolFavoritesItem::PrintToolFavoritesItem(Biz::ProjectInteractor& project_interactor) :
    m_favorites_categorizer(std::make_shared<ObservableFavoritesCategorizer>())
{
    set_object_name("PrintToolFavoritesItem");
    set_orientation(Orientation::Vertical);
    set_flex_shrink(0);

    m_favorites_categorizer->set_filter_fn(
        [](const Biz::PrintToolItem& item) -> bool { return item.is_favorite; }
    );
    m_favorites_categorizer->set_group_by_fn(
        [](const Biz::PrintToolItem& item,
           std::unordered_set<Domain::ConfigItemDef::Category>& seen_keys)
        {
            Domain::ConfigItemDef::Category category = item.print_item->def().category;
            DEBUG_ASSERT(
                category != Domain::ConfigItemDef::Category::Unknown,
                "ConfigItemDef cannot have unknown category, please fill it."
            );

            if (seen_keys.contains(category)) {
                return true;
            } else {
                seen_keys.insert(category);
                return false;
            }
        }
    );
    m_favorites_categorizer->set_sort_fn(
        [](const Biz::PrintToolItem& lhs, const Biz::PrintToolItem& rhs)
        { return lhs.print_item->def().category < rhs.print_item->def().category; }
    );

    m_favorites_categorizer->set_source_model(
        project_interactor.preset_interactor().print_tool_cbi().observable_list()
    );

    m_categories_view = emplace_back<PrintToolFavoritesOptionGroupListView>(project_interactor);
    m_categories_view->set_object_name("PrintToolRowFavoritesListView");
    m_categories_view->set_orientation(Orientation::Vertical);

    m_categories_view->set_source_list(m_favorites_categorizer.get());
}

} // namespace Slic3r::App
