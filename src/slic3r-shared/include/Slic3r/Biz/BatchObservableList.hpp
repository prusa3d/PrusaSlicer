#pragma once

#include <vector>
#include "Slic3r/Biz/IObservableList.hpp"

namespace Slic3r::Biz {

namespace Details {
struct IBatchObservableListNotifier
{
    virtual ~IBatchObservableListNotifier()           = default;
    virtual void notify_item_changed_at(size_t index) = 0;
};

} // namespace Details

/**
 * @brief Simple implementation of Observable List allowing only changing the whole list at once
 * (batch, i.e. no per-item updates).
 * @tparam T Item type
 */
template <typename T>
class BatchObservableList :
    public IObservableList<T>,
    protected Details::IBatchObservableListNotifier
{
public:
    using Items = std::vector<T>;

    class WriteAccessor
    {
    public:
        void set_source(Details::IBatchObservableListNotifier& notifier, Items& items)
        {
            ASSERT(m_items == nullptr);
            m_notifier = &notifier;
            m_items    = &items;
        }

        void mutate_at(size_t idx, std::function<void(T&)> mutator)
        {
            ASSERT(m_items != nullptr);
            ASSERT(idx < m_items->size());
            ASSERT(m_notifier != nullptr);

            mutator(m_items->at(idx));
            m_notifier->notify_item_changed_at(idx);
        }

    private:
        Details::IBatchObservableListNotifier* m_notifier{nullptr};
        Items* m_items{nullptr};
    };

    BatchObservableList() = default;

    explicit BatchObservableList(WriteAccessor& writer)
    {
        writer.set_source(*this, m_items);
    }

    const T& at(size_t index) const override
    {
        return m_items.at(index);
    }

    size_t size() const override
    {
        return m_items.size();
    }

    void set_items(const Items& data)
    {
        IObservableList<T>::template invoke_listeners<IListObserver<T>>([&](IListObserver<T>* l)
                                                                        { l->on_will_be_reset(data.size()); });
        m_items = data;
        IObservableList<T>::template invoke_listeners<IListObserver<T>>([](IListObserver<T>* l)
                                                                        { l->on_reset(); });
    }

    void set_items(Items&& data)
    {
        IObservableList<T>::template invoke_listeners<IListObserver<T>>([&](IListObserver<T>* l)
                                                                        { l->on_will_be_reset(data.size()); });
        m_items = data;
        IObservableList<T>::template invoke_listeners<IListObserver<T>>([](IListObserver<T>* l)
                                                                        { l->on_reset(); });
    }

private:
    void notify_item_changed_at(size_t index) override
    {
        IObservableList<T>::template invoke_listeners<IListObserver<T>>([index](IListObserver<T>* l)
                                                                        { l->on_updated(index); });
    }

protected:
    Items m_items;
};

template <typename T>
class MutableBatchObservableList : public BatchObservableList<T>
{
    using Base = BatchObservableList<T>;

public:
    using Base::at; // avoid hiding the const overload

    MutableBatchObservableList() = default;

    explicit MutableBatchObservableList(Base::WriteAccessor& writer)
    {
        writer.set_source(*this, this->m_items);
    }

    T& at(size_t index)
    {
        return this->m_items.at(index);
    }
};

} // namespace Slic3r::Biz
