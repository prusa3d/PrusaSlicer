#pragma once

#include "Slic3r/App/Yoga/ComboBox.hpp"
#include "Slic3r/Biz/IListObserver.hpp"
#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/Biz/IObservableList.hpp"

namespace Slic3r::App::Yoga {

template <class Data>
class ComboBoxListView : public Biz::IListObserver<Data>, public ComboBox
{
public:
    using GetNameFn = std::function<std::string(const Data* item)>;
    using Items     = std::vector<std::unique_ptr<Biz::DataObserver<Data>>>;

    ComboBoxListView(const std::string& name = "ComboBoxListView") : ComboBox(name) {}

    void set_get_name_fn(const GetNameFn& get_name_fn)
    {
        m_get_name_fn = get_name_fn;
    }

    template <
        typename Derived,
        typename = std::enable_if_t<std::is_base_of_v<Biz::IObservableList<Data>, Derived>>>
    void set_source_list(const std::weak_ptr<Derived>& source_list)
    {
        set_source_list(
            Biz::WeakerPointer<Biz::IObservableList<Data>>{
                std::static_pointer_cast<Biz::IObservableList<Data>>(source_list.lock())
            }
        );
    }

    void set_source_list(const Biz::WeakerPointer<Biz::IObservableList<Data>>& source_list)
    {
        if (m_source_list != source_list) {
            if (m_source_list.is_valid()) {
                m_source_list->template remove_listener<Biz::IListObserver<Data>>(this);
            }

            m_source_list = source_list;
            if (m_source_list.get()) {
                m_source_list->template add_listener<Biz::IListObserver<Data>>(this);
            }

            on_reset();
        }
    }

    void on_inserted(const Data& data, size_t index) override
    {
        ASSERT(index <= m_list_items.size());

        m_list_items.emplace(
            m_list_items.cbegin() + index,
            std::make_unique<Biz::DataObserver<Data>>(index, data)
        );
        m_items.emplace(m_items.cbegin() + index, m_get_name_fn(&data));

        update_indexes(++index);
    }

    void on_removed(const Biz::IndexRange& index_range) override
    {
        ASSERT(index_range.to <= m_list_items.size());

        m_list_items.erase(
            m_list_items.cbegin() + index_range.from,
            m_list_items.cbegin() + index_range.to + 1
        );
        m_items.erase(m_items.cbegin() + index_range.from, m_items.cbegin() + index_range.to + 1);

        update_indexes(index_range.from);
    }

    void on_updated(const Biz::IndexRange& index_range) override
    {
        ASSERT(index_range.to <= m_list_items.size());

        size_t index = 0;
        for (std::unique_ptr<Biz::DataObserver<Data>>& view : m_list_items) {
            view->set_state(m_source_list->at(index));
            m_items[index] = m_get_name_fn(view->state());
            index++;
        }
    }

    void on_reset() override
    {
        m_list_items.clear();
        m_items.clear();

        if (m_source_list.is_valid()) {
            for (size_t index = 0; index < m_source_list->size(); ++index) {
                on_inserted(m_source_list->at(index), index);
            }
        }

        set_current_index(0);
    }

    void on_moved(size_t from, size_t to) override
    {
        ASSERT(from < m_list_items.size() && to < m_list_items.size());
        if (from == to) {
            return;
        }

        update_indexes(from);
    }

protected:
    /**
     * @note intentionally hidden from users of this class
     */
    void set_items(const std::vector<std::string>& items)
    {
        ComboBox::set_items(items);
    }

private:
    void update_indexes(size_t from)
    {
        // update indexes for remaining objects at index_range.from+
        for (size_t new_index = from; new_index < m_list_items.size(); ++new_index) {
            m_list_items.at(new_index)->set_state_index(new_index, m_source_list->at(new_index));
        }
    }

private:
    Biz::WeakerPointer<Biz::IObservableList<Data>> m_source_list = nullptr;
    Items m_list_items;

    GetNameFn m_get_name_fn;
};

} // namespace Slic3r::App::Yoga
