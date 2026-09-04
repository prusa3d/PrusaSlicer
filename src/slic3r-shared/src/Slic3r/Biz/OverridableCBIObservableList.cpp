#include "Slic3r/Biz/OverridableCBIObservableList.hpp"

namespace Slic3r::Biz {

const OverridableConfigBoxInteractor& OverridableCBIObservableList::at(size_t index) const
{
    return m_items.at(index);
}

size_t OverridableCBIObservableList::size() const
{
    return m_items.size();
}

std::map<const OverridableConfigBoxInteractor*, OverridableConfigBoxInteractor::SetAccessor>
OverridableCBIObservableList::set_items(const std::vector<OverridableConfigBoxInteractor::ConfigBoxes>& config_boxes_list)
{
    invoke_listeners<IListObserver<OverridableConfigBoxInteractor>>(
        [&](IListObserver<OverridableConfigBoxInteractor>* l) { l->on_will_be_reset(); }
    );

    m_items.clear();
    m_items.reserve(config_boxes_list.size());

    std::map<const OverridableConfigBoxInteractor*, OverridableConfigBoxInteractor::SetAccessor>
        accessor_map;

    for (const OverridableConfigBoxInteractor::ConfigBoxes& config_boxes : config_boxes_list) {
        OverridableConfigBoxInteractor::SetAccessor accessor;
        m_items.emplace_back(accessor, config_boxes);
        accessor_map[&m_items.back()] = accessor;
    }

    invoke_listeners<IListObserver<OverridableConfigBoxInteractor>>(
        [](IListObserver<OverridableConfigBoxInteractor>* l) { l->on_reset(); }
    );

    return accessor_map;
}

} // namespace Slic3r::Biz
