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
    using Source        = Biz::ObservableListWithSelection<Data>;
    using SourcePointer = Biz::WeakerPointer<Source>;

public:
    // Accept weak_ptr<Source> and weak_ptr<Derived>.
    template <class Derived>
        requires std::derived_from<Derived, Source>
    void set_source_list(const std::weak_ptr<Derived>& source_list)
    {
        set_source_list(SourcePointer{std::weak_ptr<Source>{source_list}});
    }

    // Accept WeakerPointer<Derived>.
    template <class Derived>
        requires std::derived_from<Derived, Source>
    void set_source_list(const Biz::WeakerPointer<Derived>& source_list)
    {
        set_source_list(SourcePointer{source_list});
    }

    // Accept raw pointers, including derived pointers and nullptr.
    void set_source_list(Source* source_list)
    {
        set_source_list(SourcePointer{source_list});
    }

    // Single implementation for all input forms.
    void set_source_list(const SourcePointer& source_list)
    {
        detach_selection_listener();
        m_source_selection_list = source_list;

        if (auto* source = m_source_selection_list.get()) {
            ComboBoxListView<Data>::set_source_list(&source->items());

            source->template add_listener<Biz::IListSelectionChangedListener>(this);

            ComboBoxListView<Data>::on_reset();
            on_list_selection_changed(source->selected_index());
        } else {
            ComboBoxListView<Data>::set_source_list(nullptr);
        }
    }

    ~ComboBoxListViewSelection()
    {
        detach_selection_listener();
    }

    void on_list_selection_changed(Domain::SelectionId new_selection) override
    {
        ComboBox::set_current_index(static_cast<int>(new_selection));
    }

private:
    void detach_selection_listener()
    {
        if (auto* source = m_source_selection_list.get()) {
            source->template remove_listener<Biz::IListSelectionChangedListener>(this);
        }
    }

    SourcePointer m_source_selection_list;
};

} // namespace Slic3r::App::Yoga
