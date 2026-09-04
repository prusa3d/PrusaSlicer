#pragma once

#include "Slic3r/Biz/IObservableList.hpp"
#include "Slic3r/Biz/OverridableConfigBoxInteractor.hpp"

#include <map>
#include <vector>

namespace Slic3r::Biz {

class OverridableCBIObservableList : public IObservableList<OverridableConfigBoxInteractor>
{
public:
    virtual const OverridableConfigBoxInteractor& at(size_t index) const;

    virtual size_t size() const;

    std::map<const OverridableConfigBoxInteractor*, OverridableConfigBoxInteractor::SetAccessor>
    set_items(const std::vector<OverridableConfigBoxInteractor::ConfigBoxes>& config_boxes_list);

private:
    std::vector<OverridableConfigBoxInteractor> m_items;
};

} // namespace Slic3r::Biz
