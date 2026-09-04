#pragma once

#include <Slic3r/Domain/ConfigDef.hpp>
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

namespace Slic3r::App {

template <class Data>
class DirtyCategoryList :
    public Biz::ObservableListSortFilter<Data, Domain::ConfigItemDef::Category>
{
public:
    using GetCategoryFn = std::function<Domain::ConfigItemDef::Category(const Data& data)>;

    void set_category_getter_fn(const GetCategoryFn& get_category_fn)
    {
        m_get_category_fn = get_category_fn;
    }

    bool contains(const Domain::ConfigItemDef::Category& category) const
    {
        for (size_t index{}; index < size(); index++) {
            if (m_get_category_fn(at(index)) == category) {
                return true;
            }
        }
        return false;
    }

    size_t size() const override {
        return Biz::ObservableListSortFilter<Data, Domain::ConfigItemDef::Category>::size();
    }
    const Data& at(size_t index) const override
    {
        return Biz::ObservableListSortFilter<Data, Domain::ConfigItemDef::Category>::at(index);
    }

private:
    GetCategoryFn m_get_category_fn;
};

} // namespace Slic3r::App
