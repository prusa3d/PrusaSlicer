#pragma once

#include "Slic3r/App/SearchResultRow.hpp"
#include "Slic3r/App/Yoga/ListView.hpp"
#include "Slic3r/App/Yoga/Popup.hpp"

namespace Slic3r::Biz {
class ProjectInteractor;
} // namespace Slic3r::Biz

namespace Slic3r::App::Yoga {
class Window;
class ButtonGroup;
} // namespace Slic3r::App::Yoga

namespace Slic3r::App {

class Navigator;
class SearchObservableList;

class SearchPopup : public Yoga::Popup
{
public:
    explicit SearchPopup(
        Biz::ProjectInteractor& project_interactor,
        Navigator& navigator,
        std::weak_ptr<SearchObservableList> search_observable_list
    );

    void navigate_down();
    void navigate_up();
    void open_selected();
    void select_top();

private:
    using ResultsViewFactory = Yoga::ViewFactory<SearchResultRow, Domain::ConfigItem, Navigator&, Biz::ProjectInteractor&, std::weak_ptr<Yoga::ButtonGroup>>;
    using ResultsListView = Yoga::ListView<SearchResultRow, Domain::ConfigItem, ResultsViewFactory>;

    Biz::ProjectInteractor& m_project_interactor;
    Navigator& m_navigator;
    std::weak_ptr<SearchObservableList> m_search_observable_list;

    Yoga::Passthrough<Yoga::Window> m_window;
    Biz::UnsharedPointer<Yoga::ButtonGroup> m_button_group;
    ResultsListView* m_results_list_view{nullptr};
};

} // namespace Slic3r::App
