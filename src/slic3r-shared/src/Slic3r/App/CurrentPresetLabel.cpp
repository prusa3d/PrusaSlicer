#include "Slic3r/App/CurrentPresetLabel.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

CurrentPresetLabel::CurrentPresetLabel(
    Biz::Preset::PresetItemCompoundObservableList& observable_list
) :
    Text(std::string{}),
    m_observable_list(observable_list),
    m_observable_list_scope(observable_list, *this)
{}

void CurrentPresetLabel::on_will_be_reset(std::optional<size_t> new_size)
{
    if (m_created) {
        m_observable_list.at(m_current_list)
            .remove_listener<Biz::IListSelectionChangedListener>(this);
        m_created = false;
    }
}

void CurrentPresetLabel::on_reset()
{
    ASSERT(!m_created, "Selection Listener has to be cleaned");
    ASSERT(m_observable_list.size(), "Source compound list cannot be empty");
    m_current_list = std::min(m_observable_list.size() - 1, m_current_list);
    Biz::Preset::PresetItemObservableList& preset_list = m_observable_list.at(m_current_list);
    preset_list.add_listener<Biz::IListSelectionChangedListener>(this);
    m_created = true;
    on_list_selection_changed(preset_list.selected_index());
}

void CurrentPresetLabel::on_list_selection_changed(Domain::SelectionId new_selection)
{
    if (new_selection == Domain::INVALID_ID) {
        return;
    }

    const Biz::Preset::PresetItem& preset_item =
        m_observable_list.at(m_current_list).items().at(new_selection);

    set_text(preset_item.name);
}

size_t CurrentPresetLabel::current_list() const
{
    return m_current_list;
}

void CurrentPresetLabel::set_current_list(size_t current_list)
{
    if (m_current_list != current_list || !m_created) {
        ASSERT(m_observable_list.size() > current_list, "Cannot connect to non-existing list");
        on_will_be_reset();
        m_current_list = current_list;
        on_reset();
    }
}

} // namespace Slic3r::App
