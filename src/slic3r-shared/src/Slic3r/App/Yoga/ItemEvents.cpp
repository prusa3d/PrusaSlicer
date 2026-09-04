#include "Slic3r/App/Yoga/ItemEvents.hpp"

#include "Slic3r/App/Yoga/Item.hpp"
#include "Slic3r/App/Yoga/RootItem.hpp"

namespace Slic3r::App::Yoga {

Event::Event(Object* item) :
    m_item(item),
    m_object_heartbeat(item->heartbeat()),
    m_parent_heartbeat(item->parent()->heartbeat())
{
    ASSERT(item);
}

Event::~Event() {}

void Event::affected(const ChangeList& change_list) {}

bool Event::is_valid() const
{
    return !m_object_heartbeat.expired() && !m_parent_heartbeat.expired();
}

RemoveEvent::RemoveEvent(Item* item) : Event(item) {}

Event::ChangeList RemoveEvent::process()
{
    Object* parent = m_item->parent();
    ASSERT(parent);
    std::optional<size_t> index = parent->index_of(m_item);
    ASSERT(index.has_value());

    parent->remove(m_item);

    return {{Change::AffectedResult::Removed, parent, index.value()}};
}

MoveEvent::MoveEvent(Object* item, Object* new_parent, size_t new_index) :
    Event(item),
    m_new_parent(new_parent),
    m_new_parent_heartbeat(m_new_parent->heartbeat()),
    m_new_index(new_index)
{}

Event::ChangeList MoveEvent::process()
{
    Object* parent = m_item->parent();
    ASSERT(parent);
    std::optional<size_t> index = parent->index_of(m_item);
    ASSERT(index.has_value());

    m_new_parent->insert(parent->remove(m_item), m_new_index);

    return {
        {Change::AffectedResult::Removed, parent, index.value()},
        {Change::AffectedResult::Inserted, m_new_parent, m_new_index}
    };
}

LoopEvents::LoopEvents(RootItem& root_item) : m_root_item(root_item) {}

void LoopEvents::insert_event(EventPtr event)
{
    ASSERT(event);
    m_events.push_back(std::move(event));
}

unsigned int LoopEvents::process_events()
{
    unsigned int processed_events = 0;
    while (!m_events.empty()) {
        EventPtr event(std::move(m_events.front()));
        m_events.pop_front();

        // This is a fast temporary solution, that will *definitely* be refactored
        // once we are out of alpha, for real!

        if (!event->is_valid()) {
            continue;
        }

        const Event::ChangeList change_list = event->process();
        processed_events++;
        for (const EventPtr& event : m_events) {
            event->affected(change_list);
        }
    }

    return processed_events;
}

void MoveEvent::affected(const ChangeList& change_list)
{
    if (!is_valid()) {
        return;
    }

    for (const Change& change : change_list) {
        if (!m_item->parent() || change.parent != m_item->parent()) {
            return;
        }

        switch (change.affected_result) {
        case Change::AffectedResult::Inserted:
            if (m_new_index >= change.new_index) {
                m_new_index++;
            }
            break;
        case Change::AffectedResult::Removed:
            if (m_new_index > change.new_index) {
                m_new_index--;
            }
            break;
        }
    }
}

} // namespace Slic3r::App::Yoga
