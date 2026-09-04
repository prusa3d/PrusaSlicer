#pragma once

#include "Slic3r/Domain/ConfigDef.hpp"

#include "Slic3r/Biz/OverrideItem.hpp"
#include "Slic3r/Biz/ObservableListSortFilter.hpp"

#include <vector>

namespace Slic3r::App {

class ObservableOverrideCategorizer :
    public Biz::ObservableListSortFilter<Biz::OverrideItem, Domain::ConfigItemDef::Category>
{
public:
    ObservableOverrideCategorizer();

    bool allow_disabled() const;
    void set_allow_disabled(bool allow_disabled);
    void set_default_categories(
        std::initializer_list<Domain::ConfigItemDef::Category> def_categories
    );
    void set_ignored_categories(
        std::initializer_list<Domain::ConfigItemDef::Category> ignored_categories
    );

private:
    bool m_allow_disabled = true;
    std::vector<Domain::ConfigItemDef::Category> m_def_categories;
    std::vector<Domain::ConfigItemDef::Category> m_ignored_categories;
};
} // namespace Slic3r::App
