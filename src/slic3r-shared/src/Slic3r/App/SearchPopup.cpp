#include "Slic3r/App/SearchPopup.hpp"

#include "Slic3r/App/Yoga/Window.hpp"
#include "Slic3r/App/Yoga/ButtonGroup.hpp"
#include "Slic3r/App/SearchObservableList.hpp"
#include "Slic3r/App/Navigator.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

SearchPopup::SearchPopup(
    Biz::ProjectInteractor& project_interactor,
    Navigator& navigator,
    std::weak_ptr<SearchObservableList> search_observable_list
) :
    m_project_interactor(project_interactor),
    m_navigator(navigator),
    m_search_observable_list(search_observable_list),
    m_window(std::make_unique<Window>("SearchPopup")),
    m_button_group(std::make_shared<ButtonGroup>())
{
    set_content_item(m_window.release());

    m_window->set_orientation(Orientation::Vertical);
    m_window->set_gap(5);
    m_window->set_padding(Paddings(10, 15));
    m_window->set_width(350);
    m_window->set_flags(
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoFocusOnAppearing
    );

    m_button_group->set_always_checked(true);

    m_button_group->callbacks().pressed_primary = [this](AbstractButton* button, bool pressed)
    {
        if (pressed) {
            m_navigator.navigate_to_item(dynamic_cast<SearchResultRow*>(button)->state());
            close();
        }
    };

    m_results_list_view = m_window->emplace_back<ResultsListView>(
        ResultsViewFactory{m_navigator, m_project_interactor, m_button_group.get()}
    );
    m_results_list_view->set_orientation(Orientation::Vertical);
    m_results_list_view->set_gap(5);
    m_results_list_view->set_source_list(m_search_observable_list);

    // Todo: There should be some better solution
    // this is essentially a hack for updating tooltips
    callbacks().opened = [this] { m_results_list_view->set_enabled(true); };
    callbacks().closed = [this] { m_results_list_view->set_enabled(false); };
}

void SearchPopup::navigate_down()
{
    if (!m_results_list_view->object_count()) {
        return;
    }

    AbstractButton* checked     = m_button_group->checked_button();
    std::optional<size_t> index = m_results_list_view->index_of(checked);
    ASSERT(index.has_value());
    m_results_list_view->item_at(++index.value() % m_results_list_view->object_count())
        ->set_checked(true);
}

void SearchPopup::navigate_up()
{
    if (!m_results_list_view->object_count()) {
        return;
    }

    AbstractButton* checked     = m_button_group->checked_button();
    std::optional<size_t> index = m_results_list_view->index_of(checked);
    ASSERT(index.has_value());
    m_results_list_view
        ->item_at(
            static_cast<int>(index.value()) - 1 < 0 ? m_results_list_view->object_count() - 1 :
                                                      index.value() - 1
        )
        ->set_checked(true);
}

void SearchPopup::open_selected()
{
    AbstractButton* checked_button = m_button_group->checked_button();
    if (!checked_button) {
        return;
    }

    std::optional<size_t> index = m_results_list_view->index_of(checked_button);
    ASSERT(index.has_value());

    m_navigator.navigate_to_item(&m_search_observable_list.lock()->at(index.value()));

    close();
}

void SearchPopup::select_top()
{
    if (m_results_list_view->list_item_count()) {
        m_results_list_view->item_at(0)->set_checked(true);
    }
}

} // namespace Slic3r::App
