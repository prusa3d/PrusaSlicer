#pragma once

#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/IListObserver.hpp"

#include <unordered_set>

namespace Slic3r::Biz {

/**
 * @brief The ObservableListSortFilter class
 * @note it is advised that user first set all filter/sort/group function and then calls set_source_model,
 * otherwise invalidation will be done multiple times
 */
template <class Data, class GroupKey = std::string>
class ObservableListSortFilter : public Biz::IListObserver<Data>, public Biz::IObservableList<Data>
{
public:
    // Returns true if item should be included
    using FilterFn = std::function<bool(const Data& item)>;
    // Returns lhs < rhs operation
    using SortFn = std::function<int(const Data& lhs, const Data& rhs)>;
    // Returns true if item is groupped and should NOT be included
    using GroupByFn =
        std::function<bool(const Data& item, std::unordered_set<GroupKey>& seen_keys)>;

    virtual ~ObservableListSortFilter()
    {
        if (m_source_model.is_valid()) {
            m_source_model->template remove_listener<Biz::IListObserver<Data>>(this);
        }
    }

    void set_filter_fn(const FilterFn& filter_fn)
    {
        m_filter_fn = filter_fn;

        invalidate();
    }

    void set_sort_fn(const SortFn& sort_fn)
    {
        m_sort_fn = sort_fn;

        invalidate();
    }

    void set_group_by_fn(const GroupByFn& group_by)
    {
        m_group_by_fn = group_by;

        invalidate();
    }

    const Data& at(size_t index) const override
    {
        return m_source_model->at(m_mapped_indexes.at(index));
    }

    size_t size() const override
    {
        return m_mapped_indexes.size();
    }

    void on_inserted(const Data& data, size_t index) override
    {
        invalidate();
    }

    void on_removed(const IndexRange& index_range) override
    {
        invalidate();
    }

    void on_updated(const IndexRange& index_range) override
    {
        if (!invalidate() && size()) {
            update_range(index_range);
        }
    }

    void on_reset() override
    {
        if (!invalidate() && size()) {
            update_range({0, m_source_model->size() - 1});
        }
    }

    void on_moved(size_t from, size_t to) override
    {
        invalidate();
    }

    IObservableList<Data>* source_model() const
    {
        return m_source_model;
    }

    void set_source_model(IObservableList<Data>* source_model)
    {
        set_source_model(WeakerPointer<IObservableList<Data>>{source_model});
    }

    template <
        typename Derived,
        typename = std::enable_if_t<std::is_base_of_v<IObservableList<Data>, Derived>>>
    void set_source_model(const std::weak_ptr<Derived>& source_model)
    {
        set_source_model(
            WeakerPointer<IObservableList<Data>>{
                std::static_pointer_cast<IObservableList<Data>>(source_model.lock())
            }
        );
    }

    void set_source_model(const WeakerPointer<IObservableList<Data>>& source_model)
    {
        if (m_source_model.get() != source_model.get()) {
            if (m_source_model.is_valid()) {
                m_source_model->template remove_listener<Biz::IListObserver<Data>>(this);
            }

            m_source_model = source_model;
            if (m_source_model.get()) {
                m_source_model->template add_listener<Biz::IListObserver<Data>>(this);
            }

            on_reset();
        }
    }

    void rebuild_index_map()
    {
        m_index_map.clear();
        for (size_t mapped_index = 0; mapped_index < m_mapped_indexes.size(); ++mapped_index) {
            m_index_map[m_mapped_indexes.at(mapped_index)] = mapped_index;
        }
    }

    /**
     * @return true if indexes changed
     */
    bool invalidate()
    {
        if (!m_source_model.is_valid()) {
            m_mapped_indexes.clear();
            m_index_map.clear();
            this->template invoke_listeners<IListObserver<Data>>([&](auto* l) { l->on_reset(); });
            return true;
        }

        std::vector<size_t> mapped_indexes;
        mapped_indexes.reserve(m_source_model->size());
        {
            std::unordered_set<GroupKey> group_keys_seen;
            for (size_t i = 0; i < m_source_model->size(); ++i) {
                const Data& source_item = m_source_model->at(i);

                if (m_filter_fn && !m_filter_fn(source_item)) {
                    continue; // Skip filtered items
                }

                if (m_group_by_fn && m_group_by_fn(source_item, group_keys_seen)) {
                    continue; // Skip subsequent items in the same group
                }

                mapped_indexes.push_back(i);
            }

            if (m_sort_fn) {
                std::sort(
                    mapped_indexes.begin(),
                    mapped_indexes.end(),
                    [this](const std::optional<size_t>& lhs, const std::optional<size_t>& rhs)
                    {
                        return m_sort_fn(
                            m_source_model->at(lhs.value()),
                            m_source_model->at(rhs.value())
                        );
                    }
                );
            }
        }

        if (m_mapped_indexes == mapped_indexes) {
            // Nothing has changed, do not reset

            return false;
        } else {
            // At least 1 index change, reset everything :((

            this->template invoke_listeners<IListObserver<Data>>(
                [&](auto* l) { l->on_will_be_reset(); }
            );

            m_mapped_indexes = mapped_indexes;
            rebuild_index_map();

            this->template invoke_listeners<IListObserver<Data>>([&](auto* l) { l->on_reset(); });

            return true;
        }
    }

private:
    void update_range(const IndexRange& range)
    {
        for (size_t index = range.from; index <= range.to; ++index) {
            IndexMap::const_iterator it = m_index_map.find(index);
            if (it != m_index_map.cend()) {
                this->template invoke_listeners<IListObserver<Data>>(
                    [it](auto* l) { l->on_updated(it->second); }
                );
            }
        }
    }

private:
    using IndexMap = std::unordered_map<size_t, size_t>;

    WeakerPointer<IObservableList<Data>> m_source_model{nullptr};
    std::vector<size_t> m_mapped_indexes;
    IndexMap m_index_map; // <source_index, mapped_index>

    FilterFn m_filter_fn;
    SortFn m_sort_fn;
    GroupByFn m_group_by_fn;
};

} // namespace Slic3r::Biz
