#include "Slic3r/App/PrintToolFavoritesOptionGroup.hpp"

#include <Slic3r/Domain/Config.hpp>

#include "Slic3r/Biz/PrintToolConfigBoxInteractor.hpp"
#include "Slic3r/Biz/PrintToolConfigObservableList.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

#include "Slic3r/App/Yoga/Text.hpp"
#include "Slic3r/App/Yoga/Separator.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

PrintToolFavoritesOptionGroup::PrintToolFavoritesOptionGroup(
    size_t index,
    const Biz::PrintToolItem& data,
    Biz::ProjectInteractor& project_interactor
) :
    Biz::DataObserver<Biz::PrintToolItem>(index, data),
    m_favorites_filter(std::make_shared<PrintToolRowFilter>()),
    m_project_interactor(project_interactor)
{
    constexpr float v_padding = 10.f;
    set_padding({0, v_padding});

    set_orientation(Orientation::Vertical);
    set_gap(5);

    m_text_group_name = emplace_back<Text>(std::string());
    m_text_group_name->set_font_type(Render::ImguiFontType::Bold);
    m_text_group_name->set_flex_grow(1);

    m_favorites_filter->set_filter_fn(
        [this](const Biz::PrintToolItem& item) -> bool
        { return item.is_favorite && item.print_item->def().category == m_category; }
    );
    m_favorites_filter->set_sort_fn(
        [](const Biz::PrintToolItem& lhs, const Biz::PrintToolItem& rhs)
        {
            if (lhs.print_item->def().option_group != rhs.print_item->def().option_group)
                return lhs.print_item->def().option_group < rhs.print_item->def().option_group;
            return lhs.print_item->def().order < rhs.print_item->def().order;
        }
    );

    m_favorites_filter->set_source_model(
        project_interactor.preset_interactor().print_tool_cbi().observable_list()
    );

    m_favorites_list_view = emplace_back<PrintToolRowListView>(PrintToolRowListViewFactory{
        project_interactor.preset_interactor().print_tool_cbi(),
        project_interactor.preset_interactor(),
        project_interactor,
        {.show_favorites = false, .show_explanation = false}
    });
    m_favorites_list_view->set_orientation(Orientation::Vertical);
    m_favorites_list_view->set_gap(0);
    m_favorites_list_view->set_source_list(m_favorites_filter.get());

    emplace_back<Separator>(Orientation::Horizontal)->set_margin({0, 0, 0, -v_padding});

    on_data_update();
}

void PrintToolFavoritesOptionGroup::on_data_update()
{
    m_text_group_name->set_text(
        Domain::ConfigItemDef::translate_category(
            m_state->print_item->def().category,
            m_project_interactor.selected_config_container().print_technology()
        )
    );

    const Domain::ConfigItemDef::Category category = m_state->print_item->def().category;
    if (m_category != category) {
        m_category = category;
        m_favorites_filter->invalidate();
    }
}

} // namespace Slic3r::App
