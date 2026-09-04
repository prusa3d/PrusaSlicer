#pragma once

#include "Slic3r/Biz/Platform/ListenerScope.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"

#include "Slic3r/App/Yoga/Text.hpp"

namespace Slic3r::App {

class CurrentPresetLabel :
    public Yoga::Text,
    public Biz::IListObserver<Biz::Preset::PresetItemObservableList>,
    public Biz::IListSelectionChangedListener
{
public:
    CurrentPresetLabel(Biz::Preset::PresetItemCompoundObservableList& observable_list);

    void on_will_be_reset(std::optional<size_t> new_size = std::nullopt) override;
    void on_reset() override;
    void on_list_selection_changed(Domain::SelectionId new_selection) override;

    size_t current_list() const;
    void set_current_list(size_t current_list);

private:
    Biz::Preset::PresetItemCompoundObservableList& m_observable_list;

    Biz::ListenerScope<
        Biz::IListObserver<Biz::Preset::PresetItemObservableList>,
        Biz::Preset::PresetItemCompoundObservableList,
        CurrentPresetLabel>
        m_observable_list_scope;
    size_t m_current_list = 0;
    bool m_created        = false;
};

} // namespace Slic3r::App
