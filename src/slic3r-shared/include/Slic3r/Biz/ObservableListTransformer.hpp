#pragma once

#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/IListObserver.hpp"

namespace Slic3r::Biz {

template <class SourceData, class TargetData>
class ObservableListTransformer :
    public IObservableList<TargetData>,
    public IListObserver<SourceData>
{
public:
    virtual ~ObservableListTransformer()
    {
        if (m_source_model.is_valid()) {
            m_source_model->template remove_listener<Biz::IListObserver<SourceData>>(this);
        }
    }

    using TransformFn = std::function<TargetData(const SourceData& source_data, size_t index)>;

    IObservableList<SourceData>* source_model() const
    {
        return m_source_model.get();
    }

    void set_source_model(IObservableList<SourceData>* source_model)
    {
        set_source_model(WeakerPointer<IObservableList<SourceData>>{source_model});
    }

    template <
        typename Derived,
        typename = std::enable_if_t<std::is_base_of_v<IObservableList<SourceData>, Derived>>>
    void set_source_model(const std::weak_ptr<Derived>& source_model)
    {
        set_source_model(
            WeakerPointer<IObservableList<SourceData>>{
                std::static_pointer_cast<IObservableList<SourceData>>(source_model.lock())
            }
        );
    }

    void set_source_model(const WeakerPointer<IObservableList<SourceData>>& source_model)
    {
        if (m_source_model != source_model) {
            if (m_source_model.is_valid()) {
                m_source_model->template remove_listener<Biz::IListObserver<SourceData>>(this);
            }

            m_source_model = source_model;
            if (m_source_model.is_valid()) {
                m_source_model->template add_listener<Biz::IListObserver<SourceData>>(this);
            }

            on_reset();
        }
    }

    void set_transform_fn(const TransformFn& transform_fn)
    {
        m_transform_fn = transform_fn;

        on_reset();
    }

    const TargetData& at(size_t index) const override
    {
        return m_transformed_items.at(index);
    }

    size_t size() const override
    {
        return m_transformed_items.size();
    }

    void on_inserted(const SourceData& data, size_t index) override
    {
        if (!m_transform_fn) {
            return;
        }

        m_transformed_items.insert(
            m_transformed_items.cbegin() + index,
            m_transform_fn(data, index)
        );

        this->template invoke_listeners<IListObserver<TargetData>>(
            [&](auto* l) { l->on_inserted(m_transformed_items.at(index), index); }
        );
    }

    void on_removed(const IndexRange& index_range) override
    {
        this->template invoke_listeners<IListObserver<TargetData>>(
            [index_range](auto* l) { l->on_will_be_removed(index_range); }
        );

        m_transformed_items.erase(
            m_transformed_items.begin() + index_range.from,
            m_transformed_items.begin() + index_range.to + 1
        );

        this->template invoke_listeners<IListObserver<TargetData>>([index_range](auto* l)
                                                                   { l->on_removed(index_range); });
    }

    void on_updated(const IndexRange& index_range) override
    {
        if (m_transform_fn && m_source_model.is_valid()) {
            for (size_t i = index_range.from; i <= index_range.to; ++i) {
                m_transformed_items[i] = m_transform_fn(m_source_model->at(i), i);
            }

            this->template invoke_listeners<IListObserver<TargetData>>(
                [&](auto* l) { l->on_updated({index_range}); }
            );
        }
    }

    void on_reset() override
    {
        this->template invoke_listeners<IListObserver<TargetData>>([&](IListObserver<TargetData>* l)
                                                                   { l->on_will_be_reset(); });

        m_transformed_items.clear();

        if (m_transform_fn && m_source_model.is_valid()) {
            const size_t source_size = m_source_model->size();
            m_transformed_items.reserve(source_size);
            for (size_t i = 0; i < source_size; ++i) {
                m_transformed_items.push_back(m_transform_fn(m_source_model->at(i), i));
            }
        }

        this->template invoke_listeners<IListObserver<TargetData>>([&](IListObserver<TargetData>* l)
                                                                   { l->on_reset(); });
    }

    void on_moved(size_t from, size_t to) override
    {
        if (from == to) {
            return;
        }

        if (from > to) {
            std::rotate(
                m_transformed_items.rend() - from - 1,
                m_transformed_items.rend() - from,
                m_transformed_items.rend() - to
            );
        } else {
            std::rotate(
                m_transformed_items.begin() + from,
                m_transformed_items.begin() + from + 1,
                m_transformed_items.begin() + to + 1
            );
        }

        this->template invoke_listeners<IListObserver<TargetData>>([&](IListObserver<TargetData>* l)
                                                                   { l->on_moved(from, to); });
    }

private:
    TransformFn m_transform_fn;
    WeakerPointer<IObservableList<SourceData>> m_source_model;
    std::vector<TargetData> m_transformed_items;
};

} // namespace Slic3r::Biz
