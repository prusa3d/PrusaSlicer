#pragma once

#include "Slic3r/App/Yoga/ComboBoxListView.hpp"
#include "Slic3r/Biz/ObservableListWithSelection.hpp"

namespace Slic3r::App::Yoga {

template <class Data>
class ComboBoxListViewSelection :
    public ComboBoxListView<Data>,
    public Biz::IListSelectionChangedListener
{
public:
    void set_source_list(Biz::ObservableListWithSelection<Data>* source_list)
    {
        if (m_source_selection_list) {
            m_source_selection_list->template remove_listener<Biz::IListSelectionChangedListener>(
                this
            );
        }
        m_source_selection_list = source_list;

        if (m_source_selection_list) {
            ComboBoxListView<Data>::set_source_list(&m_source_selection_list->items());
            m_source_selection_list->template add_listener<Biz::IListSelectionChangedListener>(
                this
            );
            ComboBoxListView<Data>::on_reset();
            on_list_selection_changed(m_source_selection_list->selected_index());
        } else {
            ComboBoxListView<Data>::set_source_list(nullptr);
        }
    }

    void on_list_selection_changed(Domain::SelectionId new_selection) override
    {
        ComboBox::set_current_index(static_cast<int>(new_selection));
    }

private:
    Biz::ObservableListWithSelection<Data>* m_source_selection_list = nullptr;
};

} // namespace Slic3r::App::Yoga
