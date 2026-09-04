#pragma once

#include "Slic3r/Biz/DataObserver.hpp"
#include "Slic3r/App/Yoga/Item.hpp"

namespace Slic3r::App::Yoga {

template <class Data, class BaseItem = Item>
class DataObserverItem : public BaseItem, public Biz::DataObserver<Data>
{
public:
    using InitFn = std::function<void(DataObserverItem<Data, BaseItem>&, size_t, const Data&)>;

    template <class... BaseArgs>
    DataObserverItem(size_t index, const Data& data, InitFn configure_fn, BaseArgs&&... base_args) :
        BaseItem(std::forward<BaseArgs>(base_args)...),
        Biz::DataObserver<Data>(index, data)
    {
        configure_fn(*this, index, data);
    }

    virtual ~DataObserverItem() = default;

    using DataUpdatedFn  = std::function<void(const Data* data)>;
    using IndexUpdatedFn = std::function<void(size_t index, const Data* data)>;

    DataObserverItem& set_on_data_updated(DataUpdatedFn fn)
    {
        data_updated_fn = std::move(fn);
        return *this;
    }

    DataObserverItem& set_on_index_updated(IndexUpdatedFn fn)
    {
        index_updated_fn = std::move(fn);
        return *this;
    }

    DataObserverItem& set_on_will_be_removed(std::function<void()> fn)
    {
        will_be_removed_fn = std::move(fn);
        return *this;
    }

    DataObserverItem& set_on_will_be_reset(std::function<void()> fn)
    {
        will_be_reset_fn = std::move(fn);
        return *this;
    }

    void on_view_will_be_removed() override
    {
        if (will_be_removed_fn) {
            will_be_removed_fn();
        }
    }

    void on_view_will_be_reset() override
    {
        if (will_be_reset_fn) {
            will_be_reset_fn();
        }
    }

    const Data* state() const
    {
        return this->m_state;
    }

    size_t index() const
    {
        return this->m_index;
    }

protected:
    void on_data_update() override
    {
        if (data_updated_fn) {
            data_updated_fn(this->m_state);
        }
    }

    void on_index_update() override
    {
        if (index_updated_fn) {
            index_updated_fn(this->m_index, this->m_state);
        }
    }

private:
    DataUpdatedFn data_updated_fn;
    IndexUpdatedFn index_updated_fn;
    std::function<void()> will_be_removed_fn;
    std::function<void()> will_be_reset_fn;
};

} // namespace Slic3r::App::Yoga
