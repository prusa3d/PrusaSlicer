#pragma once

#include "Slic3r/Biz/IObservableList.hpp"

#include <vector>

namespace Slic3r::Biz {

/**
 * @brief The ObservableList class is a "Dummy" implementation of IObservableList
 * in most use-cases you actually want to just derive from IObservableList yourself
 * and add your own implementation
 */
template <class Data>
class ObservableList : public IObservableList<Data>
{
public:
    void append(Data data)
    {
        insert(data, m_data_items.size());
    }

    void insert(Data data, size_t index)
    {
        ASSERT(index <= m_data_items.size());
        m_data_items.insert(m_data_items.cbegin() + index, data);
        this->template invoke_listeners<IListObserver<Data>>([&](auto* l) {
            l->on_inserted(m_data_items.at(index), index);
        });
    }

    void remove(const IndexRange& index_range)
    {
        ASSERT(index_range.is_valid() && index_range.to < m_data_items.size());

        this->template invoke_listeners<IListObserver<Data>>([this, index_range](auto* l) {
            l->on_will_be_removed(index_range);
        });

        m_data_items.erase(
            m_data_items.begin() + index_range.from,
            m_data_items.begin() + index_range.to + 1
        );

        this->template invoke_listeners<IListObserver<Data>>([this, index_range](auto* l) {
            l->on_removed(index_range);
        });
    }

    void set(Data data, size_t index)
    {
        ASSERT(index < m_data_items.size());

        m_data_items[index] = data;

        this->template invoke_listeners<IListObserver<Data>>([&](auto* l) { l->on_updated({index}); });
    }

    void reset(std::initializer_list<Data> data)
    {
        this->template invoke_listeners<IListObserver<Data>>([&](IListObserver<Data>* l) {
            l->on_will_be_reset();
        });

        m_data_items = data;

        this->template invoke_listeners<IListObserver<Data>>([&](IListObserver<Data>* l) {
            l->on_reset();
        });
    }

    template <typename Container>
    void reset(Container&& data)
    {
        this->template invoke_listeners<IListObserver<Data>>([&](IListObserver<Data>* l) {
            l->on_will_be_reset();
        });

        m_data_items.clear();
        m_data_items.insert(
            m_data_items.end(),
            std::make_move_iterator(std::begin(data)),
            std::make_move_iterator(std::end(data))
        );
        this->template invoke_listeners<IListObserver<Data>>([&](IListObserver<Data>* l) {
            l->on_reset();
        });
    }

    void move(size_t from, size_t to)
    {
        ASSERT(from < m_data_items.size() && to < m_data_items.size());
        if (from == to) {
            return;
        }

        if (from > to) {
            std::rotate(
                m_data_items.rend() - from - 1,
                m_data_items.rend() - from,
                m_data_items.rend() - to
            );
        } else {
            std::rotate(
                m_data_items.begin() + from,
                m_data_items.begin() + from + 1,
                m_data_items.begin() + to + 1
            );
        }

        this->template invoke_listeners<IListObserver<Data>>([&](IListObserver<Data>* l) {
            l->on_moved(from, to);
        });
    }

    const Data& at(size_t index) const override
    {
        return m_data_items.at(index);
    }

    size_t size() const override
    {
        return m_data_items.size();
    }

protected:
    /**
     * Rationale behind std::vector:
     * - We need Random-Access-Memory
     *
     * Consumers (DataObserver<Data>) have references to Data which we manage
     */
    std::vector<Data> m_data_items;
};

} // namespace Slic3r::Biz
